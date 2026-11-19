# Tests for the installer's updater

`update.c`, `headbin.c`, `unzip.c` and the two hashes run here on the PC, on
the ARM simulator that comes with VitaSDK (`arm-vita-eabi-run`), with the
network, the file system and the system's installer replaced by stand-ins
that print what they are asked to do. Both halves of an update run: the
installer's (download, check, set the updater up) and the updater's (unpack,
hand the result to the system's installer).

    python make_cases.py
    arm-vita-eabi-gcc -O2 -Wall -marm -march=armv5te -mfloat-abi=soft -ffreestanding -nostdlib -nostartfiles -I.. -I. -Wl,-Ttext=0x8000 -Wl,--build-id=none -o test.elf update_harness.c ../update.c ../json.c ../unzip.c ../sha256.c ../sha1.c ../headbin.c
    arm-vita-eabi-run --memory-size 96m test.elf

`make_cases.py` asks GitHub for the latest release (the real answer is one of
the cases) and needs a built `engram_installer.vpk`: give its path, or it
looks in `..uild`.
It writes `cases.h` and `expect_files.txt`, which aren't kept in the
repository. Each case prints what the app would say and every file it would
write, with the file's SHA-256, to compare with `expect_files.txt`.

`make_cases.py` also makes the package headers of the cases itself, so
`headbin.c`'s digests are checked against a second implementation.

What this doesn't cover: the connection itself (`net.c`) and the system's
installer (`package.c`), which only a Vita can run.

## The unpacker on its own

`unzip.c` both ways, a zip held in memory and one read and unpacked a piece at
a time (as the 422 MB of CyberFlow's data files is), with `sha256.c` hashing
what comes out in pieces:

    python make_zip_case.py
    arm-vita-eabi-gcc (the same options) -o zip.elf unzip_harness.c ../unzip.c ../sha256.c
    arm-vita-eabi-run --memory-size 96m zip.elf | python make_zip_case.py --check

The zip has empty, tiny, text and noise files, stored and deflated, copies
that reach nearly a whole window back, and files bigger than the window and
the read buffer. A damaged copy and a cut-short one must be refused.

## Photos and what GitHub says

`png.c` (the PNG reader and the fitting of a picture into the photo's frame)
and `lookup.c` (reading GitHub's answers: a release's files, a project's
releases with their notes, and its screenshots):

    python make_photo_case.py
    arm-vita-eabi-gcc (the same options) -o photo.elf photo_harness.c ../png.c ../lookup.c ../json.c ../unzip.c ../sha256.c
    arm-vita-eabi-run --memory-size 128m photo.elf | python make_photo_case.py --check

`make_photo_case.py` fetches two real screenshots and GitHub's real answers
about the projects, makes PNGs of every kind the reader takes and some it must
refuse, and works out what should come of each on its own: Pillow for the
pixels, and the fitting and the clean-up of a release's notes written a second
time in Python.

`projects.c` (the installs from "My other projects") and `live.c` (the
lookups' thread and what it keeps on the memory card) have no test here: they
are mostly calls to the network, the file system and the system's installer.
