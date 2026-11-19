# Makes zip_case.h for unzip_harness.c: a zip with the kinds of file that
# exercise the unpacker (empty, tiny, text, noise, long repeats that reach far
# back, stored and deflated, bigger than its 32 KB window and its 64 KB reads),
# and a damaged copy of it.
#   python make_zip_case.py            writes zip_case.h and zip_expect.txt
#   python make_zip_case.py --check    reads the harness's output on stdin
import hashlib
import io
import random
import sys
import zipfile

random.seed(1968)
noise = bytes(random.getrandbits(8) for _ in range(150000))
words = ['engram', 'vita', 'bubble', 'liquid', 'font', 'page', 'scroll', 'tilt', 'sphere', 'cube']
text = ' '.join(random.choice(words) for _ in range(40000)).encode()
far = bytearray()
block = bytes(random.getrandbits(8) for _ in range(30000))
for _ in range(9):  # repeats 30,000 bytes back: nearly the furthest a copy reaches
    far += block
    far += bytes(random.getrandbits(8) for _ in range(17))
FILES = [
    ('RetroFlow/', b'', zipfile.ZIP_STORED),
    ('RetroFlow/empty.txt', b'', zipfile.ZIP_DEFLATED),
    ('RetroFlow/one.bin', b'\x7f', zipfile.ZIP_DEFLATED),
    ('RetroFlow/text.txt', text, zipfile.ZIP_DEFLATED),
    ('RetroFlow/noise_deflated.bin', noise, zipfile.ZIP_DEFLATED),
    ('RetroFlow/noise_stored.bin', noise[:100001], zipfile.ZIP_STORED),
    ('RetroFlow/THEMES/far.bin', bytes(far), zipfile.ZIP_DEFLATED),
    ('RetroFlow/THEMES/zeros.bin', bytes(200000), zipfile.ZIP_DEFLATED),
    ('RetroFlow/exactly_window.bin', noise[:32768] + text[:32768], zipfile.ZIP_DEFLATED),
]


def make():
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, 'w') as z:
        for name, data, method in FILES:
            z.writestr(zipfile.ZipInfo(name), data, compress_type=method, compresslevel=6)
    return buf.getvalue()


if '--check' in sys.argv:
    got = sys.stdin.read()
    expect = open('zip_expect.txt').read()
    sections = {s.split('\n')[0]: s for s in got.split('ZIP ')[1:]}
    ok = True
    want = [l for l in expect.splitlines()]
    for way in ('memory', 'stream'):
        lines = [l.strip()[len(way) + 1:] for l in sections['sound'].splitlines() if l.strip().startswith(way + ' RetroFlow')]
        same = lines == want
        ok &= same
        print('sound zip,', way + ':', len(lines), 'files,', 'all match Python' if same else 'DIFFER')
    for label in ('damaged', 'cut'):
        s = sections[label]
        refused = s.count('REFUSED')
        wrong = [l for l in s.splitlines() if 'RetroFlow' in l and 'REFUSED' not in l and l.strip().split(' ', 1)[1] not in want]
        print(label, 'zip: entries lines', [l.strip() for l in s.splitlines() if 'entries' in l], '| refused', refused, '| wrong contents let through:', len(wrong))
        ok &= not wrong
    print('PASS' if ok else 'FAIL')
    sys.exit(0 if ok else 1)

sound = make()
damaged = bytearray(sound)
for at in range(5000, len(damaged) - 3000, 9973):  # a flipped bit every so often, short of the directory
    damaged[at] ^= 0x04
with open('zip_case.h', 'w', newline='\n') as f:
    for name, data in (('zip_sound', sound), ('zip_damaged', bytes(damaged))):
        f.write('static const unsigned char %s[] = {%s};\n' % (name, ','.join(str(b) for b in data)))
with open('zip_expect.txt', 'w', newline='\n') as f:
    for name, data, method in FILES:
        f.write('%s %d %s\n' % (name, len(data), hashlib.sha256(data).hexdigest()))
print('zip of', len(sound), 'bytes,', len(FILES), 'entries')
