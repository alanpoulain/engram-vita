#include "gfx.h"

#include <math.h>
#include <psp2/display.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/pvf.h>
#include <psp2/sysmodule.h>
#include <stdlib.h>
#include <string.h>

#define FB_BYTES 0x200000                   // 960 x 544 x 4, rounded up to the 256 KB CDRAM wants
#define LAYER_BYTES (SCREEN_W * SCREEN_H * 4) // a multiple of 4 KB as it is
#define LINE_H_MAX 96

static uint32_t *fbs[2];
static int back;
static uint32_t *base, *overlay, *scene, *saved;
static LiquidLayers layers;
static ScePvfLibId pvf_lib;
static ScePvfFontId fonts[2]; // by GfxFont
static ScePvfFontId pvf_font; // the one in use
static uint8_t line_buf[SCREEN_W * LINE_H_MAX]; // one line of text, 8 bits a pixel

static void *alloc_block(const char *name, SceKernelMemBlockType type, unsigned size) {
	void *ptr = NULL;
	SceUID block = sceKernelAllocMemBlock(name, type, size, NULL);
	if (block < 0 || sceKernelGetMemBlockBase(block, &ptr) < 0)
		return NULL;
	return ptr;
}

static ScePvfPointer pvf_alloc(ScePvfPointer user, ScePvfU32 size) {
	(void)user;
	return malloc(size);
}

static ScePvfPointer pvf_realloc(ScePvfPointer user, ScePvfPointer old, ScePvfU32 size) {
	(void)user;
	return realloc(old, size);
}

static void pvf_free(ScePvfPointer user, ScePvfPointer ptr) {
	(void)user;
	free(ptr);
}

// The backdrop: a dark gradient, dithered so it doesn't band.
static void draw_base(void) {
	static const uint8_t bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
	static const int top[3] = {14, 16, 24}, bottom[3] = {28, 34, 62};
	for (int y = 0; y < SCREEN_H; y++) {
		int v[3];
		for (int ch = 0; ch < 3; ch++)
			v[ch] = top[ch] * 256 + ((bottom[ch] - top[ch]) * 256 * y) / (SCREEN_H - 1);
		uint32_t *row = base + y * SCREEN_W;
		for (int x = 0; x < SCREEN_W; x++) {
			int d = bayer[y & 3][x & 3] * 16;
			row[x] = 0xFF000000u | RGB((v[0] + d) >> 8, (v[1] + d) >> 8, (v[2] + d) >> 8);
		}
	}
}

// A font file of the VPK, read whole and opened from memory (the library
// would otherwise keep going back to the file for each letter).
static ScePvfFontId open_font(const char *path) {
	SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
	if (fd < 0)
		return NULL;
	int size = (int)sceIoLseek(fd, 0, SCE_SEEK_END);
	sceIoLseek(fd, 0, SCE_SEEK_SET);
	void *data = size > 0 ? malloc((size_t)size) : NULL;
	bool whole = data && sceIoRead(fd, data, (SceSize)size) == size;
	sceIoClose(fd);
	ScePvfError err = 0;
	ScePvfFontId font = whole ? scePvfOpenUserMemory(pvf_lib, data, (ScePvfU32)size, &err) : NULL;
	if (!font || err) {
		free(data);
		return NULL;
	}
	return font; // the font reads from data for as long as the app runs
}

// The system's own Latin font, for when the VPK's can't be opened.
static ScePvfFontId open_system_font(void) {
	ScePvfError err = 0;
	ScePvfFontId font = scePvfOpenDefaultLatinFontOnSharedMemory(pvf_lib, &err);
	if (font && !err)
		return font;
	// The shared font isn't there when the home screen didn't open it: its file instead.
	err = 0;
	font = scePvfOpenUserFile(pvf_lib, (ScePvfPointer) "sa0:data/font/pvf/ltn0.pvf", 0, &err);
	return err ? NULL : font;
}

bool gfx_init(void) {
	fbs[0] = alloc_block("installer_fb0", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, FB_BYTES);
	fbs[1] = alloc_block("installer_fb1", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, FB_BYTES);
	base = alloc_block("installer_base", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, LAYER_BYTES);
	overlay = alloc_block("installer_overlay", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, LAYER_BYTES);
	scene = alloc_block("installer_scene", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, LAYER_BYTES);
	saved = alloc_block("installer_saved", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, LAYER_BYTES);
	if (!fbs[0] || !fbs[1] || !base || !overlay || !scene || !saved)
		return false;
	layers.base = base;
	layers.overlay = overlay;
	layers.scene = scene;
	draw_base();
	memset(overlay, 0, LAYER_BYTES);
	memcpy(scene, base, LAYER_BYTES);

	sceSysmoduleLoadModule(SCE_SYSMODULE_PGF); // the font library, libpvf
	ScePvfInitRec rec;
	memset(&rec, 0, sizeof(rec));
	rec.maxNumFonts = 4;
	rec.allocFunc = pvf_alloc;
	rec.reallocFunc = pvf_realloc;
	rec.freeFunc = pvf_free;
	ScePvfError err = 0;
	pvf_lib = scePvfNewLib(&rec, &err);
	if (!pvf_lib)
		return true; // no text, but the buttons still work
	scePvfSetResolution(pvf_lib, 72.0f, 72.0f); // a point is a pixel
	fonts[FONT_TEXT] = open_font("app0:font/Poppins-Regular.ttf");
	if (!fonts[FONT_TEXT])
		fonts[FONT_TEXT] = open_system_font();
	fonts[FONT_TITLE] = open_font("app0:font/Righteous-Regular.ttf");
	if (!fonts[FONT_TITLE])
		fonts[FONT_TITLE] = fonts[FONT_TEXT];
	pvf_font = fonts[FONT_TEXT];
	return true;
}

