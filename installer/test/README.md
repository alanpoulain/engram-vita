# Tests for the installer's updater

`update.c`, `unzip.c` and `sha256.c` run here on the PC, on the ARM simulator
that comes with VitaSDK (`arm-vita-eabi-run`), with the network and the file
system replaced by stand-ins that print what they are asked to do.

    python make_cases.py
    arm-vita-eabi-gcc -O2 -Wall -marm -march=armv5te -mfloat-abi=soft -ffreestanding -nostdlib -nostartfiles -I.. -I. -Wl,-Ttext=0x8000 -Wl,--build-id=none -o test.elf update_harness.c ../update.c ../unzip.c ../sha256.c
    arm-vita-eabi-run test.elf

`make_cases.py` asks GitHub for the latest release (the real answer is one of
the cases) and needs a built `engram_installer.vpk` (its path is at the top).
It writes `cases.h` and `expect_files.txt`, which aren't kept in the
repository. Each case prints what the app would say and every file it would
write, with the file's SHA-256, to compare with `expect_files.txt`.

What this doesn't cover: the connection itself (`net.c`), which only a Vita
can run.
