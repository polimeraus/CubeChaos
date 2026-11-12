"""Draw a scaled plan of real, freshly reloaded Unreal platform outlines (no invented geometry)."""
import json
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

out = Path(__file__).resolve().parents[4] / 'Saved/PlatformCity'
layouts = json.loads((out / 'Persistence.json').read_text())['layouts']
scale = 2
image = Image.new('RGB', (2100 * scale, 700 * scale), '#101b25')
draw = ImageDraw.Draw(image)
font = ImageFont.truetype('C:/Windows/Fonts/arial.ttf', 20*scale)
title = ImageFont.truetype('C:/Windows/Fonts/arialbd.ttf', 31*scale)
small = ImageFont.truetype('C:/Windows/Fonts/arial.ttf', 17*scale)
draw.text((32*scale, 24*scale), 'DİNAMİK ŞEHİR PLAKALARI', font=title, fill='#eef4f1')
draw.text((32*scale, 71*scale), '8000 × 5000  ·  3 dikey + 2 yatay ana yol  ·  Ana yol 600 / ara yol 180 cm', font=font, fill='#aabcc7')
colors = ['#5c9fb0', '#8ba677', '#c28f62', '#9a83b4']
for index, layout in enumerate(layouts):
    zoom = 630 / 8000
    ox, oy = 350 + index*700, 365
    def xy(x, y):
        return ((ox + x*zoom)*scale, (oy-y*zoom)*scale)
    draw.rectangle([xy(-4000, 2500), xy(4000, -2500)], outline='#41535f', width=scale)
    for road in layout['roads']:
        draw.polygon([xy(x,y) for x,y in road['outline']], fill='#066d8c')
    for plate in layout['plates']:
        district = int(plate['label'].split('_')[1])-1
        poly = [xy(x,y) for x,y in plate['outline']]
        draw.polygon(poly, fill='#e5ebea')
        draw.line(poly+[poly[0]], fill='#d1dfd9', width=scale)
    draw.text(((index*700+35)*scale, 594*scale), f"Seed {layout['seed']}  ·  {len(layout['plates'])} plaka", font=font, fill='#eef4f1')
draw.text((32*scale, 653*scale), 'Beyaz: dinamik plakalar. Mavi: ortak yol ağından ayrılan boş koridorlar (şematik gösterim).', font=small, fill='#aabcc7')
image.resize((2100,700), Image.Resampling.LANCZOS).save(out/'Layouts.png')
print(out/'Layouts.png')
