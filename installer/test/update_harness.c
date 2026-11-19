// Runs update.c on the ARM simulator with the network, the file system and
// the system's installer replaced by stand-ins that print what they are asked
// to do. Files written are kept in memory, so the updater's half (update_finish)
// reads what the installer's half (update_install) left for it.
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "files.h"
#include "net.h"
#include "package.h"
#include "sha256.h"
#include "update.h"
#include "cases.h" // made by make_cases.py

static void sys(int op, void *arg) {
	register int r0 __asm__("r0") = op;
	register void *r1 __asm__("r1") = arg;
	__asm__ volatile("svc 0x123456" : "+r"(r0) : "r"(r1) : "memory");
}
static void out(const char *s) { sys(4, (void *)s); }

static uint8_t heap[32 * 1024 * 1024];
static size_t heap_at;
void *malloc(size_t n) { n = (n + 15) & ~(size_t)15; if (heap_at + n > sizeof(heap)) return 0; void *p = heap + heap_at; heap_at += n; return p; }
void free(void *p) { (void)p; }
void *realloc(void *p, size_t n) { (void)p; (void)n; return 0; }
void *memcpy(void *d, const void *s, size_t n) { uint8_t *a = d; const uint8_t *b = s; while (n--) *a++ = *b++; return d; }
void *memset(void *d, int c, size_t n) { uint8_t *a = d; while (n--) *a++ = (uint8_t)c; return d; }
int memcmp(const void *x, const void *y, size_t n) { const uint8_t *a = x, *b = y; for (; n--; a++, b++) if (*a != *b) return *a - *b; return 0; }
size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
int strcmp(const char *a, const char *b) { while (*a && *a == *b) a++, b++; return (uint8_t)*a - (uint8_t)*b; }
int strncmp(const char *a, const char *b, size_t n) { for (; n; n--, a++, b++) { if (*a != *b) return (uint8_t)*a - (uint8_t)*b; if (!*a) break; } return 0; }
char *strchr(const char *s, int c) { for (;; s++) { if (*s == (char)c) return (char *)s; if (!*s) return 0; } }
char *strstr(const char *h, const char *n) { size_t k = strlen(n); for (; *h; h++) if (strncmp(h, n, k) == 0) return (char *)h; return 0; }

// Enough of printf for update.c: %s %.*s %d %u %x %X %c %% with 0 and a width.
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
	size_t at = 0;
#define PUT(ch) do { if (at + 1 < size) buf[at] = (ch); at++; } while (0)
	for (; *fmt; fmt++) {
		if (*fmt != '%') { PUT(*fmt); continue; }
		fmt++;
		bool zero = false; int width = 0, precision = -1;
		if (*fmt == '0') { zero = true; fmt++; }
		while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');
		if (*fmt == '.') { fmt++; if (*fmt == '*') { precision = va_arg(ap, int); fmt++; } else { precision = 0; while (*fmt >= '0' && *fmt <= '9') precision = precision * 10 + (*fmt++ - '0'); } }
		if (*fmt == '%') { PUT('%'); continue; }
		if (*fmt == 'c') { PUT((char)va_arg(ap, int)); continue; }
		if (*fmt == 's') { const char *s = va_arg(ap, const char *); for (int i = 0; s[i] && (precision < 0 || i < precision); i++) PUT(s[i]); continue; }
		char digits[16]; int n = 0; bool negative = false; unsigned v;
		if (*fmt == 'd') { int d = va_arg(ap, int); negative = d < 0; v = negative ? (unsigned)-d : (unsigned)d; } else v = va_arg(ap, unsigned);
		if (*fmt == 'x' || *fmt == 'X') { do { digits[n++] = "0123456789ABCDEF"[v & 15]; v >>= 4; } while (v); }
		else { do { unsigned q = 0, r = v; while (r >= 10) { r -= 10; q++; } digits[n++] = (char)('0' + r); v = q; } while (v); }
		if (negative) PUT('-');
		for (int i = n; i < width; i++) PUT(zero ? '0' : ' ');
		while (n) PUT(digits[--n]);
	}
	if (size) buf[at < size ? at : size - 1] = '\0';
	return (int)at;
}
int snprintf(char *buf, size_t size, const char *fmt, ...) { va_list ap; va_start(ap, fmt); int n = vsnprintf(buf, size, fmt, ap); va_end(ap); return n; }
static void outf(const char *fmt, ...) { char line[400]; va_list ap; va_start(ap, fmt); vsnprintf(line, sizeof(line), fmt, ap); va_end(ap); out(line); }

