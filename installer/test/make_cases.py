# Makes cases.h for update_harness.c: GitHub's real answer for the latest
# release, and variations of it, each with the VPK the stand-in network hands
# out and the package header the app finds in its own folder.
#   python make_cases.py [path to engram_installer.vpk]
import copy
import hashlib
import io
import json
import struct
import sys
import urllib.request
import zipfile

VPK = open(sys.argv[1] if len(sys.argv) > 1 else r'..\build\engram_installer.vpk', 'rb').read()
req = urllib.request.Request('https://api.github.com/repos/chazzarchibald-ops/engram-vita/releases/latest',
                             headers={'Accept': 'application/vnd.github+json', 'User-Agent': 'EngramInstaller'})
raw = urllib.request.urlopen(req, timeout=20).read()
real = json.loads(raw)
print('real answer:', len(raw), 'bytes, tag', real['tag_name'], 'assets', [a['name'] for a in real['assets']])


def with_installer(tag, vpk=VPK, digest=None, url=None, size=None, first=False):
    r = copy.deepcopy(real)
    r['tag_name'] = tag
    a = copy.deepcopy(r['assets'][0])
    a['name'] = 'engram_installer.vpk'
    a['size'] = len(vpk) if size is None else size
    a['digest'] = 'sha256:' + hashlib.sha256(vpk).hexdigest() if digest is None else digest
    a['browser_download_url'] = url or 'https://github.com/chazzarchibald-ops/engram-vita/releases/download/%s/engram_installer.vpk' % tag
    r['assets'] = [a] + r['assets'] if first else r['assets'] + [a]
    return r


def other_tag(tag):
    r = copy.deepcopy(real)
    r['tag_name'] = tag
    return r


# A package header of the layout headbin.h describes, made here on its own so
# that headbin.c's digests are checked against a second implementation.
DIGEST_FROM = [4, 5, 6, 7, 8, 9, 10, 11, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 1, 2, 3, 12, 13, 14, 15, 16, 1, 2, 3]


def pkg_digest(data):
    first = hashlib.sha1(data).digest()
    return hashlib.sha1(bytes(first[i] for i in DIGEST_FROM) + bytes(32)).digest()[:16]


def head_bin(title):
    h = bytearray(hashlib.shake_128(b'filler').digest(1072))
    h[0:4] = b'\x7fPKG'
    for at, value in ((0x08, 0x280), (0x10, 0x190), (0xD0, 0x100), (0xD4, 0x410), (0xE8, 0x420)):
        h[at:at + 4] = struct.pack('>I', value)
    h[0x30:0x60] = ('EP9000-%s_00-0000000000000000' % title).encode().ljust(48, b'\0')
    h[0x100:0x110] = pkg_digest(bytes(h[:0x100]))
    h[0x410:0x420] = pkg_digest(bytes(h[0x280:0x280 + 0x190 - 64]))
    h[0x420:0x430] = pkg_digest(bytes(h[:0x420]))
    return bytes(h)


HEAD = head_bin('ENGR00001')
head_damaged = bytearray(HEAD)
head_damaged[0x300] ^= 1
head_other = head_bin('OTHR00001')

