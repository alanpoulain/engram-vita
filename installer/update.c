#include "update.h"

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "files.h"
#include "net.h"
#include "sha256.h"
#include "unzip.h"

#define REPO_API "https://api.github.com/repos/chazzarchibald-ops/engram-vita"
#ifdef UPDATE_TEST_TAG
// A test build (see CMakeLists.txt): one release by its tag, which finds a
// pre-release too. "The latest" never is one.
#define RELEASES REPO_API "/releases/tags/" UPDATE_TEST_TAG
#define ASKING "Asking GitHub for the " UPDATE_TEST_TAG " release (update test)..."
#else
#define RELEASES REPO_API "/releases/latest"
#define ASKING "Asking GitHub for the latest release..."
#endif
#define DOWNLOADS "https://github.com/chazzarchibald-ops/engram-vita/releases/download/"
#define ASSET "engram_installer.vpk"
#define TITLE_ID "ENGR00001"
#define APP_DIR "ux0:app/" TITLE_ID
#define KEPT_VPK "ux0:data/" ASSET
#define ANSWER_MAX (256 * 1024)
#define VPK_MAX (16 * 1024 * 1024)
#define FILES_MAX 64

// ---- GitHub's answer: just enough JSON ----

// The first "key" at or after from, and where its value starts. NULL if there is none.
static const char *json_value(const char *from, const char *key) {
	char quoted[40];
	snprintf(quoted, sizeof(quoted), "\"%s\"", key);
	for (const char *at = strstr(from, quoted); at; at = strstr(at + 1, quoted)) {
		const char *p = at + strlen(quoted);
		while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
			p++;
		if (*p != ':')
			continue; // the word as a value, not a key
		p++;
		while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
			p++;
		return p;
	}
	return NULL;
}

// The string that "key" has, into out. False if it has none, or it doesn't fit.
static bool json_string(const char *from, const char *key, char *out, int size) {
	const char *p = json_value(from, key);
	if (!p || *p != '"')
		return false;
	int n = 0;
	for (p++; *p && *p != '"'; p++) {
		if (*p == '\\' && p[1])
			p++; // \" and \/ and \\ are themselves; nothing else is expected here
		if (n >= size - 1)
			return false;
		out[n++] = *p;
	}
	out[n] = '\0';
	return *p == '"';
}

static int json_number(const char *from, const char *key) {
	const char *p = json_value(from, key);
	if (!p || *p < '0' || *p > '9')
		return -1;
	int value = 0;
	for (; *p >= '0' && *p <= '9' && value < 100000000; p++)
		value = value * 10 + (*p - '0');
	return value;
}

// "1.10" against "1.9": more than 0 if a is the newer. A leading v is ignored.
static int version_compare(const char *a, const char *b) {
	if (*a == 'v' || *a == 'V')
		a++;
	if (*b == 'v' || *b == 'V')
		b++;
	for (;;) {
		int x = 0, y = 0;
		for (; *a >= '0' && *a <= '9'; a++)
			x = x * 10 + (*a - '0');
		for (; *b >= '0' && *b <= '9'; b++)
			y = y * 10 + (*b - '0');
		if (x != y)
			return x - y;
		if (*a != '.' && *b != '.')
			return 0;
		if (*a == '.')
			a++;
		if (*b == '.')
			b++;
	}
}

static bool is_version(const char *s) {
	if (*s == 'v' || *s == 'V')
		s++;
	if (*s < '0' || *s > '9')
		return false;
	for (; *s; s++)
		if ((*s < '0' || *s > '9') && *s != '.')
			return false;
	return true;
}