bool gfx_has_font(void) {
	return pvf_font != NULL;
}

void gfx_font(GfxFont font) {
	pvf_font = fonts[font];
}

const LiquidLayers *gfx_layers(void) {
	return &layers;
}

void gfx_begin(void) {
	memset(overlay, 0, LAYER_BYTES);
}

void gfx_save(void) {
	memcpy(saved, overlay, LAYER_BYTES);
}

void gfx_restore(void) {
	memcpy(overlay, saved, LAYER_BYTES);
}

void gfx_end(void) {
	for (int p = 0; p < SCREEN_W * SCREEN_H; p++) {
		uint32_t o = overlay[p], b = base[p];
		int oa = (int)(o >> 24);
		if (!oa) {
			scene[p] = b;
			continue;
		}
		int keep = 256 - oa - (oa >> 7);
		int r = (int)(o & 0xFF) + ((int)((b & 0xFF) * keep) >> 8);
		int g = (int)((o >> 8) & 0xFF) + ((int)(((b >> 8) & 0xFF) * keep) >> 8);
		int bl = (int)((o >> 16) & 0xFF) + ((int)(((b >> 16) & 0xFF) * keep) >> 8);
		scene[p] = 0xFF000000u | RGB(r > 255 ? 255 : r, g > 255 ? 255 : g, bl > 255 ? 255 : bl);
	}
}

// rgb at alpha a (0 to 255) over the overlay's pixel p, which is premultiplied.
static inline void over_px(int p, uint32_t rgb, int a) {
	if (a <= 0)
		return;
	if (a > 255)
		a = 255;
	uint32_t d = overlay[p];
	int sa = a + (a >> 7), keep = 256 - sa;
	int r = (int)(((rgb & 0xFF) * sa) >> 8) + (int)(((d & 0xFF) * keep) >> 8);
	int g = (int)((((rgb >> 8) & 0xFF) * sa) >> 8) + (int)((((d >> 8) & 0xFF) * keep) >> 8);
	int b = (int)((((rgb >> 16) & 0xFF) * sa) >> 8) + (int)((((d >> 16) & 0xFF) * keep) >> 8);
	int da = a + (int)(((d >> 24) * keep) >> 8);
	overlay[p] = ((uint32_t)(da > 255 ? 255 : da) << 24) | RGB(r > 255 ? 255 : r, g > 255 ? 255 : g, b > 255 ? 255 : b);
}

// How far inside a rounded rectangle a pixel's centre is (negative: outside).
static inline float inside_rounded(float px, float py, float cx, float cy, float hw, float hh, float radius) {
	float qx = fabsf(px - cx) - (hw - radius), qy = fabsf(py - cy) - (hh - radius);
	float ox = qx > 0.0f ? qx : 0.0f, oy = qy > 0.0f ? qy : 0.0f;
	float outside = ox > 0.0f && oy > 0.0f ? sqrtf(ox * ox + oy * oy) : ox + oy;
	float inner = qx > qy ? qx : qy;
	return radius - outside - (inner < 0.0f ? inner : 0.0f);
}

void gfx_panel(int x, int y, int w, int h, int radius, uint32_t rgb, int alpha) {
	float cx = (float)x + (float)w * 0.5f, cy = (float)y + (float)h * 0.5f;
	for (int j = y < 0 ? 0 : y; j < y + h && j < SCREEN_H; j++)
		for (int i = x < 0 ? 0 : x; i < x + w && i < SCREEN_W; i++) {
			float d = inside_rounded((float)i + 0.5f, (float)j + 0.5f, cx, cy, (float)w * 0.5f, (float)h * 0.5f,
			                         (float)radius);
			if (d <= -0.5f)
				continue;
			float cover = d >= 0.5f ? 1.0f : d + 0.5f;
			int p = j * SCREEN_W + i;
			over_px(p, rgb, (int)((float)alpha * cover));
			// the edge: a pixel-wide lighter line just inside, stronger at the top
			float edge = 1.0f - fabsf(d - 1.0f);
			if (edge > 0.0f) {
				int strength = 26 + (22 * (y + h - j)) / h;
				over_px(p, RGB(255, 255, 255), (int)((float)strength * edge));
			}
		}
}

