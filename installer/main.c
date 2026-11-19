// Engram Installer: puts engram.suprx in ur0:tai and its lines in taiHEN's
// config.txt, or takes them out again.
//
// The screen is drawn by the app itself, on the CPU: a ball of liquid that
// swooshes around (liquid.c) behind glass panels and text (gfx.c) in the
// fonts of its LiveArea, which the VPK carries, so it needs nothing beyond the
// SDK. It writes to ur0:, so the VPK is built as unsafe homebrew.
//
// What it touches, and nothing else:
// - ur0:tai/engram.suprx (copied from app0:engram.suprx in the VPK);
// - the config.txt taiHEN reads (ux0:tai/config.txt if it's there, otherwise
//   ur0:tai/config.txt): Engram's line under *main, *NPXS10015 and *NPXS10016,
//   straight after henkaku.suprx under the two Settings sections (with other
//   Settings plugins, such as Turbo Storage, Engram's rows then fit after
//   theirs). Before each change the old file is copied beside it as
//   config.txt.engram-installer-old, and the first time also to
//   ur0:data/engram/tai_config_before_installer.txt;
// - ur0:data/engram and its fonts folder, made if they're missing, and the
//   failsafe's disabled and booting markers, removed on install;
// - with the menu's "Add the fonts" on, the two fonts this app's own text is
//   in (Poppins and Righteous, from app0:font), copied to that fonts folder
//   for Engram's Font settings. A font that's there already is left as it is.
// "Check for a new version" asks GitHub (update.c). With a newer release that
// has an installer, START downloads its VPK, keeps a copy at
// ux0:data/engram_installer.vpk and puts its files in place of this app's own
// in ux0:app/ENGR00001. Nothing is sent but the request itself.
// Uninstalling takes out every Engram line (and Engram's own comments) from
// config.txt and deletes ur0:tai/engram.suprx. Engram's settings, fonts and
// logs in ur0:data/engram stay.

#include <psp2/appmgr.h>
#include <psp2/ctrl.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/power.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "files.h"
#include "gfx.h"
#include "update.h"

#define INSTALLER_VERSION "1.4"

#define BUNDLED_PLUGIN "app0:engram.suprx"
#define PLUGIN_PATH "ur0:tai/engram.suprx"
#define PLUGIN_LINE "ur0:tai/engram.suprx"
#define TAI_UX0 "ux0:tai/config.txt" // taiHEN reads this one if it's there,
#define TAI_UR0 "ur0:tai/config.txt" // and this one otherwise
#define DATA_DIR "ur0:data/engram"
#define FONT_DIR DATA_DIR "/fonts"
#define FIRST_BACKUP DATA_DIR "/tai_config_before_installer.txt"
#define CONFIG_MAX (256 * 1024)
#define PLUGIN_MAX (4 * 1024 * 1024)
#define FONT_MAX (2 * 1024 * 1024)

#define COLOR_TEXT RGB(0xEC, 0xEC, 0xF0)
#define COLOR_DIM RGB(0xA8, 0xAE, 0xBE)
#define COLOR_ACCENT RGB(0x7C, 0xC0, 0xFF)
#define COLOR_GOOD RGB(0x76, 0xE0, 0x96)
#define COLOR_BAD RGB(0xFF, 0x84, 0x84)
#define COLOR_PANEL RGB(10, 12, 20)

// ---- the log on screen ----

#define LOG_LINES 13
static char log_text[LOG_LINES][128];
static uint32_t log_color[LOG_LINES];
static int log_count;

static void log_add(uint32_t color, const char *fmt, ...) {
	if (log_count == LOG_LINES) {
		memmove(log_text, log_text[1], sizeof(log_text[0]) * (LOG_LINES - 1));
		memmove(log_color, log_color + 1, sizeof(log_color[0]) * (LOG_LINES - 1));
		log_count--;
	}
	va_list args;
	va_start(args, fmt);
	vsnprintf(log_text[log_count], sizeof(log_text[0]), fmt, args);
	va_end(args);
	log_color[log_count++] = color;
}

static void log_drop_last(void) {
	if (log_count)
		log_count--;
}

static void log_clear(void) {
	log_count = 0;
}

// ---- files ----

