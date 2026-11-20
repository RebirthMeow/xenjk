"""package_np.py <builddir> <model> <pk3 path> <gla>  (the stock-skeleton package.py + the model GLA at
models/players/_humanoid/<model>.gla, the path the GLM references; local research pk3 only, never install/commit/upload)
  -- builddir has model.glm, build_report.json and tex/ ; writes skin/shader/sounds/icon and the pk3"""
from pathlib import Path
REPO = Path(__file__).resolve().parents[4]   # repository root (tools/cloth/pipeline/<stage>/x.py)
import sys, os, json, zipfile, shutil
b, model, pk3 = sys.argv[1:4]
W = str(REPO / '.codex-research' / 'eve2b-glm-20261012')
rep = json.load(open(os.path.join(b, 'build_report.json')))
root = os.path.join(b, 'pkg'); shutil.rmtree(root, ignore_errors=True)
mdir = os.path.join(root, 'models', 'players', model); os.makedirs(mdir)
os.makedirs(os.path.join(root, 'shaders'))
shutil.copy(os.path.join(b, 'model.glm'), mdir)
hdir = os.path.join(root, 'models', 'players', '_humanoid'); os.makedirs(hdir)
shutil.copy(sys.argv[4], os.path.join(hdir, model + '.gla'))
for f in os.listdir(os.path.join(b, 'tex')): shutil.copy(os.path.join(b, 'tex', f), mdir)
lines = []
for s in rep['surfaces']:
    if s['flags'] & 3 or not s['shader']: continue
    lines.append(f"{s['name']},{s['shader']}\n")
skin = ''.join(lines).replace('\n', '\r\n')
for n in ('default', 'red', 'blue'):
    open(os.path.join(mdir, f'model_{n}.skin'), 'w', newline='').write(skin)
P = f'models/players/{model}/'
sh = f'''{P}skirt_2s
{{
	cull none
	{{
		map {P}skirt.jpg
		rgbGen lightingDiffuse
	}}
}}

{P}hair
{{
	cull none
	{{
		map {P}hair.tga
		alphaFunc GE128
		depthWrite
		rgbGen lightingDiffuse
	}}
}}

{P}feather
{{
	cull none
	{{
		map {P}feather.tga
		alphaFunc GE128
		depthWrite
		rgbGen lightingDiffuse
	}}
}}

{P}brow
{{
	cull none
	polygonOffset
	{{
		map {P}brow.tga
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen lightingDiffuse
	}}
}}

{P}glass
{{
	cull none
	{{
		map {P}glass.tga
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen lightingDiffuse
	}}
}}
'''
open(os.path.join(root, 'shaders', f'{model}.shader'), 'w', newline='').write(sh.replace('\n', '\r\n'))
shutil.copy(os.path.join(W, 'src', 'stock', 'models', 'players', 'jan', 'sounds.cfg'), mdir)
icon = os.path.join(b, 'icon_default.jpg')
if os.path.exists(icon):
    for n in ('default', 'red', 'blue'): shutil.copy(icon, os.path.join(mdir, f'icon_{n}.jpg'))
if os.path.exists(pk3): os.remove(pk3)
with zipfile.ZipFile(pk3, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as z:
    for dp, dn, fn in os.walk(root):
        for f in fn:
            full = os.path.join(dp, f)
            z.write(full, os.path.relpath(full, root).replace('\\', '/'))
print(pk3, os.path.getsize(pk3) // 1024, 'KB', len(lines), 'skin lines')
