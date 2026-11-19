# Makes installer-preview.html: page.template.html with liquid.js put in.
import os

here = os.path.dirname(os.path.abspath(__file__))
page = open(os.path.join(here, 'page.template.html'), encoding='utf-8').read()
liquid = open(os.path.join(here, 'liquid.js'), encoding='utf-8').read()
assert '/*LIQUID_JS*/' in page
out = page.replace('/*LIQUID_JS*/', liquid)
# the other projects' photos (make_photos.py); without them the window shows empty frames
photos = os.path.join(here, 'photos.js')
out = out.replace('/*PROJECT_PHOTOS*/', open(photos, encoding='utf-8').read() if os.path.exists(photos) else '')
open(os.path.join(here, 'installer-preview.html'), 'w', encoding='utf-8', newline='\n').write(out)
# the scripts on their own, for a syntax check
parts = out.split('<script>')[1:]
open(os.path.join(here, 'page_scripts.js'), 'w', encoding='utf-8', newline='\n').write(
    '\n'.join(p.split('</script>')[0] for p in parts))
print('wrote installer-preview.html,', len(out), 'bytes')