// ---- stand-ins ----
static const Case *now;
static int writes, installs;
static bool as_updater;

// the files written so far
#define KEPT 96
static struct { char path[200]; const char *data; int len; } kept[KEPT];
static int kept_count;

static int kept_find(const char *path) {
	for (int i = 0; i < kept_count; i++)
		if (strcmp(kept[i].path, path) == 0)
			return i;
	return -1;
}

static bool under(const char *path, const char *dir) {
	size_t n = strlen(dir);
	return strncmp(path, dir, n) == 0 && (path[n] == '/' || path[n] == '\0');
}

bool net_start(char *why, int size) { (void)why; (void)size; return true; }
char *net_get(const char *url, const char *accept, int max, int *len, void (*progress)(int, int), char *why, int size) {
	(void)max; (void)why; (void)size;
	outf("    GET %s%s\n", url, accept ? " (json)" : "");
	const uint8_t *from = accept ? now->json : now->vpk;
	int n = accept ? (int)now->json_len : (int)now->vpk_len;
	char *copy = malloc((size_t)n + 1);
	memcpy(copy, from, (size_t)n);
	copy[n] = '\0';
	if (progress) { progress(n / 3, n); progress(n, n); }
	*len = n;
	return copy;
}

// What the app's own folder holds: stand-ins for its program and pictures,
// and the package header this case gives it.
static const char *own_file(const char *path, int *len) {
	static const char eboot[] = "the installer's eboot.bin", sfo[] = "the updater's param.sfo ENGR00002",
	                  other[] = "a picture or a font";
	if (strcmp(path, "app0:sce_sys/package/head.bin") == 0) {
		*len = (int)now->head_len;
		return now->head_len ? (const char *)now->head : 0;
	}
	if (strcmp(path, "app0:updater.txt") == 0) { *len = 2; return as_updater ? "1\n" : 0; }
	if (strcmp(path, "app0:eboot.bin") == 0) { *len = sizeof(eboot) - 1; return eboot; }
	if (strcmp(path, "app0:updater/param.sfo") == 0) { *len = sizeof(sfo) - 1; return sfo; }
	if (strncmp(path, "app0:", 5) == 0) { *len = sizeof(other) - 1; return other; }
	return 0;
}

bool file_exists(const char *path) {
	int len;
	if (strcmp(path, "ux0:app/ENGR00002/eboot.bin") == 0)
		return false;
	return own_file(path, &len) || kept_find(path) >= 0;
}

char *file_read(const char *path, int max, int *len) {
	int n = 0, at = kept_find(path);
	const char *from = at >= 0 ? kept[at].data : own_file(path, &n);
	if (at >= 0)
		n = kept[at].len;
	if (!from || n > max)
		return 0;
	char *copy = malloc((size_t)n + 1);
	memcpy(copy, from, (size_t)n);
	copy[n] = '\0';
	*len = n;
	return copy;
}

bool file_write_checked(const char *path, const char *buf, int len) {
	char hex[65];
	sha256_hex((const uint8_t *)buf, (uint32_t)len, hex);
	writes++;
	bool ok = writes != now->fail_write_at;
	outf("    write %s %d %s%s\n", path, len, hex, ok ? "" : " -> FAILS");
	if (!ok)
		return false;
	int at = kept_find(path);
	if (at < 0 && kept_count < KEPT)
		at = kept_count++;
	if (at < 0)
		return false;
	char *copy = malloc((size_t)len + 1);
	memcpy(copy, buf, (size_t)len);
	snprintf(kept[at].path, sizeof(kept[at].path), "%s", path);
	kept[at].data = copy;
	kept[at].len = len;
	return true;
}

bool file_copy(const char *from, const char *to, int max) {
	int len = 0;
	char *buf = file_read(from, max, &len);
	return buf && file_write_checked(to, buf, len);
}

void file_remove_tree(const char *path) {
	int gone = 0;
	for (int i = 0; i < kept_count;)
		if (under(kept[i].path, path)) {
			kept[i] = kept[--kept_count];
			gone++;
		} else {
			i++;
		}
	outf("    remove all of %s (%d files)\n", path, gone);
}

