// Runs unzip.c and sha256.c on the ARM simulator over a zip made by
// make_zip_case.py, both ways: held in memory, and read and unpacked a piece
// at a time as a zip too big for memory is. Prints each file's size and
// SHA-256 for make_zip_case.py --check to compare with Python's.
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "sha256.h"
#include "unzip.h"
#include "zip_case.h" // made by make_zip_case.py

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
void *memcpy(void *d, const void *s, size_t n) { uint8_t *a = d; const uint8_t *b = s; while (n--) *a++ = *b++; return d; }
void *memset(void *d, int c, size_t n) { uint8_t *a = d; while (n--) *a++ = (uint8_t)c; return d; }
int memcmp(const void *x, const void *y, size_t n) { const uint8_t *a = x, *b = y; for (; n--; a++, b++) if (*a != *b) return *a - *b; return 0; }

static void put_number(unsigned v) {
	char digits[12];
	int n = 0;
	do {
		unsigned q = 0, r = v;
		while (r >= 10) { r -= 10; q++; }
		digits[n++] = (char)('0' + r);
		v = q;
	} while (v);
	char text[13];
	int at = 0;
	while (n) text[at++] = digits[--n];
	text[at] = '\0';
	out(text);
}

static void put_name(const ZipEntry *e) {
	char name[200];
	int n = e->name_len < 199 ? e->name_len : 199;
	memcpy(name, e->name, (size_t)n);
	name[n] = '\0';
	out(name);
}

// the zip as a file would be: read a piece at a time, some reads cut short on purpose
typedef struct { const uint8_t *data; uint32_t len; } Source;
static int read_source(void *from, uint32_t offset, uint8_t *buf, uint32_t len) {
	Source *s = from;
	if (offset > s->len || len > s->len - offset)
		return -1;
	memcpy(buf, s->data + offset, len);
	return (int)len;
}

typedef struct { Sha256 sha; uint32_t total; uint32_t pieces; } Sink;
static bool write_sink(void *to, const uint8_t *buf, uint32_t len) {
	Sink *s = to;
	sha256_add(&s->sha, buf, len);
	s->total += len;
	s->pieces++;
	return true;
}

static ZipEntry entries[64];

static void run_zip(const char *label, const uint8_t *zip, uint32_t len) {
	char hex[65];
	out("ZIP "); out(label); out("\n");
	// held in memory
	int count = zip_entries(zip, len, entries, 64);
	out("  memory entries "); if (count < 0) out("-1"); else put_number((unsigned)count); out("\n");
	for (int i = 0; i < count; i++) {
		uint8_t *data = zip_extract(zip, len, &entries[i]);
		out("  memory "); put_name(&entries[i]); out(" ");
		if (data) {
			sha256_hex(data, entries[i].size, hex);
			put_number(entries[i].size); out(" "); out(hex);
		} else {
			out("REFUSED");
		}
		out("\n");
	}
	// a piece at a time
	Source source = {zip, len};
	uint8_t *names = 0;
	count = zip_file_entries(read_source, &source, len, entries, 64, &names);
	out("  stream entries "); if (count < 0) out("-1"); else put_number((unsigned)count); out("\n");
	for (int i = 0; i < count; i++) {
		Sink sink;
		sha256_begin(&sink.sha);
		sink.total = sink.pieces = 0;
		bool ok = zip_file_extract(read_source, &source, len, &entries[i], write_sink, &sink);
		out("  stream "); put_name(&entries[i]); out(" ");
		if (ok) {
			sha256_end(&sink.sha, hex);
			put_number(sink.total); out(" "); out(hex);
		} else {
			out("REFUSED");
		}
		out("\n");
	}
}

extern uint8_t __bss_start__[], __bss_end__[];
void run(void) {
	for (uint8_t *p = __bss_start__; p < __bss_end__; p++) *p = 0;
	run_zip("sound", zip_sound, sizeof(zip_sound));
	heap_at = 0;
	run_zip("damaged", zip_damaged, sizeof(zip_damaged));
	heap_at = 0;
	run_zip("cut", zip_sound, sizeof(zip_sound) - 100);
	sys(0x18, (void *)0x20026);
}
// (8-byte aligned, as the ARM calling rules want of a stack: 64-bit values are loaded in pairs)
uint8_t stack[512 * 1024] __attribute__((aligned(8)));
__asm__(".text\n.global _start\n_start:\nldr sp, =stack_top\nbl run\nb .\n.ltorg\n.equ stack_top, stack + 524288\n");
