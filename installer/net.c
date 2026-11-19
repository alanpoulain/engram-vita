#include "net.h"

#include <psp2/libssl.h>
#include <psp2/net/http.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/sysmodule.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ca_certs.h"

#define USER_AGENT "EngramInstaller"
#define NET_POOL (1024 * 1024)
#define SSL_POOL (1024 * 1024)
#define HTTP_POOL (1024 * 1024)
#define TIMEOUT_USEC (20 * 1000 * 1000)
// github.com answers a release's download link with a redirect whose headers
// come to about 5.7 KB (its Content-Security-Policy alone is 4.2 KB), past the
// 5000 bytes the library allows unless told otherwise.
#define HEADER_MAX (64 * 1024)
#define HEADER_MAX_SMALLER (16 * 1024)

static bool started;
static int tpl = -1;

// The authorities of ca_certs.h, on top of the Vita's own. Its documentation
// isn't public, so the PEM text is offered first and the DER bytes if that is
// refused. Returns what the library answered.
static int load_authorities(void) {
	static SceHttpsData data[CA_CERT_COUNT];
	static const SceHttpsData *list[CA_CERT_COUNT];
	for (int i = 0; i < CA_CERT_COUNT; i++) {
		data[i].ptr = (char *)ca_pem[i];
		data[i].size = (unsigned)strlen(ca_pem[i]);
		list[i] = &data[i];
	}
	int res = sceHttpsLoadCert(CA_CERT_COUNT, list, NULL, NULL);
	if (res >= 0)
		return res;
	for (int i = 0; i < CA_CERT_COUNT; i++) {
		data[i].ptr = (char *)ca_der[i];
		data[i].size = ca_der_size[i];
	}
	return sceHttpsLoadCert(CA_CERT_COUNT, list, NULL, NULL);
}

bool net_start(char *why, int why_size) {
	if (!started) {
		// (each of these answers with an error if something else loaded it first, which is fine)
		sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
		sceSysmoduleLoadModule(SCE_SYSMODULE_SSL);
		sceSysmoduleLoadModule(SCE_SYSMODULE_HTTPS);
		SceNetInitParam param;
		param.memory = malloc(NET_POOL);
		param.size = NET_POOL;
		param.flags = 0;
		if (!param.memory) {
			snprintf(why, (size_t)why_size, "Out of memory for the network.");
			return false;
		}
		sceNetInit(&param);
		sceNetCtlInit();
		sceSslInit(SSL_POOL);
		int res = sceHttpInit(HTTP_POOL);
		if (res < 0) {
			snprintf(why, (size_t)why_size, "Couldn't start the network (0x%08X).", (unsigned)res);
			return false;
		}
		int certs = load_authorities();
		tpl = sceHttpCreateTemplate(USER_AGENT, SCE_HTTP_VERSION_1_1, SCE_HTTP_ENABLE);
		if (tpl < 0) {
			snprintf(why, (size_t)why_size, "Couldn't start the network (0x%08X, certificates 0x%08X).", (unsigned)tpl,
			         (unsigned)certs);
			return false;
		}
		sceHttpSetAutoRedirect(tpl, SCE_HTTP_ENABLE); // a release's file is a redirect away
		if (sceHttpSetResponseHeaderMaxSize(tpl, HEADER_MAX) < 0)
			sceHttpSetResponseHeaderMaxSize(tpl, HEADER_MAX_SMALLER);
		sceHttpSetResolveTimeOut(tpl, TIMEOUT_USEC);
		sceHttpSetConnectTimeOut(tpl, TIMEOUT_USEC);
		sceHttpSetSendTimeOut(tpl, TIMEOUT_USEC);
		sceHttpSetRecvTimeOut(tpl, TIMEOUT_USEC);
		started = true;
	}
	int state = SCE_NETCTL_STATE_DISCONNECTED;
	if (sceNetCtlInetGetState(&state) < 0 || state != SCE_NETCTL_STATE_CONNECTED) {
		snprintf(why, (size_t)why_size, "The Vita isn't connected to the internet.");
		return false;
	}
	return true;
}

