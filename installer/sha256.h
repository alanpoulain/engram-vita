// SHA-256 (FIPS 180-4), for checking a download against the digest GitHub
// gives for it.
#ifndef SHA256_H
#define SHA256_H

#include <stdint.h>

// The digest of data as 64 lower-case hex digits and a NUL.
void sha256_hex(const uint8_t *data, uint32_t len, char hex[65]);

#endif
