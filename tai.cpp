// Fonts > Apps: Engram's own lines in taiHEN's config, kept up to date by Settings.
//
// An app's font only takes when Engram is loaded into that app, which is a
// line under the app's *NPXS section in taiHEN's config.txt. tai_sync() makes
// those lines match the fonts each time one changes in Settings: Engram is
// listed under each Fonts > Apps title id whose font isn't the system's (its
// own, or the Live Area's while it's on Same as Live Area), and taken out from
// under the rest, so it isn't loaded into apps it does nothing in. Nothing else
// in the file changes: *KERNEL, *main, *ALL, Settings' own sections (NPXS10015
// and NPXS10016, set up by hand) and every other section stay as they are, and
// so does every other line, comment and line ending. An app's section left
// with nothing in it is taken out too. A new section goes at the end, under a
// comment that says where it came from.
//
// The file is never edited in place. The old text is copied to
// config.txt.engram-old first; the new text goes to config.txt.engram-new and
// is read back; then it's renamed over config.txt. Where a rename can't replace
// a file, config.txt is removed first, and if the rename still fails the old
// text is written back. The config as it was before Engram first changed it
// stays in TAI_ORIGINAL.
//
// taiHEN reads its config at boot, and again on taiReloadConfig, which only
// the shell may call (taihen-user.c checks ksceSblACMgrIsShell). So Settings
// leaves TAI_RELOAD_PATH, and SceShell's poll thread, which looks once a
// second, reloads the config and removes it (tai_reload_check): apps opened
// from then on load with the new lines.

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/sysmem.h>

#include "engram.h"

// taiHEN's taihenUnsafe library: every process can import it, and taiHEN
// refuses the call outside the shell. taihen.h doesn't declare it.
extern "C" int taiReloadConfig(void);
#define TAI_ERROR_BLOCKING 0x9001000D // taiHEN was loading plugins: try again

#define TAI_UX0 "ux0:tai/config.txt" // taiHEN reads this one if it's there,
#define TAI_UR0 "ur0:tai/config.txt" // and this one otherwise
#define TAI_ORIGINAL DATA_DIR "/tai_config_original.txt"
#define TAI_FILE_MAX (64 * 1024)
#define TAI_OUT_MAX (TAI_FILE_MAX + 8 * 1024)
#define TAI_LINES_MAX 2048
#define TAI_PATH_MAX 128
#define TAI_ID_MAX (FONT_APP_COUNT * 2)
#define TAI_MARK "# Engram: Fonts > Apps. Settings > System > Engram adds and takes out its lines for the apps below."
#define TAI_DEFAULT_PLUGIN "ur0:tai/engram.suprx"

enum LineKind { LINE_BLANK, LINE_COMMENT, LINE_SECTION, LINE_PLUGIN };

struct Line {
	int start, next;     // the line's first byte, and the first byte of the next (past its '\n')
	int text, text_end;  // without leading and trailing blanks or the line ending
	int kind;
};

// The title ids tai_sync() looks after, and whether each one wants Engram.
struct Plan {
	const char *ids[TAI_ID_MAX];
	bool want[TAI_ID_MAX];
	int count;
};

struct Result {
	bool added[TAI_ID_MAX], removed[TAI_ID_MAX];
	int changes;
	bool in_all; // Engram is under *ALL: it's in every app already
	char plugin[TAI_PATH_MAX];
};

struct Out {
	char *buf;
	int len, max;
	bool over;
	void put(const char *s, int n) {
		if (over || len + n > max) {
			over = true;
			return;
		}
		sceClibMemcpy(buf + len, s, n);
		len += n;
	}
	void put(const char *s) { put(s, str_len(s)); }
	void end_line(const char *nl) {
		if (len > 0 && buf[len - 1] != '\n')
			put(nl);
	}
};

