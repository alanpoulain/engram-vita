// Runs update.c on the ARM simulator with the network and the file system
// replaced by stand-ins that print what they are asked to do.
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "files.h"
#include "net.h"
#include "sha256.h"
#include "update.h"
#include "cases.h" // made by make_cases.py

static void sys(int op, void *arg) {
	register int r0 __asm__("r0") = op;
	register void *r1 __asm__("r1") = arg;
	__asm__ volatile("svc 0x123456" : "+r"(r0) : "r"(r1) : "memory");
}
static void out(const char *s) { sys(4, (void *)s); }

static uint8_t heap[8 * 1024 * 1024];
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
static int fail_write_at, writes;

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
bool file_exists(const char *path) { (void)path; return true; }
bool file_write_checked(const char *path, const char *buf, int len) {
	char hex[65];
	sha256_hex((const uint8_t *)buf, (uint32_t)len, hex);
	writes++;
	bool ok = writes != fail_write_at;
	outf("    write %s %d %s%s\n", path, len, hex, ok ? "" : " -> FAILS");
	return ok;
}
int sceIoMkdir(const char *dir, SceMode mode) { (void)mode; outf("    mkdir %s\n", dir); return 0; }
int sceIoRemove(const char *path) { outf("    remove %s\n", path); return 0; }
int sceIoRename(const char *from, const char *to) { outf("    rename %s -> %s\n", from, to); return 0; }

static void say(SayKind kind, bool again, const char *fmt, ...) {
	char line[200]; va_list ap; va_start(ap, fmt); vsnprintf(line, sizeof(line), fmt, ap); va_end(ap);
	outf("  %s%s %s\n", again ? "~" : " ", kind == SAY_GOOD ? "[good]" : kind == SAY_BAD ? "[BAD] " : kind == SAY_DIM ? "[dim] " : "[text]", line);
}

extern uint8_t __bss_start__[], __bss_end__[];
void run(void) {
	for (uint8_t *p = __bss_start__; p < __bss_end__; p++) *p = 0;
	for (int i = 0; i < CASE_COUNT; i++) {
		now = &cases[i];
		heap_at = 0; writes = 0; fail_write_at = now->fail_write_at;
		outf("CASE %s (installer has %s)\n", now->name, now->have);
		UpdateInfo info;
		UpdateFound found = update_check(now->have, &info, say);
		outf("  => %s\n", found == UPDATE_READY ? "READY" : found == UPDATE_NONE ? "NONE" : "FAILED");
		if (found == UPDATE_READY) {
			outf("  url %s\n  size %d digest %s\n", info.url, info.size, info.digest);
			bool ok = update_install(&info, say);
			outf("  => install %s\n", ok ? "OK" : "REFUSED");
		}
	}
	sys(0x18, (void *)0x20026);
}
uint8_t stack[512 * 1024];
__asm__(".text\n.global _start\n_start:\nldr sp, =stack_top\nbl run\nb .\n.equ stack_top, stack + 524288\n");
// the compiler's helper for dividing, which this bare test has no library for
int __aeabi_idiv(int a, int b) { bool neg = (a < 0) != (b < 0); unsigned x = a < 0 ? (unsigned)-a : (unsigned)a, y = b < 0 ? (unsigned)-b : (unsigned)b, q = 0; for (int i = 31; i >= 0; i--) if ((x >> i) >= y) { x -= y << i; q |= 1u << i; } return neg ? -(int)q : (int)q; }
