# Tests for the installer's updater

`update.c`, `headbin.c`, `unzip.c` and the two hashes run here on the PC, on
the ARM simulator that comes with VitaSDK (`arm-vita-eabi-run`), with the
network, the file system and the system's installer replaced by stand-ins
that print what they are asked to do. Both halves of an update run: the
installer's (download, check, set the updater up) and the updater's (unpack,
hand the result to the system's installer).

    python make_cases.py
    arm-vita-eabi-gcc -O2 -Wall -marm -march=armv5te -mfloat-abi=soft -ffreestanding -nostdlib -nostartfiles -I.. -I. -Wl,-Ttext=0x8000 -Wl,--build-id=none -o test.elf update_harness.c ../update.c ../unzip.c ../sha256.c ../sha1.c ../headbin.c
    arm-vita-eabi-run test.elf

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