damaged = bytearray(VPK)
damaged[len(damaged) // 2] ^= 0x40
# a VPK that is a sound zip but another app
foreign = io.BytesIO()
with zipfile.ZipFile(foreign, 'w', zipfile.ZIP_DEFLATED) as z:
    src = zipfile.ZipFile(io.BytesIO(VPK))
    for i in src.infolist():
        data = src.read(i)
        if i.filename == 'sce_sys/param.sfo':
            data = data.replace(b'ENGR00001', b'OTHR00001')
        z.writestr(i.filename, data)
foreign = foreign.getvalue()
# one whose file names try to leave the app's folder
escaping = io.BytesIO()
with zipfile.ZipFile(escaping, 'w', zipfile.ZIP_DEFLATED) as z:
    src = zipfile.ZipFile(io.BytesIO(VPK))
    for i in src.infolist():
        z.writestr(i.filename, src.read(i))
    z.writestr('../../tai/config.txt', b'bad')
escaping = escaping.getvalue()

sound = json.dumps(with_installer('1.6')).encode()
# name, have, answer (bytes), vpk, head, fail_write_at, fail_install_at, tamper
cases = [
    ('the real answer, same version', real['tag_name'], raw, VPK, HEAD, 0, 0, 0),
    ('the real answer, older installer', '1.4', raw, VPK, HEAD, 0, 0, 0),
    ('1.6 with an installer, compact JSON', '1.5', json.dumps(with_installer('1.6'), separators=(',', ':')).encode(), VPK, HEAD, 0, 0, 0),
    ('1.6 with an installer listed first, indented JSON', '1.5', json.dumps(with_installer('1.6', first=True), indent=2).encode(), VPK, HEAD, 0, 0, 0),
    ('1.10 against 1.9', '1.9', json.dumps(with_installer('1.10')).encode(), VPK, HEAD, 0, 0, 0),
    ('v1.5.1 against 1.5', '1.5', json.dumps(with_installer('v1.5.1')).encode(), VPK, HEAD, 0, 0, 0),
    ('1.5 against 1.5.0 (the same)', '1.5.0', json.dumps(other_tag('1.5')).encode(), VPK, HEAD, 0, 0, 0),
    ('an older release than the installer', '9.9', raw, VPK, HEAD, 0, 0, 0),
    ('a tag that is no version', '1.5', json.dumps(other_tag('nightly')).encode(), VPK, HEAD, 0, 0, 0),
    ('no digest from GitHub', '1.5', json.dumps(with_installer('1.6', digest='')).encode(), VPK, HEAD, 0, 0, 0),
    ('a download link somewhere else', '1.5', json.dumps(with_installer('1.6', url='https://example.com/engram_installer.vpk')).encode(), VPK, HEAD, 0, 0, 0),
    ('the download is damaged', '1.5', sound, bytes(damaged), HEAD, 0, 0, 0),
    ('the download is another app (digest matches)', '1.5', json.dumps(with_installer('1.6', vpk=foreign)).encode(), foreign, HEAD, 0, 0, 0),
    ('a file name that leaves the folder (digest matches)', '1.5', json.dumps(with_installer('1.6', vpk=escaping)).encode(), escaping, HEAD, 0, 0, 0),
    ('not JSON at all', '1.5', b'<html>Sign in to the hotel wifi</html>', VPK, HEAD, 0, 0, 0),
    ('the app has no package header', '1.5', sound, VPK, b'', 0, 0, 0),
    ('the package header is damaged', '1.5', sound, VPK, bytes(head_damaged), 0, 0, 0),
    ('the package header is another app\'s', '1.5', sound, VPK, head_other, 0, 0, 0),
    ('the VPK can\'t be kept (1st write fails)', '1.5', sound, VPK, HEAD, 1, 0, 0),
    ('the updater can\'t be laid out (4th write fails)', '1.5', sound, VPK, HEAD, 4, 0, 0),
    ('the system won\'t install the updater', '1.5', sound, VPK, HEAD, 0, 1, 0),
    ('the kept VPK is changed before the updater reads it', '1.5', sound, VPK, HEAD, 0, 0, 1),
    ('the updater can\'t unpack (13th write fails)', '1.5', sound, VPK, HEAD, 13, 0, 0),
    ('the system won\'t install the new version', '1.5', sound, VPK, HEAD, 0, 2, 0),
]
blobs = {}


def blob(data):
    key = hashlib.sha256(data).hexdigest()[:12]
    blobs[key] = data
    return 'blob_' + key


rows = []
for name, have, answer, vpk, head, fail_write, fail_install, tamper in cases:
    rows.append('\t{"%s", "%s", %s, %d, %s, %d, %s, %d, %d, %d, %d},' % (
        name.replace('"', ''), have, blob(answer + b'\0'), len(answer), blob(vpk), len(vpk),
        blob(head + b'\0'), len(head), fail_write, fail_install, tamper))
with open('cases.h', 'w', newline='\n') as f:
    f.write('typedef struct { const char *name, *have; const unsigned char *json; unsigned json_len; '
            'const unsigned char *vpk; unsigned vpk_len; const unsigned char *head; unsigned head_len; '
            'int fail_write_at, fail_install_at, tamper; } Case;\n')
    for key, data in blobs.items():
        f.write('static const unsigned char blob_%s[] = {%s};\n' % (key, ','.join(str(b) for b in data)))
    f.write('#define CASE_COUNT %d\nstatic const Case cases[CASE_COUNT] = {\n%s\n};\n' % (len(cases), '\n'.join(rows)))
# what a sound update must hand the system's installer
z = zipfile.ZipFile(io.BytesIO(VPK))
with open('expect_files.txt', 'w', newline='\n') as f:
    for i in z.infolist():
        f.write('%s %d %s\n' % (i.filename, i.file_size, hashlib.sha256(z.read(i)).hexdigest()))
    f.write('VPK %d %s\n' % (len(VPK), hashlib.sha256(VPK).hexdigest()))
    f.write('HEAD %d %s\n' % (len(HEAD), hashlib.sha256(HEAD).hexdigest()))
    f.write('UPDATER_HEAD %d %s\n' % (len(HEAD), hashlib.sha256(head_bin('ENGR00002')).hexdigest()))
print('wrote', len(cases), 'cases')