static int lower(int c) {
	return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

static bool span_is(const char *in, int a, int b, const char *s) {
	int n = str_len(s);
	if (b - a != n)
		return false;
	for (int i = 0; i < n; i++)
		if (in[a + i] != s[i])
			return false;
	return true;
}

static int split_lines(const char *in, int len, Line *lines, int max) {
	int n = 0, pos = 0;
	while (pos < len) {
		if (n == max)
			return -1;
		Line *l = &lines[n++];
		l->start = pos;
		int e = pos;
		while (e < len && in[e] != '\n')
			e++;
		l->next = e < len ? e + 1 : e;
		if (e > pos && in[e - 1] == '\r')
			e--;
		int t = pos, te = e;
		while (t < te && (in[t] == ' ' || in[t] == '\t'))
			t++;
		while (te > t && (in[te - 1] == ' ' || in[te - 1] == '\t'))
			te--;
		l->text = t;
		l->text_end = te;
		l->kind = t == te ? LINE_BLANK : in[t] == '#' ? LINE_COMMENT : in[t] == '*' ? LINE_SECTION : LINE_PLUGIN;
		pos = l->next;
	}
	return n;
}

// A section's title id, after the '*', any blanks and a '!'. taiHEN stops
// reading for a title after its *!ID section (the parser's halt token), so a
// line for that title only counts in there: *!NPXS10009 is NPXS10009's.
static void section_name(const char *in, const Line *l, int *a, int *b) {
	*a = l->text + 1;
	*b = l->text_end;
	while (*a < *b && (in[*a] == ' ' || in[*a] == '\t'))
		(*a)++;
	if (*a < *b && in[*a] == '!')
		(*a)++;
}

// A plugin line whose file is engram.suprx, wherever it's kept.
static bool is_engram(const char *in, const Line *l) {
	if (l->kind != LINE_PLUGIN)
		return false;
	int a = l->text_end;
	while (a > l->text && in[a - 1] != '/' && in[a - 1] != ':')
		a--;
	const char *name = "engram.suprx";
	if (l->text_end - a != str_len(name))
		return false;
	for (int i = 0; name[i]; i++)
		if (lower(in[a + i]) != name[i])
			return false;
	return true;
}

static int plan_index(const char *in, const Line *l, const Plan *plan) {
	int a, b;
	section_name(in, l, &a, &b);
	for (int m = 0; m < plan->count; m++)
		if (span_is(in, a, b, plan->ids[m]))
			return m;
	return -1;
}

// Builds the new config text in out. The rest of the file is copied byte for
// byte; only Engram's lines under the plan's sections change.
static void rewrite(const char *in, int len, Line *lines, int n, const Plan *plan, Out *out, Result *res) {
	const char *nl = "\n";
	for (int i = 0; i + 1 < len; i++)
		if (in[i] == '\r' && in[i + 1] == '\n') {
			nl = "\r\n";
			break;
		}

	// Where Engram's own line is (its path is copied for new lines), whether
	// it's under *ALL, which of the plan's ids have it already (the first one
	// is kept), and whether the comment for Engram's sections is there.
	int first_engram[TAI_ID_MAX], first_section[TAI_ID_MAX];
	for (int m = 0; m < plan->count; m++)
		first_engram[m] = first_section[m] = -1;
	bool marked = false, plugin_found = false;
	int section = -1, cur = -1;
	for (int i = 0; i < n; i++) {
		const Line *l = &lines[i];
		if (l->kind == LINE_SECTION) {
			section = i;
			cur = plan_index(in, l, plan);
			if (cur >= 0 && first_section[cur] < 0)
				first_section[cur] = i;
			continue;
		}
		if (l->kind == LINE_COMMENT && span_is(in, l->text, l->text_end, TAI_MARK))
			marked = true;
		if (!is_engram(in, l))
			continue;
		if (cur >= 0 && first_engram[cur] < 0)
			first_engram[cur] = i;
		if (section >= 0) {
			int a, b;
			section_name(in, &lines[section], &a, &b);
			if (span_is(in, a, b, "ALL"))
				res->in_all = true;
			bool main = span_is(in, a, b, "main") || span_is(in, a, b, "NPXS10015");
			int size = l->text_end - l->text;
			if ((main || !plugin_found) && size < TAI_PATH_MAX) {
				sceClibMemcpy(res->plugin, in + l->text, size);
				res->plugin[size] = '\0';
				plugin_found = plugin_found || main;
			}
		}
	}
	if (!res->plugin[0])
		sceClibStrncpy(res->plugin, TAI_DEFAULT_PLUGIN, TAI_PATH_MAX - 1);
	if (res->in_all) {
		out->put(in, len);
		return;
	}

	int i = 0;
	while (i < n) {
		if (lines[i].kind != LINE_SECTION) {
			out->put(in + lines[i].start, lines[i].next - lines[i].start);
			i++;
			continue;
		}
		int j = i + 1;
		while (j < n && lines[j].kind != LINE_SECTION)
			j++;
		int m = plan_index(in, &lines[i], plan);
		if (m < 0) {
			out->put(in + lines[i].start, lines[j - 1].next - lines[i].start);
			i = j;
			continue;
		}

		bool want = plan->want[m];
		int others = 0, engrams = 0, last = i; // last: the section's last line that isn't blank
		for (int k = i + 1; k < j; k++) {
			if (lines[k].kind == LINE_PLUGIN && !is_engram(in, &lines[k]))
				others++;
			if (is_engram(in, &lines[k]))
				engrams++;
			if (lines[k].kind != LINE_BLANK)
				last = k;
		}
		bool insert = want && first_engram[m] < 0 && first_section[m] == i;
		// Only a section Engram's line is taken out of, with no plugin left.
		bool drop_header = !want && engrams > 0 && others == 0;
		for (int k = i; k < j; k++) {
			const Line *l = &lines[k];
			bool engram = is_engram(in, l);
			if (engram && !(want && k == first_engram[m])) {
				res->changes++;
				if (!want)
					res->removed[m] = true;
				continue; // Engram's line out, or a second copy of it
			}
			// A section with nothing left loses its header and blank lines;
			// its comments stay.
			if (drop_header && (l->kind == LINE_SECTION || l->kind == LINE_BLANK))
				continue;
			out->put(in + l->start, l->next - l->start);
			if (insert && k == last) {
				out->end_line(nl);
				out->put(res->plugin);
				out->put(nl);
				res->added[m] = true;
				res->changes++;
			}
		}
		i = j;
	}

	for (int m = 0; m < plan->count; m++) {
		if (!plan->want[m] || first_engram[m] >= 0 || first_section[m] >= 0)
			continue;
		out->end_line(nl);
		if (!marked) {
			out->put(TAI_MARK);
			out->put(nl);
			marked = true;
		}
		out->put("*");
		out->put(plan->ids[m]);
		out->put(nl);
		out->put(res->plugin);
		out->put(nl);
		res->added[m] = true;
		res->changes++;
	}
}

// Which ids want Engram: an app whose font isn't the system's, if it's on
// this Vita. Settings is left out: its sections are set up by hand.
static void make_plan(const Config *cfg, Plan *plan) {
	// If vs0:app can't be looked at from here, every app counts as there.
	bool can_look = file_exists("vs0:app/NPXS10015");
	int settings = font_app_index("NPXS10015");
	plan->count = 0;
	for (int a = 0; a < FONT_APP_COUNT; a++) {
		if (a == settings)
			continue;
		bool font = font_for_app(cfg, a)[0] != '\0';
		for (int k = 0; k < 2; k++) {
			const char *id = FONT_APPS[a].ids[k];
			if (!id || plan->count == TAI_ID_MAX)
				continue;
			char dir[32];
			sceClibSnprintf(dir, sizeof(dir), "vs0:app/%s", id);
			plan->ids[plan->count] = id;
			plan->want[plan->count] = font && (!can_look || file_exists(dir));
			plan->count++;
		}
	}
}

static int read_file(const char *path, char *buf, int max) {
	SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
	if (fd < 0)
		return fd;
	int len = 0;
	while (len < max) {
		int got = sceIoRead(fd, buf + len, max - len);
		if (got < 0) {
			sceIoClose(fd);
			return got;
		}
		if (got == 0)
			break;
		len += got;
	}
	sceIoClose(fd);
	return len; // max means it may not all have fitted
}

static int write_file(const char *path, const char *buf, int len) {
	SceUID fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
	if (fd < 0)
		return fd;
	int done = 0;
	while (done < len) {
		int put = sceIoWrite(fd, buf + done, len - done);
		if (put <= 0) {
			sceIoClose(fd);
			return put < 0 ? put : -1;
		}
		done += put;
	}
	sceIoSyncByFd(fd, 0);
	return sceIoClose(fd);
}

static void list_ids(const Plan *plan, const bool *which, char *text, int size) {
	int len = 0;
	text[0] = '\0';
	for (int m = 0; m < plan->count && len < size - 12; m++)
		if (which[m])
			len += sceClibSnprintf(text + len, size - len, "%s%s", len ? " " : "", plan->ids[m]);
	if (!len)
		sceClibStrncpy(text, "none", size - 1);
}

int tai_sync(const Config *cfg) {
	const char *path = file_exists(TAI_UX0) ? TAI_UX0 : TAI_UR0;
	if (!file_exists(path)) {
		log_line("tai config: no %s or %s, nothing changed\n", TAI_UX0, TAI_UR0);
		return -1;
	}

	// One block for the old text, the new text, the new file read back and the
	// line table, freed before returning: none of it stays in Settings' memory.
	const int in_size = TAI_FILE_MAX, out_size = TAI_OUT_MAX, lines_size = TAI_LINES_MAX * (int)sizeof(Line);
	SceSize size = (SceSize)(in_size + 2 * out_size + lines_size + 0xFFF) & ~0xFFFu;
	SceUID block = sceKernelAllocMemBlock("EngramTaiConfig", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, size, NULL);
	void *base = NULL;
	if (block < 0 || sceKernelGetMemBlockBase(block, &base) < 0) {
		log_line("tai config: no memory to change %s (0x%08X)\n", path, block);
		if (block >= 0)
			sceKernelFreeMemBlock(block);
		return -1;
	}
	char *in = (char *)base, *outbuf = in + in_size, *backbuf = outbuf + out_size;
	Line *lines = (Line *)(backbuf + out_size);

	static Plan plan;
	static Result res;
	sceClibMemset(&res, 0, sizeof(res));
	make_plan(cfg, &plan);
	int ret = -1;
	int len = read_file(path, in, in_size);
	int n = len >= 0 && len < in_size ? split_lines(in, len, lines, TAI_LINES_MAX) : -1;
	Out out = {outbuf, 0, out_size, false};
	if (len < 0) {
		log_line("tai config: reading %s failed (0x%08X), nothing changed\n", path, len);
		goto done;
	}
	if (n < 0) {
		log_line("tai config: %s is over %d KB or %d lines, nothing changed\n", path, TAI_FILE_MAX / 1024, TAI_LINES_MAX);
		goto done;
	}
	rewrite(in, len, lines, n, &plan, &out, &res);
	if (res.in_all) {
		log_line("tai config: Engram is under *ALL in %s, so every app has it: nothing changed\n", path);
		ret = 0;
		goto done;
	}
	if (out.over) {
		log_line("tai config: the new %s didn't fit in %d KB, nothing changed\n", path, TAI_OUT_MAX / 1024);
		goto done;
	}
	if (res.changes == 0) {
		ret = 0;
		goto done;
	}

	{
		if (!file_exists(TAI_ORIGINAL)) {
			int saved = write_file(TAI_ORIGINAL, in, len);
			log_line("tai config: kept %s as it was in " TAI_ORIGINAL " (0x%08X)\n", path, saved);
			if (saved < 0)
				goto done; // no backup, no change
		}
		char tmp[48], old[48];
		sceClibSnprintf(tmp, sizeof(tmp), "%s.engram-new", path);
		sceClibSnprintf(old, sizeof(old), "%s.engram-old", path);
		int kept = write_file(old, in, len);
		if (kept < 0) {
			log_line("tai config: copying %s to %s failed (0x%08X), nothing changed\n", path, old, kept);
			goto done;
		}
		int w = write_file(tmp, out.buf, out.len);
		int back = w >= 0 ? read_file(tmp, backbuf, out_size) : -1;
		if (w < 0 || back != out.len || sceClibMemcmp(backbuf, out.buf, out.len) != 0) {
			log_line("tai config: writing %s failed (0x%08X, read back %d of %d bytes), nothing changed\n", tmp, w, back,
			         out.len);
			sceIoRemove(tmp);
			goto done;
		}
		int r = sceIoRename(tmp, path);
		if (r < 0) {
			// This rename doesn't replace a file: take the old one out of the
			// way first, and write it back if the new one still won't go in.
			int removed_ret = sceIoRemove(path);
			r = sceIoRename(tmp, path);
			if (r < 0) {
				int restored = write_file(path, in, len);
				log_line("tai config: putting the new %s in place failed (0x%08X, 0x%08X); the old text is back "
				         "(0x%08X)\n",
				         path, removed_ret, r, restored);
				sceIoRemove(tmp);
				goto done;
			}
		}
		write_marker(TAI_RELOAD_PATH);
		char added[200], removed[200];
		list_ids(&plan, res.added, added, sizeof(added));
		list_ids(&plan, res.removed, removed, sizeof(removed));
		log_line("tai config: %s changed (%d lines), the old one is %s\n", path, res.changes, old);
		log_line("tai config: Engram added under %s\n", added);
		log_line("tai config: Engram taken out from under %s\n", removed);
		ret = res.changes;
	}

done:
	sceKernelFreeMemBlock(block);
	return ret;
}

void tai_reload_check(void) {
	if (!file_exists(TAI_RELOAD_PATH))
		return;
	int ret = taiReloadConfig();
	if (ret == (int)TAI_ERROR_BLOCKING)
		return; // taiHEN is busy loading plugins: the marker stays for the next look
	sceIoRemove(TAI_RELOAD_PATH);
	log_line("tai config: reloaded for apps opened from now on (0x%08X)\n", ret);
}
