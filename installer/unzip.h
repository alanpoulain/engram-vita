// Reads a zip (a VPK is one) held whole in memory: its list of files, and each
// file's contents, stored or deflated, checked against the CRC the zip gives.
#ifndef UNZIP_H
#define UNZIP_H

#include <stdint.h>

typedef struct {
	const char *name; // inside the zip: not NUL-terminated
	int name_len;
	uint32_t crc, packed, size;
	uint32_t offset; // of its local header
	int method;      // 0 stored, 8 deflated
} ZipEntry;

// The zip's files, up to max of them. Returns how many, or -1 if this isn't a
// zip this code reads (zip64, encrypted, split or damaged).
int zip_entries(const uint8_t *zip, uint32_t len, ZipEntry *out, int max);

// One file's contents: malloc'd, entry->size bytes (a byte more is allocated,
// so an empty file isn't NULL). NULL if it doesn't unpack to its size and CRC.
uint8_t *zip_extract(const uint8_t *zip, uint32_t len, const ZipEntry *entry);

uint32_t zip_crc32(const uint8_t *data, uint32_t len);

#endif
