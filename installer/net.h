// HTTPS with the Vita's own libraries (SceHttp, SceSsl), for asking GitHub
// about new versions. The server's certificate is always checked, against the
// Vita's authorities and the ones in ca_certs.h; a server that fails the check
// gets no request and nothing it sends is read.
#ifndef NET_H
#define NET_H

#include <stdbool.h>

// Starts the network libraries, once. False with the reason in why (the Vita
// being offline, say).
bool net_start(char *why, int why_size);

// GETs url into memory: malloc'd and NUL-terminated, *len its size. accept is
// the Accept header, or NULL. progress, if given, hears how much has arrived
// (total is 0 when the server didn't say). NULL, with the reason in why, if
// the answer isn't a whole 200 of at most max bytes.
char *net_get(const char *url, const char *accept, int max, int *len, void (*progress)(int got, int total), char *why,
              int why_size);

#endif