// Puts new_path in place of path: a rename, and where it can't replace a
// file, path is removed first and, if the rename still fails, old is written
// back.
static bool replace_file(const char *path, const char *new_path, const char *old, int old_len) {
	if (sceIoRename(new_path, path) >= 0)
		return true;
	sceIoRemove(path);
	if (sceIoRename(new_path, path) >= 0)
		return true;
	if (old)
		file_write(path, old, old_len);
	return false;
}

// ---- taiHEN's config.txt ----

typedef struct {
	char **line; // without their line ends
	int count, cap;
	bool crlf; // the file's own line ends
	bool final_newline;
} Lines;

static void lines_free(Lines *ls) {
	for (int i = 0; i < ls->count; i++)
		free(ls->line[i]);
	free(ls->line);
	memset(ls, 0, sizeof(*ls));
}

// Takes the first len characters of s as the new line at.
static bool lines_insert_n(Lines *ls, int at, const char *s, size_t len) {
	if (ls->count == ls->cap) {
		int cap = ls->cap ? ls->cap * 2 : 64;
		char **grown = realloc(ls->line, sizeof(char *) * cap);
		if (!grown)
			return false;
		ls->line = grown;
		ls->cap = cap;
	}
	char *copy = malloc(len + 1);
	if (!copy)
		return false;
	memcpy(copy, s, len);
	copy[len] = '\0';
	memmove(ls->line + at + 1, ls->line + at, sizeof(char *) * (ls->count - at));
	ls->line[at] = copy;
	ls->count++;
	return true;
}

static bool lines_insert(Lines *ls, int at, const char *s) {
	return lines_insert_n(ls, at, s, strlen(s));
}

static void lines_remove(Lines *ls, int at) {
	free(ls->line[at]);
	memmove(ls->line + at, ls->line + at + 1, sizeof(char *) * (ls->count - at - 1));
	ls->count--;
}

static bool lines_parse(Lines *ls, const char *text) {
	memset(ls, 0, sizeof(*ls));
	ls->crlf = strstr(text, "\r\n") != NULL;
	size_t n = strlen(text);
	ls->final_newline = n > 0 && text[n - 1] == '\n';
	const char *p = text;
	while (*p) {
		const char *end = strchr(p, '\n');
		size_t len = end ? (size_t)(end - p) : strlen(p);
		if (len && p[len - 1] == '\r')
			len--;
		if (!lines_insert_n(ls, ls->count, p, len))
			return false;
		if (!end)
			break;
		p = end + 1;
	}
	return true;
}

static char *lines_join(const Lines *ls, int *len) {
	const char *eol = ls->crlf ? "\r\n" : "\n";
	size_t total = 1;
	for (int i = 0; i < ls->count; i++)
		total += strlen(ls->line[i]) + 2;
	char *out = malloc(total);
	if (!out)
		return NULL;
	size_t at = 0;
	for (int i = 0; i < ls->count; i++) {
		size_t l = strlen(ls->line[i]);
		memcpy(out + at, ls->line[i], l);
		at += l;
		if (i + 1 < ls->count || ls->final_newline) {
			memcpy(out + at, eol, strlen(eol));
			at += strlen(eol);
		}
	}
	out[at] = '\0';
	*len = (int)at;
	return out;
}

// The line with its surrounding spaces taken off, in buf.
static const char *trimmed(const char *s, char *buf, size_t size) {
	while (*s == ' ' || *s == '\t')
		s++;
	size_t n = strlen(s);
	while (n && (s[n - 1] == ' ' || s[n - 1] == '\t'))
		n--;
	if (n >= size)
		n = size - 1;
	memcpy(buf, s, n);
	buf[n] = '\0';
	return buf;
}

static bool ends_with_nocase(const char *s, const char *tail) {
	size_t a = strlen(s), b = strlen(tail);
	return a >= b && strcasecmp(s + a - b, tail) == 0;
}

static bool is_engram_line(const char *line) {
	char t[512];
	trimmed(line, t, sizeof(t));
	return t[0] && t[0] != '#' && ends_with_nocase(t, "/engram.suprx");
}

// Engram's own comments, from its Fonts > Apps config sync.
static bool is_engram_comment(const char *line) {
	char t[512];
	trimmed(line, t, sizeof(t));
	return strncasecmp(t, "# Engram:", 9) == 0;
}

static bool is_section(const char *line, const char *name) {
	char t[512];
	trimmed(line, t, sizeof(t));
	return t[0] == '*' && strcasecmp(t + 1, name) == 0;
}

static bool is_any_section(const char *line) {
	char t[512];
	trimmed(line, t, sizeof(t));
	return t[0] == '*';
}

