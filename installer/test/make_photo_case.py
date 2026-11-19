# Makes photo_case.h for photo_harness.c: PNGs of every kind png.c reads, two
# real screenshots, some it must refuse, and GitHub's real answers about the
# two projects (their newest release, and the folder their screenshots are in).
#   python make_photo_case.py            writes photo_case.h and photo_expect.txt
#   python make_photo_case.py --check    reads the harness's output on stdin
import hashlib
import io
import json
import random
import sys
import urllib.request

from PIL import Image

UA = {'User-Agent': 'EngramInstaller', 'Accept': 'application/vnd.github+json'}
FW, FH, FILL = 448, 254, (0x18, 0x1C, 0x2C)
PROJECTS = [
    ('tweaks', 'chazzarchibald-ops/vita-tweaks-plugins', 'screenshots', ['tweaks.suprx', '*.vpk']),
    ('cyberflow', 'chazzarchibald-ops/CyberFLow-Launcher', 'Media/Screenshots', ['*.vpk', 'CyberFlow_data.zip', 'tweaks.suprx']),
]


def get(url):
    return urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=60).read()


def fit(im):
    """photo_fit, again in Python: the same whole-number sums."""
    w, h = im.size
    src = im.tobytes()
    ow, oh = FW, (h * FW + w // 2) // w
    if oh > FH:
        oh, ow = FH, (w * FH + h // 2) // h
    ow, oh = max(ow, 1), max(oh, 1)
    ox, oy = (FW - ow) // 2, (FH - oh) // 2
    out = bytearray(bytes(FILL) * (FW * FH))
    for y in range(oh):
        y0, y1 = y * h // oh, (y + 1) * h // oh
        y1 = max(y1, y0 + 1)
        for x in range(ow):
            x0, x1 = x * w // ow, (x + 1) * w // ow
            x1 = max(x1, x0 + 1)
            n = (x1 - x0) * (y1 - y0)
            s = [0, 0, 0]
            for sy in range(y0, y1):
                row = src[(sy * w + x0) * 3:(sy * w + x1) * 3]
                s[0] += sum(row[0::3])
                s[1] += sum(row[1::3])
                s[2] += sum(row[2::3])
            at = ((oy + y) * FW + ox + x) * 3
            out[at:at + 3] = bytes((c + n // 2) // n for c in s)
    return bytes(out)


def expected(png):
    im = Image.open(io.BytesIO(png))
    rgb = im.convert('RGBA').convert('RGB') if im.mode in ('P', 'LA', 'PA') else im.convert('RGB')
    if im.mode == 'P':  # the palette's colours as they are, whatever its transparency says
        rgb = im.convert('RGB')
    return rgb


if '--check' in sys.argv:
    got = [l.rstrip() for l in sys.stdin.read().splitlines()]
    want = [l.rstrip() for l in open('photo_expect.txt', encoding='utf-8').read().splitlines()]
    bad = [(g, w) for g, w in zip(got, want) if g != w]
    print(len(got), 'lines from the harness,', len(want), 'expected,', len(bad), 'differ')
    for g, w in bad[:8]:
        print('  got ', g)
        print('  want', w)
    ok = not bad and len(got) == len(want)
    print('PASS' if ok else 'FAIL')
    sys.exit(0 if ok else 1)

random.seed(448254)


def noise(w, h, mode):
    bands = {'RGB': 3, 'RGBA': 4, 'L': 1, 'LA': 2}[mode]
    smooth = bytearray()
    for y in range(h):  # partly smooth, so every kind of row filter gets chosen
        for x in range(w):
            for b in range(bands):
                smooth.append((x * 3 + y * 5 + b * 40 + random.randrange(24 if (y // 8) % 2 else 256)) & 255)
    return Image.frombytes(mode, (w, h), bytes(smooth))


def png_of(im, **kw):
    b = io.BytesIO()
    im.save(b, 'PNG', **kw)
    return b.getvalue()


pngs = []
for name, path in (('real_tweaks', 'chazzarchibald-ops/vita-tweaks-plugins/main/screenshots/tweaks-plugins.png'),
                   ('real_cyberflow', 'chazzarchibald-ops/CyberFLow-Launcher/main/Media/Screenshots/settings_menu.png')):
    pngs.append((name, get('https://raw.githubusercontent.com/' + path)))
pngs.append(('rgb_wide', png_of(noise(301, 97, 'RGB'))))
pngs.append(('rgba_tall', png_of(noise(64, 211, 'RGBA'))))
pngs.append(('grey', png_of(noise(120, 68, 'L'))))
pngs.append(('grey_alpha', png_of(noise(50, 50, 'LA'))))
pngs.append(('palette', png_of(noise(200, 113, 'RGB').quantize(64))))
pngs.append(('tiny', png_of(noise(3, 2, 'RGB'))))
pngs.append(('exact', png_of(noise(448, 254, 'RGB'), compress_level=1)))
pngs.append(('stored', png_of(noise(90, 40, 'RGB'), compress_level=0)))
bad = bytearray(pngs[2][1])
bad[len(bad) // 2] ^= 0x20
pngs.append(('damaged', bytes(bad)))
pngs.append(('cut_short', pngs[2][1][:len(pngs[2][1]) // 2]))
pngs.append(('interlaced', None))
pngs.append(('sixteen_bit', png_of(Image.frombytes('I;16', (20, 10), bytes(400)))))
pngs.append(('not_a_png', b'GIF89a' + bytes(60)))
pngs.append(('too_big', png_of(Image.new('RGB', (2100, 2100), (1, 2, 3)))))
# an interlaced one, which PIL can't write: a real file's header with the flag set
header = bytearray(pngs[2][1])
header[28] = 1
pngs[12] = ('interlaced', bytes(header))

answers = []
expect = []

LATIN = ('AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty'
         'AaAaAaCcCcCcCcDdDdEeEeEeEeEeGgGgGgGgHhHhIiIiIiIiIiIiJjKkkLlLlLlLlLlNnNnNnnNnOoOoOoOoRrRrRrSsSsSsSsTtTtTt'
         'UuUuUuUuUuUuWwYyYZzZzZzs')
OTHERS = {0xA0: ' ', 0x2018: "'", 0x2019: "'", 0x201C: '"', 0x201D: '"', 0x2013: '-', 0x2014: '-', 0x2022: '-',
          0x2026: '...', 0x2192: '->', 0x2190: '<-'}


def plain_char(code):
    if 0xC0 <= code < 0x180:
        return LATIN[code - 0xC0].encode()
    return OTHERS.get(code, '').encode()


def letter(b):
    return 0x41 <= b <= 0x5A or 0x61 <= b <= 0x7A


def plain_line(s, size=1608):
    """lookup.c's plain_line: one line of notes with its markdown taken off."""
    out, i, n = bytearray(), 0, len(s)
    while i < n and len(out) < size - 4:
        c = s[i]
        if c == 0x60:
            i += 1
        elif c == 0x2A and i + 1 < n and s[i + 1] == 0x2A:
            i += 2
        elif c == 0x21 and i + 1 < n and s[i + 1] == 0x5B:
            close = i + 2
            while close < n and s[close] != 0x5D:
                close += 1
            end = close + 1
            if close < n and end < n and s[end] == 0x28:
                while end < n and s[end] != 0x29:
                    end += 1
                i = end + 1 if end < n else n
            else:
                out.append(0x21)
                i += 1
        elif c == 0x5B:
            close = i + 1
            while close < n and s[close] != 0x5D:
                close += 1
            end = close + 1
            if close < n and end < n and s[end] == 0x28:
                while end < n and s[end] != 0x29:
                    end += 1
            else:
                end = n
            if end < n:
                for k in range(i + 1, close):
                    if len(out) >= size - 4:
                        break
                    if s[k] not in (0x60, 0x2A) and s[k] < 0x80:
                        out.append(s[k])
                i = end + 1
            else:
                out.append(0x5B)
                i += 1
        elif c == 0x3C and i + 1 < n and (s[i + 1] == 0x2F or letter(s[i + 1])):
            end = i + 1
            while end < n and s[end] != 0x3E:
                end += 1
            if end < n:
                i = end + 1
            else:
                out.append(0x3C)
                i += 1
        elif c >= 0x80:
            count = 4 if c >= 0xF0 else 3 if c >= 0xE0 else 2 if c >= 0xC0 else 1
            code = c & 7 if count == 4 else c & 15 if count == 3 else c & 31 if count == 2 else 0
            for k in range(1, count):
                if i + k < n:
                    code = (code << 6) | (s[i + k] & 0x3F)
            if count > 1:
                out += plain_char(code)
            i += count
        elif c < 0x20:
            out.append(0x20)
            i += 1
        else:
            out.append(c)
            i += 1
    while out and out[-1] == 0x20:
        out.pop()
    return bytes(out)


def plain_notes(notes, size=6144):
    """lookup.c's plain_notes: the notes as plain lines, \\1 before a heading and \\2 before an item."""
    out, fenced, gap, p = bytearray(), False, False, 0
    while p < len(notes):
        end = p
        while end < len(notes) and notes[end] not in (0x0A, 0x0D):
            end += 1
        s = notes[p:end]
        p = end + 2 if notes[end:end + 2] == b'\r\n' else end + 1 if end < len(notes) else end
        s = s.strip(b' \t')
        if s[:3] == b'```':
            fenced = not fenced
            continue
        mark = 0
        if not fenced and s[:1] == b'#':
            s = s.lstrip(b'#').lstrip(b' ')
            mark = 1
        elif not fenced and len(s) >= 2 and s[:1] in (b'*', b'-', b'+') and s[1:2] == b' ':
            s = s[2:].lstrip(b' ')
            mark = 2
        had = len(s) > 0
        s = s[:1592]
        line = bytes(c for c in s if 0x20 <= c < 0x80) if fenced else plain_line(s)
        if not line:
            if not had and out:
                gap = True
            continue
        need = len(line) + (1 if mark else 0) + (1 if gap else 0) + (1 if out else 0)
        if len(out) + need >= size - 5:
            out += b'\n...'
            return bytes(out)
        if out:
            out.append(0x0A)
        if gap:
            out.append(0x0A)
        gap = False
        if mark:
            out.append(mark)
        out += line
    return bytes(out)

for name, data in pngs:
    try:
        rgb = expected(data)
        refused = name in ('damaged', 'cut_short', 'interlaced', 'sixteen_bit', 'not_a_png', 'too_big')
    except Exception:
        refused = True
    if refused:
        expect.append('PNG %s REFUSED' % name)
        continue
    expect.append('PNG %s %d %d %s' % (name, rgb.size[0], rgb.size[1], hashlib.sha256(rgb.tobytes()).hexdigest()))
    expect.append('FIT %s %s' % (name, hashlib.sha256(fit(rgb)).hexdigest()))

MONTHS = 'Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec'.split()


def size_text(n):
    if n < 1048576:
        return '%d KB' % ((n + 1023) // 1024)
    if n < 100 * 1048576:
        t = (n + 52429) // 104858
        return '%d.%d MB' % (t // 10, t % 10)
    return '%d MB' % ((n + 524288) // 1048576)


for key, repo, folder, assets in PROJECTS:
    raw = get('https://api.github.com/repos/%s/contents/%s' % (repo, folder))
    answers.append((0, key + '_shots', repo, raw, []))
    shots = [f for f in json.loads(raw) if f['type'] == 'file' and f['name'].lower().endswith('.png')][:8]
    expect.append('SHOTS %s_shots %d' % (key, len(shots)))
    for f in shots:
        expect.append('  shot %s %d %s' % (f['sha'], f['size'], f['download_url']))
    raw = get('https://api.github.com/repos/%s/releases/latest' % repo)
    rel = json.loads(raw)
    answers.append((1, key + '_release', repo, raw, assets))
    plain = ''.join(c if ord(c) < 128 else '-' for c in (rel['name'] or rel['tag_name']) if ord(c) >= 32)[:63]
    y, m, d = rel['published_at'][:10].split('-')
    expect.append('RELEASE %s_release ok|%s|%d %s %s' % (key, plain, int(d), MONTHS[int(m) - 1], y))
    for want in assets:
        found = [a for a in rel['assets'] if (a['name'].endswith(want[1:]) and len(a['name']) > len(want) - 1 if want[0] == '*' else a['name'] == want)]
        if found:
            a = found[0]
            expect.append('  asset %s %s %d %s %s' % (want, a['name'], a['size'], size_text(a['size']), a['digest'][7:]))
        else:
            expect.append('  asset %s NONE' % want)
# every release of a project, with its notes
for key, repo, assets in (('tweaks', PROJECTS[0][1], ['tweaks.suprx']), ('cyberflow', PROJECTS[1][1], ['*.vpk', 'CyberFlow_data.zip']),
                          ('engram', 'chazzarchibald-ops/engram-vita', ['engram_installer.vpk', 'engram.suprx'])):
    raw = get('https://api.github.com/repos/%s/releases?per_page=8' % repo)
    answers.append((2, key + '_releases', repo, raw, assets))
    rels = [r for r in json.loads(raw) if not r['draft']][:8]
    expect.append('RELEASES %s_releases %d' % (key, len(rels)))
    for rel in rels:
        plain = ''.join(c if ord(c) < 128 else '-' for c in (rel['name'] or rel['tag_name']) if ord(c) >= 32)[:63]
        y, m, d = rel['published_at'][:10].split('-')
        sizes = []
        for want in (assets + [None])[:2]:
            found = [a for a in rel['assets'] if want and (a['name'].endswith(want[1:]) and len(a['name']) > len(want) - 1 if want[0] == '*' else a['name'] == want)]
            sizes.append(size_text(found[0]['size']) if found else '')
        notes = plain_notes((rel['body'] or '').encode('utf-8')[:24 * 1024 - 1])
        expect.append('  rel %s|%s|%d %s %s|%s|%s|%s|%d|%s' % (
            rel['tag_name'], plain, int(d), MONTHS[int(m) - 1], y, 'pre' if rel['prerelease'] else 'full', sizes[0], sizes[1],
            len(notes), hashlib.sha256(notes).hexdigest()))
        if key == 'engram' and rel is rels[0]:
            open('notes_sample.txt', 'wb').write(notes)
answers.append((2, 'error_releases', PROJECTS[0][1], b'{"message":"API rate limit exceeded"}', ['tweaks.suprx']))
expect.append('RELEASES error_releases -1')
# answers that aren't what was asked for
answers.append((0, 'error_shots', PROJECTS[0][1], b'{"message":"Not Found","status":"404"}', []))
expect.append('SHOTS error_shots -1')
foreign = json.loads(get('https://api.github.com/repos/%s/contents/%s' % (PROJECTS[0][1], PROJECTS[0][2])))
for f in foreign:
    f['download_url'] = f['download_url'].replace('raw.githubusercontent.com', 'example.com')
answers.append((0, 'foreign_shots', PROJECTS[0][1], json.dumps(foreign).encode(), []))
expect.append('SHOTS foreign_shots 0')
answers.append((1, 'error_release', PROJECTS[0][1], b'<html>Sign in to the hotel wifi</html>', ['tweaks.suprx']))
expect.append('RELEASE error_release REFUSED||')
expect.append('  asset tweaks.suprx NONE')
for n in (1, 1023, 1024, 18388, 1048575, 1048576, 15340487, 104857599, 104857600, 442193162):
    expect.append('SIZE %d %s' % (n, size_text(n)))

with open('photo_case.h', 'w', newline='\n') as f:
    f.write('typedef struct { const char *name; const unsigned char *data; unsigned len; } Png;\n')
    for i, (name, data) in enumerate(pngs):
        f.write('static const unsigned char png_%d[] = {%s};\n' % (i, ','.join(str(b) for b in data)))
    f.write('#define PNG_COUNT %d\nstatic const Png pngs[PNG_COUNT] = {\n%s\n};\n' % (
        len(pngs), '\n'.join('\t{"%s", png_%d, %d},' % (n, i, len(d)) for i, (n, d) in enumerate(pngs))))
    f.write('typedef struct { int kind; const char *name, *repo, *text; const char *assets[4]; } Answer;\n')
    for i, (kind, name, repo, raw, assets) in enumerate(answers):
        f.write('static const char answer_%d[] = {%s,0};\n' % (i, ','.join(str(b if b < 128 else b - 256) for b in raw)))
    f.write('#define ANSWER_COUNT %d\nstatic const Answer answers[ANSWER_COUNT] = {\n' % len(answers))
    for i, (kind, name, repo, raw, assets) in enumerate(answers):
        f.write('\t{%d, "%s", "%s", answer_%d, {%s0}},\n' % (kind, name, repo, i, ''.join('"%s", ' % a for a in assets)))
    f.write('};\n')
open('photo_expect.txt', 'w', encoding='utf-8', newline='\n').write('\n'.join(expect) + '\n')
print(len(pngs), 'PNGs,', len(answers), 'answers,', len(expect), 'lines expected')
