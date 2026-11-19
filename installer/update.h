// Checks GitHub for a newer Engram release and, if its installer VPK is there,
// downloads it and puts its files in place of this app's own.
//
// What a release needs for this: a tag that is a version number ("1.6") and a
// file named engram_installer.vpk. The VPK is checked three ways before
// anything of the app is touched: the connection is to GitHub (net.h), the
// file's SHA-256 is the one GitHub's release page gives for it, and it unpacks
// whole, with this app's title ID inside.
#ifndef UPDATE_H
#define UPDATE_H

#include <stdbool.h>

typedef enum { SAY_TEXT, SAY_DIM, SAY_GOOD, SAY_BAD } SayKind;
// A line for the screen. again: it takes the place of the line before (progress).
typedef void (*UpdateSay)(SayKind kind, bool again, const char *fmt, ...);

typedef struct {
	char tag[24];     // the release's version, "1.6"
	char url[320];    // where its engram_installer.vpk is
	char digest[65];  // that file's SHA-256, as GitHub gives it
	int size;
} UpdateInfo;

typedef enum { UPDATE_FAILED, UPDATE_NONE, UPDATE_READY } UpdateFound;

// have: the Engram version this installer carries.
UpdateFound update_check(const char *have, UpdateInfo *info, UpdateSay say);
// Downloads and checks the VPK, keeps a copy in ux0:data, and replaces the
// app's files with its own. True if the app is now the new version.
bool update_install(const UpdateInfo *info, UpdateSay say);

#endif