// The first line of a section, its header's index, or -1.
static int section_start(const Lines *ls, const char *name) {
	for (int i = 0; i < ls->count; i++)
		if (is_section(ls->line[i], name))
			return i;
	return -1;
}

// The index after a section's last line that isn't blank or a comment.
static int section_end(const Lines *ls, int header) {
	int last = header;
	for (int i = header + 1; i < ls->count && !is_any_section(ls->line[i]); i++) {
		char t[512];
		trimmed(ls->line[i], t, sizeof(t));
		if (t[0] && t[0] != '#')
			last = i;
	}
	return last + 1;
}

// Engram's line under a section: any earlier Engram line there taken out,
// then the line after henkaku.suprx if after_henkaku and it's there, at the
// end of the section otherwise. A missing section is added at the end.
static bool put_in_section(Lines *ls, const char *name, bool after_henkaku) {
	int header = section_start(ls, name);
	if (header < 0) {
		if (ls->count && ls->line[ls->count - 1][0])
			lines_insert(ls, ls->count, "");
		char head[64];
		snprintf(head, sizeof(head), "*%s", name);
		return lines_insert(ls, ls->count, head) && lines_insert(ls, ls->count, PLUGIN_LINE);
	}
	for (int i = header + 1; i < ls->count && !is_any_section(ls->line[i]);)
		if (is_engram_line(ls->line[i]))
			lines_remove(ls, i);
		else
			i++;
	int at = -1;
	if (after_henkaku)
		for (int i = header + 1; i < ls->count && !is_any_section(ls->line[i]); i++) {
			char t[512];
			trimmed(ls->line[i], t, sizeof(t));
			if (t[0] != '#' && ends_with_nocase(t, "/henkaku.suprx")) {
				at = i + 1;
				break;
			}
		}
	if (at < 0)
		at = section_end(ls, header);
	return lines_insert(ls, at, PLUGIN_LINE);
}

static const char *tai_config_path(void) {
	return file_exists(TAI_UX0) ? TAI_UX0 : TAI_UR0;
}

// Reads taiHEN's config, changes it with edit(), and writes it back safely.
// unchanged: what to say when edit() had nothing to do.
static bool change_config(bool (*edit)(Lines *), int *changes, const char *unchanged) {
	const char *path = tai_config_path();
	int len = 0;
	char *old = file_read(path, CONFIG_MAX, &len);
	if (!old) {
		log_add(COLOR_BAD, "Couldn't read %s.", path);
		return false;
	}
	log_add(COLOR_DIM, "taiHEN's config: %s", path);
	Lines ls;
	if (!lines_parse(&ls, old)) {
		free(old);
		log_add(COLOR_BAD, "Out of memory reading the config.");
		return false;
	}
	bool ok = edit(&ls);
	int new_len = 0;
	char *text_new = ok ? lines_join(&ls, &new_len) : NULL;
	*changes = text_new ? (new_len != len || memcmp(text_new, old, len) != 0) : 0;
	lines_free(&ls);
	if (!text_new) {
		free(old);
		log_add(COLOR_BAD, "Couldn't change the config.");
		return false;
	}
	if (!*changes) {
		free(old);
		free(text_new);
		log_add(COLOR_DIM, "%s", unchanged);
		return true;
	}
	sceIoMkdir(DATA_DIR, 0777);
	if (!file_exists(FIRST_BACKUP))
		file_write(FIRST_BACKUP, old, len);
	char side[96], fresh[96];
	snprintf(side, sizeof(side), "%s.engram-installer-old", path);
	snprintf(fresh, sizeof(fresh), "%s.engram-installer-new", path);
	bool done = file_write_checked(side, old, len) && file_write_checked(fresh, text_new, new_len) &&
	            replace_file(path, fresh, old, len);
	if (done) {
		int back_len = 0;
		char *back = file_read(path, CONFIG_MAX, &back_len);
		done = back && back_len == new_len && memcmp(back, text_new, new_len) == 0;
		free(back);
	}
	if (done)
		log_add(COLOR_GOOD, "Config updated. The old one is %s", side);
	else
		log_add(COLOR_BAD, "Couldn't write the config: it was left as it was.");
	sceIoRemove(fresh);
	free(old);
	free(text_new);
	return done;
}

