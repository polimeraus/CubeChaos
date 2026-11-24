"""Build the blue arcade railgun. The editable source uses centimetres in grip space."""
import math
from pathlib import Path

import bpy
from mathutils import Matrix, Vector

out = Path(__file__).resolve().parents[3] / 'ContentSource/Railgun'
out.mkdir(parents=True, exist_ok=True)
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
parts = []
tau = math.tau


def finish(name, rgb, metal, rough, glow=0.0):
    mat = bpy.data.materials.new(name)
    mat.diffuse_color = (*rgb, 1)
    mat.use_nodes = True
    shader = mat.node_tree.nodes.get('Principled BSDF')
    for key, value in [('Base Color', mat.diffuse_color), ('Metallic', metal), ('Roughness', rough),
                       ('Emission Color', mat.diffuse_color), ('Emission Strength', glow)]:
        shader.inputs[key].default_value = value
    return mat


cobalt = finish('RailCobalt', (.022, .135, .57), .34, .25)
ice = finish('RailPorcelain', (.62, .85, .93), .16, .29)
chrome = finish('RailSilver', (.64, .75, .82), .8, .23)
navy = finish('RailGunmetal', (.035, .055, .08), .55, .35)
rubber = finish('RailRubber', (.023, .033, .042), .08, .54)
amber = finish('RailBrass', (.66, .29, .068), .68, .26)
cyan = finish('RailEnergy', (.018, .69, .99), .16, .23, 1.7)
glass = finish('RailGauge', (.018, .08, .095), .2, .25)


def mesh(name, vertices, faces, mat, smooth=True):
    data = bpy.data.meshes.new(name)
    data.from_pydata(vertices, [], faces)
    data.update()
    obj = bpy.data.objects.new(name, data)
    bpy.context.collection.objects.link(obj)
    data.materials.append(mat)
    for poly in data.polygons:
        poly.use_smooth = smooth
    parts.append(obj)
    return obj


def section(angle, exponent):
    def signed(v):
        return math.copysign(abs(v) ** exponent, v)
    return signed(math.cos(angle)), signed(math.sin(angle))


def hull(name, stations, mat, exponent=1., sides=48):
    # Each station gives y, center x/z and the two cross-section radii.
    vertices = []
    for y, x, z, width, height in stations:
        for i in range(sides):
            px, pz = section(tau * i / sides, exponent)
            vertices.append((x + width * px, y, z + height * pz))
    faces = [tuple(range(sides)), tuple(reversed(range(len(vertices)-sides, len(vertices))))]
    for row in range(len(stations)-1):
        for i in range(sides):
            a, b = row*sides+i, row*sides+(i+1)%sides
            faces.append((a, a+sides, b+sides, b))
    obj = mesh(name, vertices, faces, mat)
    for poly in obj.data.polygons[:2]:
        poly.use_smooth = False
    return obj


def line(name, points, radius, mat):
    curve = bpy.data.curves.new(name, 'CURVE')
    curve.dimensions = '3D'
    curve.bevel_depth = radius
    curve.bevel_resolution = 3
    curve.use_fill_caps = True
    spine = curve.splines.new('POLY')
    spine.points.add(len(points)-1)
    for dst, point in zip(spine.points, points):
        dst.co = (*point, 1)
    obj = bpy.data.objects.new(name, curve)
    bpy.context.collection.objects.link(obj)
    curve.materials.append(mat)
    parts.append(obj)


def barrel(name, stations, mat, exponent=1.):
    return hull(name, [(y, 0., 10., w, h) for y, w, h in stations], mat, exponent)


shell = [(-16.2,0,9.1,2.7,2.6),(-15.7,0,9.1,4.6,3.7),(-14.,0,9.2,6.7,5.0),
         (-11.,0,9.4,7.65,5.8),(-7.,0,9.65,8.20,6.05),(-1.,0,9.75,8.48,6.08),
         (6.,0,9.7,8.20,5.85),(12.,0,9.6,7.55,5.35),(18.,0,9.5,6.65,4.72),
         (23.,0,9.55,5.45,3.92),(25.,0,9.7,4.12,3.15)]
