"""Labeled silhouette preview from geometry exported by a fresh Unreal process."""
import json
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

out=Path(__file__).resolve().parents[4]/'Saved/ParametricPlatform/ParametricShape'
records=json.loads((out/'Persistence.json').read_text())
scale=2
width,height=1800,1420
canvas=Image.new('RGB',(width*scale,height*scale),'#15232d')
draw=ImageDraw.Draw(canvas)
title=ImageFont.truetype('C:/Windows/Fonts/arialbd.ttf',32*scale)
font=ImageFont.truetype('C:/Windows/Fonts/arial.ttf',22*scale)
small=ImageFont.truetype('C:/Windows/Fonts/arial.ttf',18*scale)
draw.text((35*scale,27*scale),'PARAMETRIC SHAPE',font=title,fill='#f2eee5')
draw.text((35*scale,73*scale),'Merkez daire · kıvrımlı ve daralan kollar · dairesel uç platformlar',font=small,fill='#aebfc8')
labels=['Y düzeni · 3 kol','Çift kapı · 2 kol','5 kıvrımlı kol','Ahtapot · 8 kol','Kol başına farklı ayarlar','S kıvrımları']
colors=['#b487c9','#6faec8','#64b4a1','#e6a45f','#d891a0','#8b9ece']
for index,record in enumerate(records):
    xs=[p[0] for p in record['outline']];ys=[p[1] for p in record['outline']]
    cx,cy=(min(xs)+max(xs))/2,(min(ys)+max(ys))/2
    zoom=min(500/(max(xs)-min(xs)),470/(max(ys)-min(ys)))
    col,row=index%3,index//3
    ox,oy=col*600+300,row*640+365
    def points(loop): return [((ox+(x-cx)*zoom)*scale,(oy-(y-cy)*zoom)*scale) for x,y in loop]
    draw.polygon(points(record['outline']),fill=colors[index])
    for hole in record['holes']: draw.polygon(points(hole),fill='#15232d')
    draw.text(((col*600+35)*scale,(row*640+625)*scale),labels[index],font=font,fill='#f2eee5')
    s=record['settings'];arm=s['default_arm']
    line=f"Merkez {s['center_radius']:.0f} cm  /  Kol {arm['length']:.0f} cm"
    draw.text(((col*600+35)*scale,(row*640+660)*scale),line,font=small,fill='#aebfc8')
canvas.resize((width,height),Image.Resampling.LANCZOS).save(out/'ParametricShapeCatalog.png')
print(out/'ParametricShapeCatalog.png')