static bool edit_install(Lines *ls) {
	return put_in_section(ls, "main", false) && put_in_section(ls, "NPXS10015", true) &&
	       put_in_section(ls, "NPXS10016", true);
}

static bool edit_uninstall(Lines *ls) {
	for (int i = 0; i < ls->count;)
		if (is_engram_line(ls->line[i]) || is_engram_comment(ls->line[i]))
			lines_remove(ls, i);
		else
			i++;
	return true;
}

// ---- install and uninstall ----

// The fonts the VPK carries (the app's own text is in them), for Engram's Font
// settings when the menu's switch is on.
static const char *const BUNDLED_FONTS[] = {"Poppins-Regular.ttf", "Righteous-Regular.ttf"};
#define BUNDLED_FONT_COUNT 2
static bool add_fonts = true;

// Copies them to Engram's fonts folder. One that's there already stays as it
// is: it may be the user's own file of that name.
static void install_fonts(void) {
	int added = 0, there = 0, failed = 0;
	for (int i = 0; i < BUNDLED_FONT_COUNT; i++) {
		char from[96], to[96];
		snprintf(from, sizeof(from), "app0:font/%s", BUNDLED_FONTS[i]);
		snprintf(to, sizeof(to), FONT_DIR "/%s", BUNDLED_FONTS[i]);
		if (file_exists(to)) {
			there++;
			continue;
		}
		int len = 0;
		char *font = file_read(from, FONT_MAX, &len);
		if (font && file_write_checked(to, font, len)) {
			added++;
		} else {
			sceIoRemove(to); // not half a font
			failed++;
		}
		free(font);
	}
	if (failed)
		log_add(COLOR_BAD, "Couldn't copy %d of the fonts to " FONT_DIR ".", failed);
	else if (added)
		log_add(COLOR_GOOD, "Added %s to " FONT_DIR ".", added == 2 ? "Poppins and Righteous" : "the missing font");
	else if (there)
		log_add(COLOR_TEXT, "Poppins and Righteous were in " FONT_DIR " already.");
}

static bool install(void) {
	log_clear();
	int len = 0;
	char *plugin = file_read(BUNDLED_PLUGIN, PLUGIN_MAX, &len);
	if (!plugin) {
		log_add(COLOR_BAD, "engram.suprx is missing from this installer.");
		return false;
	}
	sceIoMkdir("ur0:tai", 0777);
	bool copied = file_write_checked(PLUGIN_PATH ".installer-new", plugin, len) &&
	              replace_file(PLUGIN_PATH, PLUGIN_PATH ".installer-new", NULL, 0);
	if (copied) {
		int back_len = 0;
		char *back = file_read(PLUGIN_PATH, PLUGIN_MAX, &back_len);
		copied = back && back_len == len && memcmp(back, plugin, len) == 0;
		free(back);
	}
	free(plugin);
	sceIoRemove(PLUGIN_PATH ".installer-new");
	if (!copied) {
		log_add(COLOR_BAD, "Couldn't copy engram.suprx to " PLUGIN_PATH ".");
		return false;
	}
	log_add(COLOR_GOOD, "Copied engram.suprx (%d bytes) to " PLUGIN_PATH ".", len);

	int changes = 0;
	if (!change_config(edit_install, &changes, "The config had Engram's lines already."))
		return false;
	log_add(COLOR_TEXT, "Engram is under *main, *NPXS10015 and *NPXS10016.");

	sceIoMkdir(DATA_DIR, 0777);
	sceIoMkdir(FONT_DIR, 0777);
	if (add_fonts)
		install_fonts();
	else
		log_add(COLOR_TEXT, "Fonts go in " FONT_DIR ".");
	bool was_off = file_exists(DATA_DIR "/disabled");
	sceIoRemove(DATA_DIR "/disabled");
	sceIoRemove(DATA_DIR "/booting");
	if (was_off)
		log_add(COLOR_TEXT, "Engram was turned off by its failsafe: it's on again.");
	log_add(COLOR_GOOD, "Done. Restart the Vita to start Engram.");
	return true;
}

static bool uninstall(void) {
	log_clear();
	int changes = 0;
	if (!change_config(edit_uninstall, &changes, "The config had no Engram lines."))
		return false;
	if (file_exists(PLUGIN_PATH)) {
		if (sceIoRemove(PLUGIN_PATH) < 0) {
			log_add(COLOR_BAD, "Couldn't delete " PLUGIN_PATH ".");
			return false;
		}
		log_add(COLOR_GOOD, "Deleted " PLUGIN_PATH ".");
	}
	log_add(COLOR_TEXT, "Your settings, fonts and logs stay in " DATA_DIR ".");
	log_add(COLOR_GOOD, "Done. Restart the Vita to finish.");
	return true;
}