void gfx_dot(int cx, int cy, int radius, uint32_t rgb, int alpha) {
	for (int j = cy - radius - 1; j <= cy + radius + 1; j++)
		for (int i = cx - radius - 1; i <= cx + radius + 1; i++) {
			if (i < 0 || j < 0 || i >= SCREEN_W || j >= SCREEN_H)
				continue;
			float dx = (float)(i - cx), dy = (float)(j - cy);
			float d = (float)radius + 0.5f - sqrtf(dx * dx + dy * dy);
			if (d > 0.0f)
				over_px(j * SCREEN_W + i, rgb, (int)((float)alpha * (d > 1.0f ? 1.0f : d)));
		}
}

int gfx_text_width(float size, const char *s) {
	if (!pvf_font)
		return 0;
	scePvfSetCharSize(pvf_font, size, size);
	int pen64 = 0;
	for (const unsigned char *c = (const unsigned char *)s; *c; c++) {
		ScePvfCharInfo info;
		memset(&info, 0, sizeof(info));
		if (scePvfGetCharInfo(pvf_font, *c, &info) >= 0)
			pen64 += info.glyphMetrics.horizontalAdvance64;
	}
	return (pen64 + 63) / 64;
}

#define TEXT_PAD 2 // room to the left of the pen, for letters that reach back

int gfx_text(int x, int y, float size, uint32_t rgb, int alpha, const char *s) {
	if (!pvf_font || !s[0])
		return 0;
	int width = gfx_text_width(size, s);
	int line_h = (int)(size * 1.5f) + 2;
	if (line_h > LINE_H_MAX)
		line_h = LINE_H_MAX;
	int span = width + (int)size + 2 * TEXT_PAD;
	if (span > SCREEN_W)
		span = SCREEN_W;
	for (int j = 0; j < line_h; j++)
		memset(line_buf + j * SCREEN_W, 0, span);
	scePvfSetCharSize(pvf_font, size, size);
	int pen64 = TEXT_PAD * 64, base64 = (int)(size * 1.12f) * 64;
	for (const unsigned char *c = (const unsigned char *)s; *c && pen64 < span * 64; c++) {
		ScePvfCharInfo info;
		memset(&info, 0, sizeof(info));
		if (scePvfGetCharInfo(pvf_font, *c, &info) < 0)
			continue;
		if (*c != ' ') {
			ScePvfUserImageBufferRec image;
			memset(&image, 0, sizeof(image));
			image.pixelFormat = SCE_PVF_USERIMAGE_DIRECT8;
			image.xPos64 = pen64;
			image.yPos64 = base64;
			image.rect.width = span;
			image.rect.height = line_h;
			image.bytesPerLine = SCREEN_W;
			image.buffer = line_buf;
			scePvfGetCharGlyphImage(pvf_font, *c, &image);
		}
		pen64 += info.glyphMetrics.horizontalAdvance64;
	}
	for (int j = 0; j < line_h; j++) {
		int sy = y + j;
		if (sy < 0 || sy >= SCREEN_H)
			continue;
		const uint8_t *row = line_buf + j * SCREEN_W;
		for (int i = 0; i < span; i++) {
			int sx = x - TEXT_PAD + i;
			if (row[i] && sx >= 0 && sx < SCREEN_W)
				over_px(sy * SCREEN_W + sx, rgb, (row[i] * alpha) / 255);
		}
	}
	return width;
}

int gfx_text_shadow(int x, int y, float size, uint32_t rgb, const char *s) {
	gfx_text(x + 1, y + 2, size, RGB(0, 0, 0), 150, s);
	gfx_text(x - 1, y + 1, size, RGB(0, 0, 0), 90, s);
	return gfx_text(x, y, size, rgb, 255, s);
}

int gfx_text_wrap(int x, int y, int width, float size, int line_h, uint32_t rgb, const char *s) {
	char line[160];
	int len = 0;
	line[0] = '\0';
	while (*s) {
		while (*s == ' ')
			s++;
		const char *end = s;
		while (*end && *end != ' ')
			end++;
		int word = (int)(end - s);
		if (!word)
			break;
		int add = word + (len ? 1 : 0);
		if (len + add >= (int)sizeof(line))
			break;
		// Try the word on this line; if it's too wide, it starts the next.
		if (len)
			line[len] = ' ';
		memcpy(line + len + (len ? 1 : 0), s, (size_t)word);
		line[len + add] = '\0';
		if (len && gfx_text_width(size, line) > width) {
			line[len] = '\0';
			gfx_text(x, y, size, rgb, 255, line);
			y += line_h;
			memcpy(line, s, (size_t)word);
			len = word;
			line[len] = '\0';
		} else {
			len += add;
		}
		s = end;
	}
	if (len) {
		gfx_text(x, y, size, rgb, 255, line);
		y += line_h;
	}
	return y;
}

uint32_t *gfx_back(void) {
	return fbs[back];
}

int gfx_back_index(void) {
	return back;
}

void gfx_present(void) {
	SceDisplayFrameBuf param;
	memset(&param, 0, sizeof(param));
	param.size = sizeof(param);
	param.base = fbs[back];
	param.pitch = SCREEN_W;
	param.pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;
	param.width = SCREEN_W;
	param.height = SCREEN_H;
	sceDisplaySetFrameBuf(&param, SCE_DISPLAY_SETBUF_NEXTFRAME);
	sceDisplayWaitVblankStart();
	back ^= 1;
}