int sceIoMkdir(const char *dir, SceMode mode) { (void)dir; (void)mode; return 0; }

bool package_start(int *error) { (void)error; return true; }
int package_install(const char *dir) {
	installs++;
	bool ok = installs != now->fail_install_at;
	int files = 0;
	for (int i = 0; i < kept_count; i++)
		files += under(kept[i].path, dir);
	outf("    SYSTEM INSTALL of %s (%d files)%s\n", dir, files, ok ? "" : " -> FAILS");
	char head[220];
	snprintf(head, sizeof(head), "%s/sce_sys/package/head.bin", dir);
	int at = kept_find(head);
	if (at >= 0)
		outf("      its header is for %.9s\n", kept[at].data + 0x37);
	if (!ok)
		return (int)0x80870004;
	for (int i = 0; i < kept_count;) // the folder is taken
		if (under(kept[i].path, dir))
			kept[i] = kept[--kept_count];
		else
			i++;
	return 0;
}
bool package_installed(const char *titleid) { (void)titleid; return false; }
int package_remove(const char *titleid) { outf("    SYSTEM DELETE of %s\n", titleid); return 0; }
void package_launch(const char *titleid) { outf("    OPEN %s\n", titleid); }

static void say(SayKind kind, bool again, const char *fmt, ...) {
	char line[200]; va_list ap; va_start(ap, fmt); vsnprintf(line, sizeof(line), fmt, ap); va_end(ap);
	outf("  %s%s %s\n", again ? "~" : " ", kind == SAY_GOOD ? "[good]" : kind == SAY_BAD ? "[BAD] " : kind == SAY_DIM ? "[dim] " : "[text]", line);
}

extern uint8_t __bss_start__[], __bss_end__[];
void run(void) {
	for (uint8_t *p = __bss_start__; p < __bss_end__; p++) *p = 0;
	for (int i = 0; i < CASE_COUNT; i++) {
		now = &cases[i];
		heap_at = 0; writes = 0; installs = 0; kept_count = 0; as_updater = false;
		outf("CASE %s (installer has %s)\n", now->name, now->have);
		UpdateInfo info;
		UpdateFound found = update_check(now->have, &info, say);
		outf("  => %s\n", found == UPDATE_READY ? "READY" : found == UPDATE_NONE ? "NONE" : "FAILED");
		if (found != UPDATE_READY)
			continue;
		outf("  url %s\n  size %d digest %s\n", info.url, info.size, info.digest);
		bool ok = update_install(&info, say);
		outf("  => updater %s\n", ok ? "READY" : "NOT INSTALLED");
		if (!ok)
			continue;
		update_open_updater();
		// the updater's turn
		as_updater = true;
		if (now->tamper) { // the kept VPK is changed behind its back
			int at = kept_find("ux0:data/engram_installer.vpk");
			((char *)kept[at].data)[kept[at].len / 2] ^= 0x10;
			outf("  (the kept VPK is changed)\n");
		}
		outf("  as the updater: %s\n", update_is_updater() ? "yes" : "NO");
		ok = update_finish(say);
		outf("  => install %s\n", ok ? "OK" : "REFUSED");
		if (ok)
			update_open_installer();
		outf("  files left behind: %d\n", kept_count);
		for (int k = 0; k < kept_count; k++)
			outf("    left %s\n", kept[k].path);
	}
	sys(0x18, (void *)0x20026);
}
// (8-byte aligned, as the ARM calling rules want of a stack: 64-bit values are loaded in pairs)
uint8_t stack[512 * 1024] __attribute__((aligned(8)));
__asm__(".text\n.global _start\n_start:\nldr sp, =stack_top\nbl run\nb .\n.ltorg\n.equ stack_top, stack + 524288\n");
// the compiler's helper for dividing, which this bare test has no library for
int __aeabi_idiv(int a, int b) { bool neg = (a < 0) != (b < 0); unsigned x = a < 0 ? (unsigned)-a : (unsigned)a, y = b < 0 ? (unsigned)-b : (unsigned)b, q = 0; for (int i = 31; i >= 0; i--) if ((x >> i) >= y) { x -= y << i; q |= 1u << i; } return neg ? -(int)q : (int)q; }