// ---- the screen ----

enum { VIEW_MENU, VIEW_WORKING, VIEW_RESULT };

enum { MENU_INSTALL, MENU_FONTS, MENU_UPDATE, MENU_UNINSTALL, MENU_EXIT };
static const char *const MENU[] = {"Install or update Engram", "Add the fonts", "Check for a new version",
                                   "Uninstall Engram", "Exit"};
static const char *const MENU_HELP[] = {
	"Copies Engram to ur0:tai and adds it to taiHEN's config.txt. A copy of the config is kept first.",
	"On: installing also puts Poppins and Righteous, this app's fonts, in Engram's fonts folder, to choose in its Font settings.",
	"Asks GitHub whether a newer Engram is out. If it is, this installer can download it and update itself.",
	"Takes Engram out of config.txt and deletes the plugin. Your settings and fonts stay.",
	"Leaves without changing anything.",
};
#define MENU_COUNT 5

static const char *const FEATURES[][2] = {
	{"Scroll Animations", "15 effects for the icons as you scroll between pages"},
	{"Motion Depth", "Tilt the Vita and the icons float above the wallpaper"},
	{"Icon Placement", "Open book, sphere and inside-a-sphere layouts"},
	{"App Page Animation", "Effects as you swipe between your open apps"},
	{"Fonts", "Your own fonts for the home screen, lock screen and apps"},
};
#define FEATURE_COUNT 5

// What the screen after "Working..." is showing, which decides its heading and buttons.
typedef enum {
	RESULT_PLUGIN,       // Engram was installed or removed: START restarts the Vita
	RESULT_NOTE,         // something to read
	RESULT_UPDATE_READY, // a newer release: START downloads it
	RESULT_UPDATED,      // this app's files are the new version's: START restarts the app
} ResultKind;
static ResultKind result_kind;

#define PANEL_ALPHA 92
#define LEFT_X 32
#define RIGHT_X 456
#define PANEL_Y 132
#define PANEL_H 316

