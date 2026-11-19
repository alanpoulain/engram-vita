# Makes cases.h for harness2.c: GitHub's real answer for the latest release,
# and variations of it, each with the VPK the stand-in network hands out.
import copy
import hashlib
import json
import urllib.request

VPK = open(r'E:\PS Vita Claude\exports\engram_installer.vpk', 'rb').read()
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


damaged = bytearray(VPK)
damaged[len(damaged) // 2] ^= 0x40
# a VPK that is a sound zip but another app
import io
import zipfile
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

cases = [  # name, have, answer (bytes), vpk, fail_write_at
    ('the real answer, same version', '1.5', raw, VPK, 0),
    ('the real answer, older installer (the release has no VPK)', '1.4', raw, VPK, 0),
    ('1.6 with an installer, compact JSON', '1.5', json.dumps(with_installer('1.6'), separators=(',', ':')).encode(), VPK, 0),
    ('1.6 with an installer listed first, indented JSON', '1.5', json.dumps(with_installer('1.6', first=True), indent=2).encode(), VPK, 0),
    ('1.10 against 1.9', '1.9', json.dumps(with_installer('1.10')).encode(), VPK, 0),
    ('v1.5.1 against 1.5', '1.5', json.dumps(with_installer('v1.5.1')).encode(), VPK, 0),
    ('1.5 against 1.5.0 (the same)', '1.5.0', json.dumps(other_tag('1.5')).encode(), VPK, 0),
    ('an older release than the installer', '1.6', json.dumps(other_tag('1.5')).encode(), VPK, 0),
    ('a tag that is no version', '1.5', json.dumps(other_tag('nightly')).encode(), VPK, 0),
    ('no digest from GitHub', '1.5', json.dumps(with_installer('1.6', digest='')).encode(), VPK, 0),
    ('a download link somewhere else', '1.5', json.dumps(with_installer('1.6', url='https://example.com/engram_installer.vpk')).encode(), VPK, 0),
    ('the download is damaged', '1.5', json.dumps(with_installer('1.6')).encode(), bytes(damaged), 0),
    ('the download is another app (digest matches)', '1.5', json.dumps(with_installer('1.6', vpk=foreign)).encode(), foreign, 0),
    ('a file name that leaves the folder (digest matches)', '1.5', json.dumps(with_installer('1.6', vpk=escaping)).encode(), escaping, 0),
    ('the app folder stops taking writes (4th write fails)', '1.5', json.dumps(with_installer('1.6')).encode(), VPK, 4),
    ('not JSON at all', '1.5', b'<html>Sign in to the hotel wifi</html>', VPK, 0),
]
blobs = {}


def blob(data):
    key = hashlib.sha256(data).hexdigest()[:12]
    blobs[key] = data
    return 'blob_' + key


rows = []
for name, have, answer, vpk, fail in cases:
    rows.append('\t{"%s", "%s", %s, %d, %s, %d, %d},' % (name, have, blob(answer + b'\0'), len(answer), blob(vpk), len(vpk), fail))
with open('cases.h', 'w', newline='\n') as f:
    f.write('typedef struct { const char *name, *have; const unsigned char *json; unsigned json_len; const unsigned char *vpk; unsigned vpk_len; int fail_write_at; } Case;\n')
    for key, data in blobs.items():
        f.write('static const unsigned char blob_%s[] = {%s};\n' % (key, ','.join(str(b) for b in data)))
    f.write('#define CASE_COUNT %d\nstatic const Case cases[CASE_COUNT] = {\n%s\n};\n' % (len(cases), '\n'.join(rows)))
# what a sound install must write
z = zipfile.ZipFile(io.BytesIO(VPK))
with open('expect_files.txt', 'w', newline='\n') as f:
    for i in z.infolist():
        f.write('%s %d %s\n' % (i.filename, i.file_size, hashlib.sha256(z.read(i)).hexdigest()))
    f.write('VPK %d %s\n' % (len(VPK), hashlib.sha256(VPK).hexdigest()))
print('wrote', len(cases), 'cases')
