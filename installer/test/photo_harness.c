// Runs png.c and lookup.c on the ARM simulator: PNGs of every kind the reader
// takes (and some it must refuse), fitted into the photo's frame, and GitHub's
// real answers about the two projects. Prints what it gets for
// make_photo_case.py --check to compare with Python's.
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "lookup.h"
#include "png.h"
#include "sha256.h"
#include "photo_case.h" // made by make_photo_case.py

static void sys(int op, void *arg) {
	register int r0 __asm__("r0") = op;
	register void *r1 __asm__("r1") = arg;
	__asm__ volatile("svc 0x123456" : "+r"(r0) : "r"(r1) : "memory");
}
static void out(const char *s) { sys(4, (void *)s); }

static uint8_t heap[40 * 1024 * 1024];
static size_t heap_at;
void *malloc(size_t n) { n = (n + 15) & ~(size_t)15; if (heap_at + n > sizeof(heap)) return 0; void *p = heap + heap_at; heap_at += n; return p; }
void free(void *p) { (void)p; }
void *memcpy(void *d, const void *s, size_t n) { uint8_t *a = d; const uint8_t *b = s; while (n--) *a++ = *b++; return d; }
void *memset(void *d, int c, size_t n) { uint8_t *a = d; while (n--) *a++ = (uint8_t)c; return d; }
int memcmp(const void *x, const void *y, size_t n) { const uint8_t *a = x, *b = y; for (; n--; a++, b++) if (*a != *b) return *a - *b; return 0; }
size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
int strcmp(const char *a, const char *b) { while (*a && *a == *b) a++, b++; return (uint8_t)*a - (uint8_t)*b; }
int strncmp(const char *a, const char *b, size_t n) { for (; n; n--, a++, b++) { if (*a != *b) return (uint8_t)*a - (uint8_t)*b; if (!*a) break; } return 0; }
char *strchr(const char *s, int c) { for (;; s++) { if (*s == (char)c) return (char *)s; if (!*s) return 0; } }
char *strstr(const char *h, const char *n) { size_t k = strlen(n); for (; *h; h++) if (strncmp(h, n, k) == 0) return (char *)h; return 0; }

// Enough of printf: %s %.4s %d %u %c %% with a width.
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
	size_t at = 0;
#define PUT(ch) do { if (at + 1 < size) buf[at] = (ch); at++; } while (0)
	for (; *fmt; fmt++) {
		if (*fmt != '%') { PUT(*fmt); continue; }
		fmt++;
		int precision = -1;
		if (*fmt == '.') { fmt++; precision = 0; while (*fmt >= '0' && *fmt <= '9') precision = precision * 10 + (*fmt++ - '0'); }
		if (*fmt == '%') { PUT('%'); continue; }
		if (*fmt == 'c') { PUT((char)va_arg(ap, int)); continue; }
		if (*fmt == 's') { const char *s = va_arg(ap, const char *); for (int i = 0; s[i] && (precision < 0 || i < precision); i++) PUT(s[i]); continue; }
		char digits[16]; int n = 0; bool negative = false; unsigned v;
		if (*fmt == 'd') { int d = va_arg(ap, int); negative = d < 0; v = negative ? (unsigned)-d : (unsigned)d; } else v = va_arg(ap, unsigned);
		do { digits[n++] = (char)('0' + v % 10); v /= 10; } while (v);
		if (negative) PUT('-');
		while (n) PUT(digits[--n]);
	}
	if (size) buf[at < size ? at : size - 1] = '\0';
	return (int)at;
}
int snprintf(char *buf, size_t size, const char *fmt, ...) { va_list ap; va_start(ap, fmt); int n = vsnprintf(buf, size, fmt, ap); va_end(ap); return n; }
static void outf(const char *fmt, ...) { char line[600]; va_list ap; va_start(ap, fmt); vsnprintf(line, sizeof(line), fmt, ap); va_end(ap); out(line); }