// The overlay: the title, two glass panels (the menu, and what Engram does or
// what just happened) and the buttons along the bottom. The panels and title
// never change, so they're drawn once and kept.
static void render_ui(int view, int selected, bool installed, bool ok, const char *perf) {
	static bool chrome;
	if (chrome) {
		gfx_restore();
	} else {
		gfx_begin();
		gfx_font(FONT_TITLE);
		gfx_text_shadow(40, 20, 54.0f, COLOR_TEXT, "Engram");
		gfx_font(FONT_TEXT);
		gfx_text_shadow(44, 90, 19.0f, COLOR_DIM, "Home screen tweaks for the PS Vita");
#ifdef UPDATE_TEST_TAG
		const char *version = "Update test  -  Installer " INSTALLER_VERSION "  -  Engram " ENGRAM_VERSION;
#else
		const char *version = "Installer " INSTALLER_VERSION "  -  Engram " ENGRAM_VERSION;
#endif
		gfx_text_shadow(SCREEN_W - 40 - gfx_text_width(16.0f, version), 42, 16.0f, COLOR_DIM, version);
		gfx_panel(LEFT_X, PANEL_Y, 404, PANEL_H, 20, COLOR_PANEL, PANEL_ALPHA);
		gfx_panel(RIGHT_X, PANEL_Y, 472, PANEL_H, 20, COLOR_PANEL, PANEL_ALPHA);
		gfx_panel(LEFT_X, 464, 896, 46, 14, COLOR_PANEL, PANEL_ALPHA);
		gfx_save();
		chrome = true;
	}

	// left: the menu
	gfx_dot(LEFT_X + 30, PANEL_Y + 31, 5, installed ? COLOR_GOOD : COLOR_DIM, 255);
	gfx_text(LEFT_X + 46, PANEL_Y + 19, 16.0f, COLOR_DIM, 255,
	         installed ? "Engram is installed" : "Engram isn't installed yet");
	for (int i = 0; i < MENU_COUNT; i++) {
		int y = PANEL_Y + 54 + i * 37;
		bool on = i == selected && view == VIEW_MENU;
		if (on)
			gfx_panel(LEFT_X + 14, y - 4, 376, 34, 11, COLOR_ACCENT, 64);
		gfx_text(LEFT_X + 32, y, 20.0f, on ? COLOR_TEXT : COLOR_DIM, 255, MENU[i]);
		if (i == MENU_FONTS) { // a switch: its state at the right
			const char *state = add_fonts ? "On" : "Off";
			gfx_text(LEFT_X + 372 - gfx_text_width(20.0f, state), y, 20.0f, add_fonts ? COLOR_ACCENT : COLOR_DIM, 255,
			         state);
		}
	}
	gfx_text_wrap(LEFT_X + 32, PANEL_Y + 246, 344, 15.0f, 20, COLOR_DIM, MENU_HELP[selected]);

	// right: what Engram does, or what just happened
	if (view == VIEW_MENU) {
		gfx_text(RIGHT_X + 24, PANEL_Y + 18, 16.0f, COLOR_ACCENT, 255, "What Engram does");
		for (int i = 0; i < FEATURE_COUNT; i++) {
			int y = PANEL_Y + 50 + i * 46;
			gfx_text(RIGHT_X + 24, y, 19.0f, COLOR_TEXT, 255, FEATURES[i][0]);
			gfx_text(RIGHT_X + 24, y + 24, 13.5f, COLOR_DIM, 255, FEATURES[i][1]);
		}
		gfx_text(RIGHT_X + 24, PANEL_Y + 284, 14.0f, COLOR_ACCENT, 255, "All set in Settings > System > Engram.");
	} else {
		bool working = view == VIEW_WORKING;
		bool offer = !working && result_kind == RESULT_UPDATE_READY;
		gfx_text(RIGHT_X + 24, PANEL_Y + 18, 16.0f, working || offer ? COLOR_ACCENT : ok ? COLOR_GOOD : COLOR_BAD, 255,
		         working ? "Working..." : offer ? "A new version" : ok ? "Done" : "Something went wrong");
		int y = PANEL_Y + 52;
		for (int i = 0; i < log_count && y < PANEL_Y + PANEL_H - 24; i++)
			y = gfx_text_wrap(RIGHT_X + 24, y, 424, 14.0f, 18, log_color[i], log_text[i]) + 4;
	}

	// the buttons
	static const char *const RESULT_BUTTONS[] = {
		"START: restart the Vita now        O or X: back to the menu",
		"O or X: back to the menu",
		"START: download and install it        O or X: back to the menu",
		"START: restart the installer        O or X: back to the menu",
	};
	gfx_text(LEFT_X + 24, 476, 16.0f, COLOR_DIM, 255,
	         view == VIEW_RESULT    ? RESULT_BUTTONS[result_kind]
	         : view == VIEW_WORKING ? "One moment..."
	                                : "Up / Down: choose        O or X: select        START: exit");
	if (perf && perf[0])
		gfx_text(LEFT_X + 896 - 24 - gfx_text_width(16.0f, perf), 476, 16.0f, COLOR_DIM, 255, perf);
	gfx_end();
}

// While the app is busy the main loop isn't drawing, so each line update.c
// has to say is put on screen at once: over the liquid as it was.
static int busy_selected;
static double busy_clock;

static void say(SayKind kind, bool again, const char *fmt, ...) {
	static const uint32_t colors[] = {COLOR_TEXT, COLOR_DIM, COLOR_GOOD, COLOR_BAD};
	char line[128];
	va_list args;
	va_start(args, fmt);
	vsnprintf(line, sizeof(line), fmt, args);
	va_end(args);
	if (again)
		log_drop_last();
	log_add(colors[kind], "%s", line);
	render_ui(VIEW_WORKING, busy_selected, file_exists(PLUGIN_PATH), false, "");
	liquid_frame(gfx_back(), gfx_back_index(), busy_clock, true, gfx_layers());
	gfx_present();
}

static unsigned buttons_pressed(unsigned *last) {
	SceCtrlData pad;
	memset(&pad, 0, sizeof(pad));
	sceCtrlPeekBufferPositive(0, &pad, 1);
	unsigned now = pad.buttons, pressed = now & ~*last;
	*last = now;
	return pressed;
}

