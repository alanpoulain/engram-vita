# Makes, from the other projects' own pictures on GitHub:
#   icons/<project>_<size>.rgb   for the app: the project's bubble picture at
#                                the sizes its emblem is drawn at, as pixels of
#                                red, green and blue (the app cuts the circle)
#   preview/photos.js            for the preview page: the screenshots made the
#                                size the app shows them, and the icon, as data
#                                URIs (a published page can't load pictures
#                                from elsewhere)
# The app itself gets the screenshots from GitHub as it runs (live.c).
import base64
import io
import os
import urllib.request

from PIL import Image

RAW = 'https://raw.githubusercontent.com/chazzarchibald-ops/'
PHOTOS = {
    'tweaks': ['vita-tweaks-plugins/main/screenshots/settings-top-menu.png',
               'vita-tweaks-plugins/main/screenshots/tweaks-plugins.png'],
    'cyberflow': ['CyberFLow-Launcher/main/Media/Screenshots/start_menu.png',
                  'CyberFLow-Launcher/main/Media/Screenshots/game_info.png',
                  'CyberFLow-Launcher/main/Media/Screenshots/settings_menu.png',
                  'CyberFLow-Launcher/main/Media/Screenshots/scan_settings.png',
                  'CyberFLow-Launcher/main/Media/Screenshots/theme_menu.png'],
}
# The projects that have a bubble, and where its picture is.
ICONS = {'cyberflow': 'CyberFLow-Launcher/HEAD/sce_sys/icon0.png'}
ICON_SIZES = (48, 34)
SIZE = (448, 254)
here = os.path.dirname(os.path.abspath(__file__))  # (the installer's folder)


def fetch(path):
    request = urllib.request.Request(RAW + path, headers={'User-Agent': 'EngramInstaller'})
    return Image.open(io.BytesIO(urllib.request.urlopen(request, timeout=60).read())).convert('RGB')


def uri(im, kind, **options):
    out = io.BytesIO()
    im.save(out, kind, **options)
    return 'data:image/%s;base64,%s' % (kind.lower(), base64.b64encode(out.getvalue()).decode())


photos = []
for key, paths in PHOTOS.items():
    uris = [uri(fetch(path).resize(SIZE, Image.LANCZOS), 'JPEG', quality=84) for path in paths]
    print(key, len(uris), 'photos')
    photos.append('\t%s: [\n%s\n\t],' % (key, '\n'.join("\t\t'%s'," % u for u in uris)))
icons = []
os.makedirs(os.path.join(here, 'icons'), exist_ok=True)
for key, path in ICONS.items():
    im = fetch(path)
    for size in ICON_SIZES:
        small = im.resize((size, size), Image.LANCZOS)
        open(os.path.join(here, 'icons', '%s_%d.rgb' % (key, size)), 'wb').write(small.tobytes())
    icons.append("\t%s: '%s'," % (key, uri(im.resize((96, 96), Image.LANCZOS), 'PNG')))
    print(key, 'icon', im.size)
open(os.path.join(here, 'preview', 'photos.js'), 'w', encoding='utf-8', newline='\n').write(
    '// made by ../make_photos.py\nconst PHOTO_URIS = {\n%s\n};\nconst ICON_URIS = {\n%s\n};\n' % (
        '\n'.join(photos), '\n'.join(icons)))
print('wrote icons/ and preview/photos.js')