hull('Cobalt enamel cowling', shell, cobalt, .74, 64)


def on_shell(y, angle, side, lift):
    for left, right in zip(shell, shell[1:]):
        if y <= right[0]:
            break
    t = (y-left[0]) / (right[0]-left[0])
    z, w, h = [left[i]+(right[i]-left[i])*t for i in (2,3,4)]
    px,pz = section(angle,.74)
    return (side*(w+lift)*px, y, z+(h+lift)*pz)


def flash(name, side, y0, y1, angle0, angle1, half, lift, mat):
    ys = sorted({y0+(y1-y0)*i/44 for i in range(45)} | {s[0] for s in shell if y0<s[0]<y1})
    verts = []
    for y in ys:
        t = (y-y0)/(y1-y0)
        # Rounded head at the rear, drawn to a fine point on the nose.
        width = half * max(.012, math.sin(math.pi*t)**.64 * (1.32-.65*t))
        angle = angle0+(angle1-angle0)*t
        verts.extend(on_shell(y, angle+width*j/3, side, lift) for j in range(-3,4))
    faces = [(i*7+j,(i+1)*7+j,(i+1)*7+j+1,i*7+j+1) for i in range(len(ys)-1) for j in range(6)]
    mesh(name, verts, faces if side>0 else [f[::-1] for f in faces], mat)


for side in (-1,1):
    flash('Nickel outline around speed flash',side,-15.0,20.7,.18,.54,.27,.065,chrome)
    flash('Glacier porcelain side flash',side,-14.75,20.,.18,.54,.212,.11,ice)
    for j in range(3):
        flash('Stepped coachline',side,-14.+j*.55,12.-j*4.1,-.22-.14*j,-.05-.14*j,.030,.11,chrome)

    # The forward hull opens into twin swept pontoons around the light chamber.
    stations = [(8.,6.35,9.5,1.0,1.5),(13.,7.05,9.65,2.05,2.70),
                (19.,7.65,9.9,2.32,3.10),(26.,7.9,10.,2.28,2.88),
                (38.,7.45,10.,1.93,2.22),(48.,6.4,10.,1.60,1.60),
                (57.,4.75,10.,1.15,1.12),(62.,3.2,10.,.60,.73)]
    hull('Swept cobalt outrigger',[(y,side*x,z,w,h) for y,x,z,w,h in stations],cobalt,.74)
    line('Chrome outrigger rubbing strip',[(side*(x+w*.98),y,z+.25) for y,x,z,w,h in stations],.22,chrome)
    line('Porcelain outrigger crest',[(side*x,y,z+h) for y,x,z,w,h in stations[1:]],.30,ice)

# A softly waisted luminous bottle is visible above and between the two outriggers.
barrel('Inset accelerator socket',[(18.,4.2,3.7),(20.,4.65,4.05),(24.,4.42,3.85),(25.,3.85,3.25)],navy,.78)
barrel('Polished socket rim',[(23.,4.48,3.91),(23.4,4.58,4.0),(24.0,4.53,3.95)],chrome,.78)
barrel('Turquoise induction bottle',[(24.,3.65,3.2),(26.,3.92,3.55),(31.,3.68,3.40),
     (38.,2.85,2.72),(43.,2.35,2.29),(49.,2.63,2.52),(54.,3.12,3.01),(58.,3.30,3.17)],cyan)
for y,w,h in [(27.,3.96,3.63),(53.,3.13,3.02)]:
    barrel('Nickel bottle cap',[(y-.64,w-.16,h-.16),(y-.4,w+.12,h+.12),(y+.42,w+.12,h+.12),(y+.65,w-.12,h-.12)],chrome)
for y,x,h in [(31.,7.65,4.6),(48.,6.35,3.7)]:
    line('Polished arch over the induction bottle',
         [(x*math.cos(math.pi*i/24),y,10.+h*math.sin(math.pi*i/24)) for i in range(25)],.28,chrome)