// "github.com" of "https://github.com/a/b", for messages.
static void host_of(const char *url, char *host, int size) {
	const char *from = strstr(url, "://");
	from = from ? from + 3 : url;
	int n = 0;
	while (from[n] && from[n] != '/' && n < size - 1) {
		host[n] = from[n];
		n++;
	}
	host[n] = '\0';
}

char *net_get(const char *url, const char *accept, int max, int *len, void (*progress)(int got, int total), char *why,
              int why_size) {
	char host[64];
	host_of(url, host, sizeof(host));
	char *buf = NULL;
	int conn = sceHttpCreateConnectionWithURL(tpl, url, 0);
	int req = conn >= 0 ? sceHttpCreateRequestWithURL(conn, SCE_HTTP_METHOD_GET, url, 0) : -1;
	if (req < 0) {
		snprintf(why, (size_t)why_size, "Couldn't set up the request to %s (0x%08X).", host,
		         (unsigned)(conn < 0 ? conn : req));
		goto done;
	}
	if (accept)
		sceHttpAddRequestHeader(req, "Accept", accept, SCE_HTTP_HEADER_OVERWRITE);
	int res = sceHttpSendRequest(req, NULL, 0);
	if (res < 0) {
		int ssl = 0;
		unsigned detail = 0;
		sceHttpsGetSslError(req, &ssl, &detail);
		if (ssl || detail)
			snprintf(why, (size_t)why_size,
			         "Couldn't check that %s is who it says (0x%08X, %d, 0x%X).%s", host, (unsigned)res, ssl, detail,
			         detail & (SCE_HTTPS_ERROR_SSL_NOT_AFTER_CHECK | SCE_HTTPS_ERROR_SSL_NOT_BEFORE_CHECK)
			             ? " Is the Vita's date right?"
			             : "");
		else if (res == (int)SCE_HTTP_ERROR_TOO_LARGE_RESPONSE_HEADER)
			snprintf(why, (size_t)why_size, "%s's answer had more headers than this app has room for.", host);
		else
			snprintf(why, (size_t)why_size, "Couldn't reach %s (0x%08X).", host, (unsigned)res);
		goto done;
	}
	int status = 0;
	sceHttpGetStatusCode(req, &status);
	if (status != 200) {
		snprintf(why, (size_t)why_size, "%s answered %d.", host, status);
		goto done;
	}
	unsigned long long said = 0;
	int total = sceHttpGetResponseContentLength(req, &said) >= 0 && said <= (unsigned long long)max ? (int)said : 0;
	if (said > (unsigned long long)max) {
		snprintf(why, (size_t)why_size, "%s sent more than expected.", host);
		goto done;
	}
	int room = total ? total : 64 * 1024, got = 0;
	buf = malloc((size_t)room + 1);
	while (buf) {
		if (got == room) {
			if (total || room >= max)
				break; // all of it, or as much as is allowed: one more read tells which
			room = room * 2 > max ? max : room * 2;
			char *bigger = realloc(buf, (size_t)room + 1);
			if (!bigger) {
				free(buf);
				buf = NULL;
				break;
			}
			buf = bigger;
		}
		int n = sceHttpReadData(req, buf + got, (unsigned)(room - got));
		if (n < 0) {
			snprintf(why, (size_t)why_size, "The connection to %s broke (0x%08X).", host, (unsigned)n);
			free(buf);
			buf = NULL;
			goto done;
		}
		if (n == 0)
			break;
		got += n;
		if (progress)
			progress(got, total);
	}
	if (!buf) {
		snprintf(why, (size_t)why_size, "Out of memory for the download.");
		goto done;
	}
	char one;
	if ((total && got != total) || (!total && got == max && sceHttpReadData(req, &one, 1) > 0)) {
		snprintf(why, (size_t)why_size, "%s sent %s than expected.", host, total ? "less" : "more");
		free(buf);
		buf = NULL;
		goto done;
	}
	buf[got] = '\0';
	*len = got;
done:
	if (req >= 0)
		sceHttpDeleteRequest(req);
	if (conn >= 0)
		sceHttpDeleteConnection(conn);
	return buf;
}
