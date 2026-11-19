#include "unzip.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static inline uint32_t u16(const uint8_t *p) {
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}

static inline uint32_t u32(const uint8_t *p) {
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

uint32_t zip_crc32(const uint8_t *data, uint32_t len) {
	static uint32_t table[256];
	if (!table[1])
		for (uint32_t i = 0; i < 256; i++) {
			uint32_t c = i;
			for (int k = 0; k < 8; k++)
				c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
			table[i] = c;
		}
	uint32_t crc = 0xFFFFFFFFu;
	for (uint32_t i = 0; i < len; i++)
		crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
	return crc ^ 0xFFFFFFFFu;
}

// ---- the list of files ----

#define END_SIG 0x06054B50u
#define CENTRAL_SIG 0x02014B50u
#define LOCAL_SIG 0x04034B50u
#define END_SIZE 22
#define CENTRAL_SIZE 46
#define LOCAL_SIZE 30

int zip_entries(const uint8_t *zip, uint32_t len, ZipEntry *out, int max) {
	if (len < END_SIZE)
		return -1;
	// The end record is the last thing in the file, but for a comment of up to 64 KB.
	uint32_t end = len - END_SIZE, stop = len > END_SIZE + 0xFFFF ? len - END_SIZE - 0xFFFF : 0;
	while (u32(zip + end) != END_SIG) {
		if (end == stop)
			return -1;
		end--;
	}
	uint32_t count = u16(zip + end + 10), dir_size = u32(zip + end + 12), dir = u32(zip + end + 16);
	if (u16(zip + end + 4) || u16(zip + end + 6) || count != u16(zip + end + 8))
		return -1; // in several parts
	if (count == 0xFFFF || dir == 0xFFFFFFFFu || dir > end || dir_size > end - dir)
		return -1; // zip64, or a directory that isn't where it says
	int n = 0;
	uint32_t at = dir;
	for (uint32_t i = 0; i < count; i++) {
		if (at > end || end - at < CENTRAL_SIZE || u32(zip + at) != CENTRAL_SIG)
			return -1;
		uint32_t flags = u16(zip + at + 8), name_len = u16(zip + at + 28);
		uint32_t skip = name_len + u16(zip + at + 30) + u16(zip + at + 32);
		if (end - at - CENTRAL_SIZE < skip)
			return -1;
		if (flags & 1)
			return -1; // encrypted
		if (n < max) {
			ZipEntry *e = &out[n];
			e->name = (const char *)zip + at + CENTRAL_SIZE;
			e->name_len = (int)name_len;
			e->method = (int)u16(zip + at + 10);
			e->crc = u32(zip + at + 16);
			e->packed = u32(zip + at + 20);
			e->size = u32(zip + at + 24);
			e->offset = u32(zip + at + 42);
			if (e->packed == 0xFFFFFFFFu || e->size == 0xFFFFFFFFu || e->offset == 0xFFFFFFFFu)
				return -1; // zip64
			n++;
		}
		at += CENTRAL_SIZE + skip;
	}
	return n;
}

// ---- inflate: RFC 1951 ----
// Codes are read a bit at a time against counts of codes per length, which is
// slow but small, and a VPK's files are small.

#define MAX_BITS 15
#define MAX_LITERALS 286
#define MAX_DISTANCES 30

typedef struct {
	const uint8_t *in;
	uint32_t in_len, in_pos;
	uint32_t bit_buf;
	int bit_count;
	uint8_t *out;
	uint32_t out_len, out_pos;
	bool bad; // ran out of input, or the data makes no sense
} Inflate;

typedef struct {
	uint16_t count[MAX_BITS + 1]; // codes of each length
	uint16_t symbol[MAX_LITERALS + 2]; // symbols in order of code
} Huffman;

static uint32_t bits(Inflate *s, int need) {
	uint32_t value = s->bit_buf;
	while (s->bit_count < need) {
		if (s->in_pos >= s->in_len) {
			s->bad = true;
			return 0;
		}
		value |= (uint32_t)s->in[s->in_pos++] << s->bit_count;
		s->bit_count += 8;
	}
	s->bit_buf = value >> need;
	s->bit_count -= need;
	return value & ((1u << need) - 1u);
}

static int decode(Inflate *s, const Huffman *h) {
	int code = 0, first = 0, index = 0;
	for (int len = 1; len <= MAX_BITS; len++) {
		code |= (int)bits(s, 1);
		if (s->bad)
			return -1;
		int count = h->count[len];
		if (code - count < first)
			return h->symbol[index + (code - first)];
		index += count;
		first += count;
		first <<= 1;
		code <<= 1;
	}
	s->bad = true;
	return -1;
}

// Builds the table from each symbol's code length. Returns 0 for a complete
// set of codes, more for an incomplete one, less for one that can't be.
static int construct(Huffman *h, const uint8_t *length, int n) {
	uint16_t offset[MAX_BITS + 1];
	for (int len = 0; len <= MAX_BITS; len++)
		h->count[len] = 0;
	for (int sym = 0; sym < n; sym++)
		h->count[length[sym]]++;
	if (h->count[0] == n)
		return 0; // no codes at all
	int left = 1;
	for (int len = 1; len <= MAX_BITS; len++) {
		left <<= 1;
		left -= h->count[len];
		if (left < 0)
			return left;
	}
	offset[1] = 0;
	for (int len = 1; len < MAX_BITS; len++)
		offset[len + 1] = (uint16_t)(offset[len] + h->count[len]);
	for (int sym = 0; sym < n; sym++)
		if (length[sym])
			h->symbol[offset[length[sym]]++] = (uint16_t)sym;
	return left;
}

static void codes(Inflate *s, const Huffman *len_code, const Huffman *dist_code) {
	static const uint16_t len_base[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
	                                      31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
	static const uint8_t len_extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
	                                      2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
	static const uint16_t dist_base[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,
	                                       33,  49,  65,  97,  129, 193,  257,  385,  513,  769,
	                                       1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
	static const uint8_t dist_extra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
	                                       6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
	for (;;) {
		int sym = decode(s, len_code);
		if (s->bad)
			return;
		if (sym < 256) {
			if (s->out_pos >= s->out_len) {
				s->bad = true;
				return;
			}
			s->out[s->out_pos++] = (uint8_t)sym;
			continue;
		}
		if (sym == 256)
			return; // the end of the block
		sym -= 257;
		if (sym >= 29) {
			s->bad = true;
			return;
		}
		uint32_t len = len_base[sym] + bits(s, len_extra[sym]);
		sym = decode(s, dist_code);
		if (s->bad)
			return;
		if (sym >= 30) {
			s->bad = true;
			return;
		}
		uint32_t dist = dist_base[sym] + bits(s, dist_extra[sym]);
		if (s->bad || dist > s->out_pos || len > s->out_len - s->out_pos) {
			s->bad = true;
			return;
		}
		// a byte at a time: the copy may run into what it is writing
		for (uint32_t i = 0; i < len; i++, s->out_pos++)
			s->out[s->out_pos] = s->out[s->out_pos - dist];
	}
}

static void fixed_block(Inflate *s) {
	static Huffman len_code, dist_code;
	static bool built;
	if (!built) {
		uint8_t length[288];
		int sym = 0;
		for (; sym < 144; sym++)
			length[sym] = 8;
		for (; sym < 256; sym++)
			length[sym] = 9;
		for (; sym < 280; sym++)
			length[sym] = 7;
		for (; sym < 288; sym++)
			length[sym] = 8;
		construct(&len_code, length, 288);
		for (sym = 0; sym < MAX_DISTANCES; sym++)
			length[sym] = 5;
		construct(&dist_code, length, MAX_DISTANCES);
		built = true;
	}
	codes(s, &len_code, &dist_code);
}

static void dynamic_block(Inflate *s) {
	static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
	uint8_t length[MAX_LITERALS + MAX_DISTANCES];
	Huffman len_code, dist_code;
	int n_len = (int)bits(s, 5) + 257, n_dist = (int)bits(s, 5) + 1, n_code = (int)bits(s, 4) + 4;
	if (s->bad || n_len > MAX_LITERALS || n_dist > MAX_DISTANCES) {
		s->bad = true;
		return;
	}
	// the code that the code lengths themselves are written in
	int i = 0;
	for (; i < n_code; i++)
		length[order[i]] = (uint8_t)bits(s, 3);
	for (; i < 19; i++)
		length[order[i]] = 0;
	if (s->bad || construct(&len_code, length, 19) != 0) {
		s->bad = true;
		return;
	}
	i = 0;
	while (i < n_len + n_dist) {
		int sym = decode(s, &len_code);
		if (s->bad)
			return;
		if (sym < 16) {
			length[i++] = (uint8_t)sym;
			continue;
		}
		uint8_t value = 0;
		int repeat;
		if (sym == 16) {
			if (i == 0) {
				s->bad = true;
				return;
			}
			value = length[i - 1];
			repeat = 3 + (int)bits(s, 2);
		} else if (sym == 17) {
			repeat = 3 + (int)bits(s, 3);
		} else {
			repeat = 11 + (int)bits(s, 7);
		}
		if (s->bad || i + repeat > n_len + n_dist) {
			s->bad = true;
			return;
		}
		while (repeat--)
			length[i++] = value;
	}
	if (length[256] == 0) { // no way to end the block
		s->bad = true;
		return;
	}
	// An incomplete set is only allowed when it is a single code.
	int left = construct(&len_code, length, n_len);
	if (left < 0 || (left > 0 && n_len - len_code.count[0] != 1)) {
		s->bad = true;
		return;
	}
	left = construct(&dist_code, length + n_len, n_dist);
	if (left < 0 || (left > 0 && n_dist - dist_code.count[0] != 1)) {
		s->bad = true;
		return;
	}
	codes(s, &len_code, &dist_code);
}

static void stored_block(Inflate *s) {
	s->bit_buf = 0; // what's left of the byte isn't used
	s->bit_count = 0;
	if (s->in_len - s->in_pos < 4) {
		s->bad = true;
		return;
	}
	uint32_t len = u16(s->in + s->in_pos), check = u16(s->in + s->in_pos + 2);
	s->in_pos += 4;
	if (len != (~check & 0xFFFFu) || len > s->in_len - s->in_pos || len > s->out_len - s->out_pos) {
		s->bad = true;
		return;
	}
	memcpy(s->out + s->out_pos, s->in + s->in_pos, len);
	s->in_pos += len;
	s->out_pos += len;
}

// Unpacks in to exactly out_len bytes of out.
static bool inflate(const uint8_t *in, uint32_t in_len, uint8_t *out, uint32_t out_len) {
	Inflate s;
	memset(&s, 0, sizeof(s));
	s.in = in;
	s.in_len = in_len;
	s.out = out;
	s.out_len = out_len;
	uint32_t last;
	do {
		last = bits(&s, 1);
		uint32_t type = bits(&s, 2);
		if (s.bad)
			return false;
		if (type == 0)
			stored_block(&s);
		else if (type == 1)
			fixed_block(&s);
		else if (type == 2)
			dynamic_block(&s);
		else
			s.bad = true;
		if (s.bad)
			return false;
	} while (!last);
	return s.out_pos == out_len;
}

uint8_t *zip_extract(const uint8_t *zip, uint32_t len, const ZipEntry *e) {
	if (e->offset > len || len - e->offset < LOCAL_SIZE || u32(zip + e->offset) != LOCAL_SIG)
		return NULL;
	uint32_t skip = LOCAL_SIZE + u16(zip + e->offset + 26) + u16(zip + e->offset + 28);
	if (len - e->offset < skip || len - e->offset - skip < e->packed)
		return NULL;
	const uint8_t *data = zip + e->offset + skip;
	uint8_t *out = malloc((size_t)e->size + 1);
	if (!out)
		return NULL;
	bool ok;
	if (e->method == 0) {
		ok = e->packed == e->size;
		if (ok)
			memcpy(out, data, e->size);
	} else {
		ok = e->method == 8 && inflate(data, e->packed, out, e->size);
	}
	if (!ok || zip_crc32(out, e->size) != e->crc) {
		free(out);
		return NULL;
	}
	return out;
}