# Broad horizontal emitter with a recessed luminous aperture.
barrel('Dark emitter joint',[(55.,3.2,2.9),(57.,4.4,3.35),(58.8,4.7,3.48)],navy,.66)
barrel('Ice enamel emitter cowl',[(57.2,4.25,3.28),(58.2,5.33,3.65),(61.5,5.70,3.73),
                                  (64.3,5.34,3.47),(65.4,4.73,3.23)],ice,.62)
barrel('Cobalt nose band',[(63.2,5.56,3.65),(63.7,5.58,3.66),(65.5,5.02,3.39),(65.9,4.9,3.34)],cobalt,.62)
barrel('Chrome emitter bezel',[(65.5,4.96,3.37),(66.,5.02,3.41),(66.8,4.87,3.3),(67.,4.62,3.12)],chrome,.62)
barrel('Inset blue-black aperture',[(66.95,4.41,2.97),(67.24,4.15,2.78)],navy,.66)
barrel('Cyan emitter lens',[(67.25,3.28,2.04),(67.64,3.18,1.95),(67.94,2.32,1.48),(68.,.6,.42)],cyan,.78)

# Two grasping surfaces fit the existing idle animation at its authored wrist positions.
hull('Underslung palm saddle',[(13.,0,4.1,1.6,.8),(16.,0,3.2,3.6,1.6),
          (22.,0,2.,4.12,2.1),(31.,0,2.,4.15,2.22),(35.5,0,2.8,3.54,1.65),
          (38.,0,4.0,1.8,.9)],navy,.78)
for y in (22.,24.6,27.2,29.8,32.4):
    hull('Foregrip fluting',[(y-.38,0,2.,4.09,2.12),(y,0,2.,4.25,2.24),(y+.38,0,2.,4.12,2.13)],rubber,.78)


def handle(name, rings, mat):
    n = 32
    verts = [(w*math.cos(tau*i/n),y+d*math.sin(tau*i/n),z) for z,y,w,d in rings for i in range(n)]
    faces = [tuple(reversed(range(n))),tuple(range(len(verts)-n,len(verts)))]
    faces += [(j*n+i,j*n+(i+1)%n,(j+1)*n+(i+1)%n,(j+1)*n+i) for j in range(len(rings)-1) for i in range(n)]
    return mesh(name,verts,faces,mat)


handle('Blue-black Bakelite grip',[(-10.8,-3.55,2.25,3.2),(-10.2,-3.5,3.00,3.72),
       (-8.25,-2.97,3.03,3.72),(-3.4,-1.55,2.72,3.5),(2.0,.04,2.67,3.55),(5.7,1.5,3.14,3.8)],rubber)
handle('Nickel grip heel',[(-10.9,-3.56,2.31,3.18),(-10.65,-3.54,2.85,3.58),(-10.14,-3.46,3.00,3.72)],chrome)
line('Amber control lever',[(0,5.8,4.1),(0,5.55,2.5),(0,5.1,.25),(0,5.7,-.8)],.46,amber)
line('Rolled chrome bow',[(0,1.4,-4.45),(0,3.4,-5.3),(0,7.1,-5.2),(0,10.2,-3.9),(0,11.1,-2.0),(0,11.2,3.65)],.43,chrome)

# A dashboard inspired by a 1950s radio, tilted towards the player.
normal = Vector((0,-.78,.626)).normalized()
across = Vector((1,0,0))
up = normal.cross(across)
origin = Vector((0,-11.15,16.72))


def dash_point(x,y,h):
    return origin + across*x + up*y + normal*h


def dash_panel(name, rings, mat):
    n = 48
    verts = []
    for depth,w,h in rings:
        for i in range(n):
            x,y = section(tau*i/n,.44)
            verts.append(dash_point(x*w,y*h,depth))
    faces = [tuple(reversed(range(n))),tuple(range(len(verts)-n,len(verts)))]
    faces += [(j*n+i,j*n+(i+1)%n,(j+1)*n+(i+1)%n,(j+1)*n+i) for j in range(len(rings)-1) for i in range(n)]
    obj=mesh(name,verts,faces,mat)
    for poly in obj.data.polygons[:2]:
        poly.use_smooth=False
    return obj