UpdateFound update_check(const char *have, UpdateInfo *info, UpdateSay say) {
	char why[160];
	memset(info, 0, sizeof(*info));
	say(SAY_DIM, false, ASKING);
	if (!net_start(why, sizeof(why))) {
		say(SAY_BAD, false, "%s", why);
		return UPDATE_FAILED;
	}
	int len = 0;
	char *answer = net_get(RELEASES, "application/vnd.github+json", ANSWER_MAX, &len, NULL, why, sizeof(why));
	if (!answer) {
		say(SAY_BAD, false, "%s", why);
		return UPDATE_FAILED;
	}
	UpdateFound found = UPDATE_FAILED;
	if (!json_string(answer, "tag_name", info->tag, sizeof(info->tag)) || !is_version(info->tag)) {
		say(SAY_BAD, false, "GitHub's answer has no version in it.");
	} else if (version_compare(info->tag, have) <= 0) {
		say(SAY_GOOD, false, "This installer has the latest Engram (%s).", have);
		found = UPDATE_NONE;
	} else {
		// the release's file named ASSET, and what GitHub says of it
		const char *asset = NULL;
		char name[64];
		for (const char *at = answer; (at = strstr(at, "\"name\"")) != NULL; at++)
			if (json_string(at, "name", name, sizeof(name)) && strcmp(name, ASSET) == 0) {
				asset = at;
				break;
			}
		char digest[80] = "";
		if (asset) {
			info->size = json_number(asset, "size");
			json_string(asset, "digest", digest, sizeof(digest));
			json_string(asset, "browser_download_url", info->url, sizeof(info->url));
		}
		say(SAY_TEXT, false, "Engram %s is out. This installer has %s.", info->tag, have);
		if (!asset) {
			say(SAY_BAD, false, "That release has no " ASSET " to update with.");
		} else if (strncmp(info->url, DOWNLOADS, strlen(DOWNLOADS)) != 0 || info->size <= 0 || info->size > VPK_MAX ||
		           strncmp(digest, "sha256:", 7) != 0 || strlen(digest + 7) != 64) {
			say(SAY_BAD, false, "GitHub's details of " ASSET " aren't as expected.");
		} else {
			memcpy(info->digest, digest + 7, 65);
			say(SAY_GOOD, false, "It can be downloaded (%d KB) and put in place of this one.", (info->size + 1023) / 1024);
			found = UPDATE_READY;
		}
	}
	free(answer);
	return found;
}

// ---- putting the new files in ----

static UpdateSay progress_say;
static int progress_shown;

static void download_progress(int got, int total) {
	if (total <= 0)
		return;
	int tenth = got / 1024 * 10 / (total / 1024 + 1); // (in KB, so a big file's count fits)
	if (tenth != progress_shown) {
		progress_shown = tenth;
		progress_say(SAY_DIM, true, "Downloading " ASSET "... %d%%", tenth * 10);
	}
}

// A file name of the VPK as a path under dir. False for a name that could
// lead out of dir, or that doesn't fit.
static bool path_in(const char *dir, const ZipEntry *e, char *path, int size) {
	if (e->name_len <= 0 || e->name[0] == '/' || (int)strlen(dir) + 1 + e->name_len >= size)
		return false;
	for (int i = 0; i < e->name_len; i++) {
		char c = e->name[i];
		if (c == '\\' || c == ':' || c == '\0' || (c == '.' && i + 1 < e->name_len && e->name[i + 1] == '.'))
			return false;
	}
	snprintf(path, (size_t)size, "%s/%.*s", dir, e->name_len, e->name);
	return true;
}

static bool is_name(const ZipEntry *e, const char *name) {
	return e->name_len == (int)strlen(name) && memcmp(e->name, name, (size_t)e->name_len) == 0;
}

static bool contains(const uint8_t *data, uint32_t len, const char *word) {
	uint32_t n = (uint32_t)strlen(word);
	for (uint32_t i = 0; i + n <= len; i++)
		if (memcmp(data + i, word, n) == 0)
			return true;
	return false;
}

// Makes the folders a path's file sits in.
static void make_folders(const char *path) {
	char dir[200];
	snprintf(dir, sizeof(dir), "%s", path);
	for (char *p = strchr(dir, ':'); p && (p = strchr(p + 1, '/')) != NULL;) {
		*p = '\0';
		sceIoMkdir(dir, 0777);
		*p = '/';
	}
}

