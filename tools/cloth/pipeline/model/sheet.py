import sys
from PIL import Image
out=sys.argv[1]; names=sys.argv[2:]
ims=[Image.open(n).resize((640,360)) for n in names]
cols=2; rows=(len(ims)+1)//2
sheet=Image.new('RGB',(1280,360*rows))
for i,im in enumerate(ims): sheet.paste(im,((i%2)*640,(i//2)*360))
sheet.save(out,quality=88)