dash_panel('Sculpted instrument pod',[(-3.6,6.0,2.48),(-1.5,6.43,2.72),(.12,6.25,2.50)],cobalt)
dash_panel('Chrome instrument rim',[(.06,6.23,2.47),(.35,6.31,2.52),(.54,6.04,2.30)],chrome)
dash_panel('Glacier dashboard surround',[(.54,5.94,2.23),(.61,5.89,2.18)],ice)
display = dash_panel('Luminous charge scale',[(.615,5.10,1.54),(.69,5.08,1.52)],glass)
uv = display.data.uv_layers.new(name='MeterUV')
for loop in display.data.loops:
    v = display.data.vertices[loop.vertex_index].co-origin
    uv.data[loop.index].uv = (.5+v.dot(across)/10.16,.5+v.dot(up)/3.04)

# Show the charged instrument in the editable studio model. Unreal shades the same glass dynamically.
def studio_scale(name, centers, half_width, half_height, mat):
    verts = [dash_point(x+dx, y+dy, .71) for x,y in centers
             for dx,dy in [(-half_width,-half_height),(half_width,-half_height),
                           (half_width,half_height),(-half_width,half_height)]]
    return mesh(name, verts, [tuple(range(i,i+4)) for i in range(0,len(verts),4)], mat, False)


pitch = 10.16*.836/8
phosphor = finish('Studio turquoise phosphor', (.12,.82,.98), .08, .35, 1.1)
studio_bars = studio_scale('Charged display bars - studio', [((i-3.5)*pitch,.03) for i in range(8)],
                           .407*pitch, .55, phosphor)
studio_ticks = studio_scale('Charge scale ticks - studio', [((i-4)*pitch,-.84) for i in range(9)],
                            .11*pitch, .16, ice)
for side in (-1,1):
    line('Bezel rivet', [dash_point(side*5.54,0,.63),dash_point(side*5.54,.02,.72)], .17, amber)
for j in range(3):
    y = -4.2+j*2.7
    line('Chrome dorsal louvers',[on_shell(y,math.radians(a),1,.13) for a in range(64,118,2)],.21,chrome)

# Preserve the separate enamel parts in Blender. Bake a single grip-space mesh for Unreal.
bpy.context.scene.unit_settings.system = 'METRIC'
bpy.context.scene.unit_settings.scale_length = .01
bpy.context.preferences.filepaths.save_version = 0
bpy.ops.wm.save_as_mainfile(filepath=str(out/'Railgun_Source.blend'))
deps = bpy.context.evaluated_depsgraph_get()
bpy.ops.object.select_all(action='DESELECT')
export = []
for obj in parts:
    if obj in (studio_bars, studio_ticks):
        continue
    data = bpy.data.meshes.new_from_object(obj.evaluated_get(deps),depsgraph=deps)
    part = bpy.data.objects.new('Export ' + obj.name,data)
    bpy.context.collection.objects.link(part)
    data.transform(Matrix.Scale(.01,4) @ Matrix.Rotation(math.pi,4,'Z') @ obj.matrix_world)
    if obj != display:
        uv = data.uv_layers.active or data.uv_layers.new(name='UVMap')
        for poly in data.polygons:
            major = max(range(3),key=lambda i:abs(poly.normal[i]))
            axes = [i for i in range(3) if i != major]
            for li in poly.loop_indices:
                p=data.vertices[data.loops[li].vertex_index].co
                uv.data[li].uv=(p[axes[0]]*2.,p[axes[1]]*2.)
    else:
        data.uv_layers.active.name = 'UVMap'
    part.select_set(True)
    export.append(part)
bpy.context.view_layer.objects.active = export[0]
bpy.ops.object.join()
joined = bpy.context.view_layer.objects.active
joined.name = 'SM_QuakeRailgun'
bpy.context.scene.unit_settings.scale_length = 1.
bpy.ops.export_scene.fbx(filepath=str(out/'SM_QuakeRailgun.fbx'),use_selection=True,object_types={'MESH'},
                         add_leaf_bones=False,axis_forward='-Y',axis_up='Z',bake_anim=False,mesh_smooth_type='FACE')
print('BLUE_RETRO_RAILGUN_BUILT', len(joined.data.vertices), 'vertices')
