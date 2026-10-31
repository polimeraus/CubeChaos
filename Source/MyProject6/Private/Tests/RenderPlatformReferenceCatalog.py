"""Draw the actual persisted Unreal outlines for side-by-side silhouette review (Pillow)."""
import json
import re
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

root = Path(__file__).resolve().parents[4]
out = root / 'Saved/ParametricPlatform/ReferenceShapes'
records = json.loads((out / 'Persistence.json').read_text())
scale = 2
cols, cell_w, cell_h = 6, 320, 340
rows = (len(records)+cols-1)//cols
canvas = Image.new('RGB',(cols*cell_w*scale,(rows*cell_h+100)*scale),'#16242b')
draw = ImageDraw.Draw(canvas)
font = ImageFont.truetype('C:/Windows/Fonts/arial.ttf',17*scale)
title = ImageFont.truetype('C:/Windows/Fonts/arialbd.ttf',29*scale)
draw.text((28*scale,25*scale),'REFERENCE SHAPES / 26',font=title,fill='#e9f0eb')
colors = ['#ed9c32','#5bb8d0','#e7849b','#70b99d','#a99aca','#d8c58b']
for index,record in enumerate(records):
    col,row = index%cols,index//cols
    origin = ((col+.5)*cell_w,(row+.5)*cell_h+85)
    width,height = record['size']
    zoom = min(250/width,235/height)
    def points(loop):
        return [((origin[0]+x*zoom)*scale,(origin[1]-y*zoom)*scale) for x,y in loop]
    draw.polygon(points(record['outline']),fill=colors[index%len(colors)])
    for hole in record['holes']: draw.polygon(points(hole),fill='#16242b')
    label = f'{index+1:02d}  '+re.sub(r'(?<=[a-z])(?=[A-Z])',' ',record['name'])
    draw.text(((col*cell_w+18)*scale,((row+1)*cell_h+43)*scale),label,font=font,fill='#e9f0eb')
canvas.resize((canvas.width//scale,canvas.height//scale),Image.Resampling.LANCZOS).save(out/'ReferenceCatalog.png')
print(out/'ReferenceCatalog.png')