extern uint8_t __bss_start__[], __bss_end__[];
void run(void) {
	for (uint8_t *p = __bss_start__; p < __bss_end__; p++) *p = 0;
	char hex[65];
	for (int i = 0; i < PNG_COUNT; i++) {
		heap_at = 0;
		int w = 0, h = 0;
		uint8_t *rgb = png_decode(pngs[i].data, pngs[i].len, 4 * 1024 * 1024, &w, &h);
		if (!rgb) {
			outf("PNG %s REFUSED\n", pngs[i].name);
			continue;
		}
		sha256_hex(rgb, (uint32_t)(w * h * 3), hex);
		outf("PNG %s %d %d %s\n", pngs[i].name, w, h, hex);
		uint8_t *photo = photo_fit(rgb, w, h, 448, 254, 0x2C1C18);
		sha256_hex(photo, 448 * 254 * 3, hex);
		outf("FIT %s %s\n", pngs[i].name, hex);
	}
	for (int i = 0; i < ANSWER_COUNT; i++) {
		static Shot shots[8];
		const Answer *a = &answers[i];
		if (a->kind == 0) {
			int n = lookup_shots(a->text, a->repo, shots, 8);
			outf("SHOTS %s %d\n", a->name, n);
			for (int k = 0; k < n; k++)
				outf("  shot %s %d %s\n", shots[k].sha, shots[k].size, shots[k].url);
		} else if (a->kind == 2) {
			static Release releases[8];
			int count = 0;
			while (a->assets[count])
				count++;
			int n = lookup_releases(a->text, a->repo, a->assets, count, releases, 8);
			outf("RELEASES %s %d\n", a->name, n);
			for (int k = 0; k < n; k++) {
				const Release *r = &releases[k];
				sha256_hex((const uint8_t *)r->notes, (uint32_t)strlen(r->notes), hex);
				outf("  rel %s|%s|%s|%s|%s|%s|%d|%s\n", r->tag, r->name, r->date, r->prerelease ? "pre" : "full", r->size[0],
				     r->size[1], (int)strlen(r->notes), hex);
			}
		} else {
			char name[64], date[16], size[12];
			bool ok = lookup_release(a->text, name, sizeof(name), date);
			outf("RELEASE %s %s|%s|%s\n", a->name, ok ? "ok" : "REFUSED", ok ? name : "", ok ? date : "");
			for (int k = 0; a->assets[k]; k++) {
				ReleaseFile file;
				if (lookup_asset(a->text, a->repo, a->assets[k], &file)) {
					lookup_size_text(file.size, size);
					outf("  asset %s %s %d %s %s\n", a->assets[k], file.name, file.size, size, file.digest);
				} else {
					outf("  asset %s NONE\n", a->assets[k]);
				}
			}
		}
	}
	static const int sizes[] = {1, 1023, 1024, 18388, 1048575, 1048576, 15340487, 104857599, 104857600, 442193162};
	for (int i = 0; i < 10; i++) {
		char text[12];
		lookup_size_text(sizes[i], text);
		outf("SIZE %d %s\n", sizes[i], text);
	}
	sys(0x18, (void *)0x20026);
}
// the compiler's helpers for dividing, which this bare test has no library for
// (quotient and remainder come back together, as the two halves of 64 bits)
unsigned long long __aeabi_uidivmod(unsigned n, unsigned d) {
	unsigned q = 0;
	for (int i = 31; i >= 0; i--)
		if ((n >> i) >= d) {
			n -= d << i;
			q |= 1u << i;
		}
	return ((unsigned long long)n << 32) | q;
}
unsigned __aeabi_uidiv(unsigned n, unsigned d) { return (unsigned)__aeabi_uidivmod(n, d); }
long long __aeabi_idivmod(int n, int d) {
	unsigned long long r = __aeabi_uidivmod(n < 0 ? (unsigned)-n : (unsigned)n, d < 0 ? (unsigned)-d : (unsigned)d);
	int q = (int)(unsigned)r, rem = (int)(unsigned)(r >> 32);
	if ((n < 0) != (d < 0))
		q = -q;
	if (n < 0)
		rem = -rem;
	return (long long)(((unsigned long long)(unsigned)rem << 32) | (unsigned)q);
}
int __aeabi_idiv(int n, int d) { return (int)__aeabi_idivmod(n, d); }

// (8-byte aligned, as the ARM calling rules want of a stack: 64-bit values are loaded in pairs)
uint8_t stack[512 * 1024] __attribute__((aligned(8)));
__asm__(".text\n.global _start\n_start:\nldr sp, =stack_top\nbl run\nb .\n.ltorg\n.equ stack_top, stack + 524288\n");