bool update_install(const UpdateInfo *info, UpdateSay say) {
	static ZipEntry entries[FILES_MAX];
	uint8_t *files[FILES_MAX];
	char why[160], path[200], staged[208];
	bool ok = false, touched = false;
	int count = 0;
	memset(files, 0, sizeof(files));

	if (!file_exists(APP_DIR "/eboot.bin")) {
		say(SAY_BAD, false, "This app isn't in " APP_DIR ", so it can't update itself.");
		return false;
	}
	if (!net_start(why, sizeof(why))) {
		say(SAY_BAD, false, "%s", why);
		return false;
	}
	progress_say = say;
	progress_shown = 0;
	say(SAY_DIM, false, "Downloading " ASSET "... 0%%");
	int len = 0;
	uint8_t *vpk = (uint8_t *)net_get(info->url, NULL, VPK_MAX, &len, download_progress, why, sizeof(why));
	if (!vpk) {
		say(SAY_BAD, false, "%s", why);
		return false;
	}
	say(SAY_TEXT, true, "Downloaded " ASSET " (%d bytes).", len);

	// 1. it is the file GitHub's release lists
	char digest[65];
	sha256_hex(vpk, (uint32_t)len, digest);
	if (len != info->size || strcmp(digest, info->digest) != 0) {
		say(SAY_BAD, false, "The download isn't the file GitHub lists (its SHA-256 differs). Nothing was changed.");
		goto done;
	}
	say(SAY_GOOD, false, "Its SHA-256 is the one GitHub lists.");

	// 2. it unpacks whole, and it is this app
	count = zip_entries(vpk, (uint32_t)len, entries, FILES_MAX);
	bool has_eboot = false, has_plugin = false, is_this_app = false, sound = count > 0 && count < FILES_MAX;
	for (int i = 0; sound && i < count; i++) {
		if (entries[i].name_len > 0 && entries[i].name[entries[i].name_len - 1] == '/')
			continue; // a folder
		sound = path_in(APP_DIR, &entries[i], path, sizeof(path)) &&
		        (files[i] = zip_extract(vpk, (uint32_t)len, &entries[i])) != NULL;
		if (!sound)
			break;
		has_eboot |= is_name(&entries[i], "eboot.bin");
		has_plugin |= is_name(&entries[i], "engram.suprx");
		if (is_name(&entries[i], "sce_sys/param.sfo"))
			is_this_app = contains(files[i], entries[i].size, TITLE_ID);
	}
	if (!sound || !has_eboot || !has_plugin || !is_this_app) {
		say(SAY_BAD, false, "The download doesn't unpack as the Engram Installer. Nothing was changed.");
		goto done;
	}

	// 3. a copy to install by hand, should the rest go wrong
	bool kept = file_write_checked(KEPT_VPK, (const char *)vpk, len);
	if (kept)
		say(SAY_DIM, false, "Kept the VPK at " KEPT_VPK ".");

	// 4. every new file beside the one it replaces, then the swaps, the app itself last
	for (int i = 0; i < count; i++) {
		if (!files[i])
			continue;
		path_in(APP_DIR, &entries[i], path, sizeof(path));
		snprintf(staged, sizeof(staged), "%s.new", path);
		make_folders(path);
		if (!file_write_checked(staged, (const char *)files[i], (int)entries[i].size)) {
			say(SAY_BAD, false, "Couldn't write to " APP_DIR ". Nothing was changed.");
			for (int j = 0; j <= i; j++)
				if (files[j] && path_in(APP_DIR, &entries[j], path, sizeof(path))) {
					snprintf(staged, sizeof(staged), "%s.new", path);
					sceIoRemove(staged);
				}
			if (kept)
				say(SAY_TEXT, false, "You can install the kept VPK with VitaShell instead.");
			goto done;
		}
	}
	ok = true;
	for (int pass = 0; pass < 2; pass++)
		for (int i = 0; i < count; i++) {
			if (!files[i] || is_name(&entries[i], "eboot.bin") != (pass == 1))
				continue;
			path_in(APP_DIR, &entries[i], path, sizeof(path));
			snprintf(staged, sizeof(staged), "%s.new", path);
			touched = true;
			sceIoRemove(path);
			if (sceIoRename(staged, path) < 0 &&
			    !file_write_checked(path, (const char *)files[i], (int)entries[i].size))
				ok = false;
			sceIoRemove(staged);
		}
	if (ok)
		say(SAY_GOOD, false, "This installer now has Engram %s. Restart it, then choose Install.", info->tag);
	else if (touched)
		say(SAY_BAD, false, "Some of the app's files couldn't be replaced. Install %s with VitaShell.",
		    kept ? "the kept VPK" : "the new VPK");
done:
	for (int i = 0; i < FILES_MAX; i++)
		free(files[i]);
	free(vpk);
	return ok;
}