int main(void) {
	scePowerSetArmClockFrequency(444); // the liquid is drawn on the CPU
	if (!gfx_init() || !liquid_init()) {
		sceKernelExitProcess(0);
		return 0;
	}
	int view = VIEW_MENU, selected = 0, working_frames = 0, frames = 0;
	bool ok = false, redraw = true, full[2] = {true, true}, perf_on = false, downloading = false;
	UpdateInfo update;
	char perf[24] = "";
	unsigned last = 0;
	SceUInt64 start = sceKernelGetProcessTimeWide(), spent = 0;
	for (;;) {
		unsigned pressed = buttons_pressed(&last);
		if (pressed & SCE_CTRL_SELECT) { // how long a frame of liquid takes
			perf_on = !perf_on;
			perf[0] = '\0';
			frames = 0;
			spent = 0;
			redraw = true;
		}
		if (view == VIEW_MENU) {
			if (pressed & SCE_CTRL_START)
				break;
			if (pressed & SCE_CTRL_UP) {
				selected = (selected + MENU_COUNT - 1) % MENU_COUNT;
				redraw = true;
			}
			if (pressed & SCE_CTRL_DOWN) {
				selected = (selected + 1) % MENU_COUNT;
				redraw = true;
			}
			if (selected == MENU_FONTS &&
			    (pressed & (SCE_CTRL_CROSS | SCE_CTRL_CIRCLE | SCE_CTRL_LEFT | SCE_CTRL_RIGHT))) {
				add_fonts = !add_fonts;
				redraw = true;
			} else if (pressed & (SCE_CTRL_CROSS | SCE_CTRL_CIRCLE)) {
				if (selected == MENU_EXIT)
					break;
				log_clear();
				view = VIEW_WORKING;
				working_frames = 0;
				redraw = true;
			}
		} else if (view == VIEW_WORKING) {
			// Both buffers have shown "Working..." by the third frame.
			if (++working_frames > 2) {
				SceUInt64 before = sceKernelGetProcessTimeWide();
				busy_selected = selected;
				busy_clock = (double)(before - start) / 1000000.0;
				if (downloading) {
					ok = update_install(&update, say);
					result_kind = ok ? RESULT_UPDATED : RESULT_NOTE;
				} else if (selected == MENU_UPDATE) {
					UpdateFound found = update_check(ENGRAM_VERSION, &update, say);
					ok = found != UPDATE_FAILED;
					result_kind = found == UPDATE_READY ? RESULT_UPDATE_READY : RESULT_NOTE;
				} else {
					ok = selected == MENU_INSTALL ? install() : uninstall();
					result_kind = RESULT_PLUGIN;
				}
				downloading = false;
				start += sceKernelGetProcessTimeWide() - before; // the liquid picks up where it was
				view = VIEW_RESULT;
				redraw = true;
			}
		} else {
			if ((pressed & SCE_CTRL_START) && result_kind == RESULT_PLUGIN) {
				scePowerRequestColdReset();
			} else if ((pressed & SCE_CTRL_START) && result_kind == RESULT_UPDATE_READY) {
				log_clear();
				downloading = true;
				view = VIEW_WORKING;
				working_frames = 0;
				redraw = true;
			} else if ((pressed & SCE_CTRL_START) && result_kind == RESULT_UPDATED) {
				// the new eboot.bin, in this app's place
				if (sceAppMgrLoadExec("app0:eboot.bin", NULL, NULL) < 0) {
					log_add(COLOR_TEXT, "Close the installer and open it again to finish.");
					result_kind = RESULT_NOTE;
					redraw = true;
				}
			} else if (pressed & (SCE_CTRL_CROSS | SCE_CTRL_CIRCLE)) {
				view = VIEW_MENU;
				redraw = true;
			}
		}
		if (redraw) {
			render_ui(view, selected, file_exists(PLUGIN_PATH), ok, perf);
			full[0] = full[1] = true;
			redraw = false;
		}
		SceUInt64 before = sceKernelGetProcessTimeWide();
		int buf = gfx_back_index();
		liquid_frame(gfx_back(), buf, (double)(before - start) / 1000000.0, full[buf], gfx_layers());
		full[buf] = false;
		spent += sceKernelGetProcessTimeWide() - before;
		if (perf_on && ++frames >= 30) {
			snprintf(perf, sizeof(perf), "%u.%u ms", (unsigned)(spent / frames / 1000),
			         (unsigned)(spent / frames / 100 % 10));
			frames = 0;
			spent = 0;
			redraw = true;
		}
		gfx_present();
	}
	sceKernelExitProcess(0);
	return 0;
}
