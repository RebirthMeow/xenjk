from pathlib import Path
REPO = Path(__file__).resolve().parents[4]   # repository root (tools/cloth/pipeline/<stage>/x.py)
import zipfile,sys,os,fnmatch
G=os.path.join(str(REPO / 'game_directory' / 'Jedi Academy' / 'GameData'), 'base')
out=os.path.join(str(REPO / '.codex-research' / 'eve2b-glm-20261012'), 'src', 'stock')
pats=["models/players/_humanoid/*","models/players/jan/*","models/players/kyle/*"]
for pk in ["assets0.pk3","assets1.pk3","assets2.pk3","assets3.pk3"]:
    z=zipfile.ZipFile(os.path.join(G,pk))
    for n in z.namelist():
        if any(fnmatch.fnmatch(n.lower(),p) for p in pats):
            z.extract(n,out); print(pk,n)
