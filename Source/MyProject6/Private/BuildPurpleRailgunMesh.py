"""Build the original Comet arcade raygun in the existing two-hand grip space.

Run with Blender in background. Dimensions below are centimetres, +Y is down
the barrel. The .blend retains the separate named enamel, trim and dial pieces.
"""
from pathlib import Path
from math import cos, sin, pi, radians
import json
import bpy
from mathutils import Matrix, Vector

root = Path(__file__).resolve().parents[3]
source = root / 'ContentSource/PurpleRailgun'
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
palette = {
    'PurpleBody': (.64, .022, .045, .40, .24),
    'CometIvory': (.97, .79, .48, .15, .31),
    'CometBrass': (.72, .39, .125, .72, .26),
    'CometOxblood': (.065, .016, .026, .20, .39),
    'CometGrip': (.046, .034, .038, .12, .55),
    'PurpleEnergy': (1., .025, .052, .16, .23),
    'CometDial': (.028, .057, .063, .32, .25),
}
materials = {}
for name, (r, g, b, metal, rough) in palette.items():
    mat = bpy.data.materials.new(name)
    mat.diffuse_color = (r, g, b, 1)
    mat.use_nodes = True
    shader = mat.node_tree.nodes.get('Principled BSDF')
    shader.inputs['Base Color'].default_value = mat.diffuse_color
    shader.inputs['Metallic'].default_value = metal
    shader.inputs['Roughness'].default_value = rough
    if name == 'PurpleEnergy':
        shader.inputs['Emission Color'].default_value = mat.diffuse_color
        shader.inputs['Emission Strength'].default_value = 1.6
    materials[name] = mat

cherry, ivory, brass, dark, grip, ruby, dial = materials
objects = []

def mesh(name, vertices, faces, finish, smooth=True):
    data = bpy.data.meshes.new(name)
    data.from_pydata(vertices, [], faces)
    data.update()
    obj = bpy.data.objects.new(name, data)
    bpy.context.collection.objects.link(obj)
    data.materials.append(materials[finish])
    for polygon in data.polygons:
        polygon.use_smooth = smooth
    objects.append(obj)
    return obj

def soften(obj, width):
    bevel = obj.modifiers.new('Rolled edges', 'BEVEL')
    bevel.width = width
    bevel.segments = 3
    normal = obj.modifiers.new('Flat enamel faces', 'WEIGHTED_NORMAL')
    normal.keep_sharp = True

def loft(name, profile, finish, sides=48):
    # Station = longitudinal position, horizontal and vertical radii, height.
    vertices = [(rx*cos(2*pi*i/sides), y, z+rz*sin(2*pi*i/sides))
                for y, rx, rz, z in profile for i in range(sides)]
    faces = [tuple(range(sides)), tuple(reversed(range(len(vertices)-sides, len(vertices))))]
    for j in range(len(profile)-1):
        for i in range(sides):
            a, b = j*sides+i, j*sides+(i+1)%sides
            faces.append((a, a+sides, b+sides, b))
    obj = mesh(name, vertices, faces, finish)
    for face in obj.data.polygons[:2]:
        face.use_smooth = False
    return obj

def fair(points, y):
    # Monotone Hermite interpolation gives continuous highlights on the main shell.
    for i in range(len(points)-1):
        if y <= points[i+1][0]:
            break
    delta = points[i+1][0]-points[i][0]
    t = min(1., max(0., (y-points[i][0])/delta))
    def slope(j, k):
        if j==0 or j==len(points)-1:
            a, b = (0, 1) if j==0 else (j-1, j)
            return (points[b][k]-points[a][k])/(points[b][0]-points[a][0])
        a = (points[j][k]-points[j-1][k])/(points[j][0]-points[j-1][0])
        b = (points[j+1][k]-points[j][k])/(points[j+1][0]-points[j][0])
        return 2*a*b/(a+b) if a*b>0 else 0.
    return tuple((2*t**3-3*t*t+1)*points[i][k] + (t**3-2*t*t+t)*delta*slope(i,k)
                 + (-2*t**3+3*t*t)*points[i+1][k] + (t**3-t*t)*delta*slope(i+1,k)
                 for k in range(1,len(points[0])))

def drawn(name, points, finish, radius):
    points = list(map(Vector, points))
    data = bpy.data.curves.new(name, 'CURVE')
    data.dimensions = '3D'
    data.resolution_u = 2
    data.bevel_depth = radius
    data.bevel_resolution = 3
    data.use_fill_caps = True
    spline = data.splines.new('POLY')
    spline.points.add(len(points)-1)
    for dst, pos in zip(spline.points, points):
        dst.co = (*pos, 1)
    obj = bpy.data.objects.new(name, data)
    bpy.context.collection.objects.link(obj)
    data.materials.append(materials[finish])
    objects.append(obj)

def blade(name, points, side, offset, thickness, finish, bevel):
    # Face profile in YZ. Each blade sweeps outward toward its tail.
    def x_at(y): return side*(offset + max(0., 5-y)*.12)
    vertices = [(x_at(y)+d*side, y, z) for d in (0., thickness) for y,z in points]
    n = len(points)
    faces = [tuple(range(n)), tuple(reversed(range(n, 2*n)))]
    faces += [(i, n+i, n+(i+1)%n, (i+1)%n) for i in range(n)]
    obj = mesh(name, vertices, faces, finish, False)
    soften(obj, bevel)

def collar(name, y, radius, width, finish, z=10.74, flatten=1., lip=.16):
    cuts = [(-.5, .91), (-.5+lip, 1.), (.5-lip, 1.), (.5, .91)]
    return loft(name, [(y+u*width, radius*k, radius*k*flatten, z) for u,k in cuts], finish)

body = [(-16.8,.65,.9,9.5),(-16.,3.4,3.2,9.4),(-13.,6.8,5.9,9.6),
        (-7.,8.25,7.0,10.),(3.,8.55,7.05,10.35),(11.,7.95,6.7,10.55),
        (20.,6.65,5.7,10.7),(26.,5.5,4.75,10.74),(28.5,4.8,4.45,10.74)]
ys = sorted(set([p[0] for p in body] + [-16.8+i*45.3/48 for i in range(49)]))
loft('01 | Cherry torpedo shell', [(y,*fair(body,y)) for y in ys], cherry, 64)
def on_shell(y, angle, side, lift):
    rx, rz, z = fair(body,y)
    return Vector((side*(rx+lift)*cos(angle), y, z+(rz+lift)*sin(angle)))

def stripe(name, side, start, end, centre, halfwidth, lift, finish):
    verts = []
    for i in range(49):
        t = i/48
        y = start+(end-start)*t
        half = max(.004, halfwidth*sin(pi*t)**.8)
        for j in range(7):
            verts.append(on_shell(y, centre(t)+half*(j/3-1), side, lift))
    faces = [(i*7+j,(i+1)*7+j,(i+1)*7+j+1,i*7+j+1) for i in range(48) for j in range(6)]
    mesh(name, verts, faces if side>0 else [f[::-1] for f in faces], finish)

for side in (-1,1):
    stripe('02 | Swept champagne border',side,-14.4,23.4,lambda t:.22+.22*t,.34,.055,brass)
    stripe('03 | Cream side flash',side,-13.9,22.6,lambda t:.22+.22*t,.276,.092,ivory)
    stripe('04 | Upper coachline',side,-10.8,22,lambda t:.89-.10*t,.023,.07,brass)
    # Twin Cadillac fins have a cream inset and a bright leading lip.
    outline=[(-17.,14.8),(-18.7,21.9),(-17.7,22.1),(-8.2,17.65),(5.8,14.8),(8.,12.4),(-6.8,12.35)]
    blade('05 | Swept tail fin',outline,side,4.75,.92,cherry,.28)
    inset=[(-17.35,20.95),(-8.0,16.86),(2.8,14.6),(-7.6,15.35),(-15.75,17.8)]
    blade('06 | Cream fin inlay',inset,side,5.68,.09,ivory,.10)
    drawn('07 | Rolled fin edge',[(side*(5.35+max(0.,5-y)*.12),y,z) for y,z in [(-18.3,21.9),(-17.6,22.0),(-8.2,17.65),(5.8,14.8)]],brass,.16)
    # Raised four-point insignia over the cream flash.
    center_y, angle = -2.5, .30
    insignia=[(0,.19),(.55,.05),(3.5,0),(.55,-.045),(0,-.18),(-.54,-.05),(-3.5,0),(-.54,.05)]
    vertices=[on_shell(center_y, angle, side, .18)]+[on_shell(center_y+y,angle+a,side,.18) for y,a in insignia]
    faces=[(0,i+1,(i+1)%8+1) for i in range(8)]
    mesh('08 | Brass comet emblem',vertices,faces if side<0 else [f[::-1] for f in faces],brass)

loft('09 | Socket collar',[(25.4,5.56,5.04,10.74),(26.1,5.88,5.23,10.74),(27.4,5.88,5.23,10.74),
                           (28.1,5.1,4.6,10.74),(29.,4.9,4.5,10.74)],brass)
loft('10 | Porcelain nose',[(27.8,5.15,4.68,10.74),(28.4,5.36,4.87,10.74),
       (31.8,4.96,4.53,10.74),(33.9,4.25,4.02,10.74),(34.4,3.48,3.42,10.74)],ivory)
collar('11 | Dark accelerator socket',34.0,3.95,1.55,dark)
loft('12 | Exposed ruby accelerator',[(33.3,2.7,2.7,10.74),(34.3,3.1,3.1,10.74),
            (39.,2.75,2.75,10.74),(48.,2.05,2.05,10.74),(52.7,1.8,1.8,10.74)],ruby)
for i,(y,radius) in enumerate([(36.2,5.15),(41.6,4.55),(46.7,3.95)]):
    profile=[(y-.97,2.42,2.42,10.74),(y-.52,radius*.89,radius*.89,10.74),
             (y-.10,radius,radius,10.74),(y+.32,radius,radius,10.74),
             (y+.58,radius*.84,radius*.84,10.74),(y+.95,2.2,2.2,10.74)]
    loft('13 | Saturn vane %d'%i,profile,ivory)
    collar('14 | Gold vane rim %d'%i,y+.1,radius+.10,.48,brass)
loft('15 | Cherry emitter nose',[(50.4,2.25,2.25,10.74),(51.,2.8,2.8,10.74),
      (53.7,3.65,3.65,10.74),(55.6,3.82,3.82,10.74),(56.1,3.4,3.4,10.74)],cherry)
collar('16 | Brass emitter bezel',56.07,3.7,1.15,brass)
collar('17 | Recessed dark lens seat',56.73,2.98,.48,dial)
loft('18 | Ruby emitter lens',[(56.8,2.45,2.45,10.74),(57.12,2.44,2.44,10.74),
       (57.36,1.75,1.75,10.74),(57.44,.36,.36,10.74)],ruby)
# An underside palm rest meets the baked support-hand pose at the front of the shell.
loft('19 | Front palm rest',[(17.,1.3,1.,3.4),(20.,3.5,2.2,3.),(24.,4.1,2.8,2.9),
       (33.,4.1,2.8,2.8),(36.5,3.2,2.3,3.25),(38.,1.1,1.2,3.6)],dark)
for y in [23.8,26.4,29.,31.6,34.2]:
    collar('20 | Fluted cream palm grip',y,4.15,.82,ivory,z=2.85,flatten=.67)
# Vertical rear grip, tilted forward at the top.
def handle(name, stations, finish):
    n=32
    verts=[(rx*cos(2*pi*i/n),y+ry*sin(2*pi*i/n),z) for z,y,rx,ry in stations for i in range(n)]
    faces=[tuple(reversed(range(n))),tuple(range(len(verts)-n,len(verts)))]
    faces += [(j*n+i,j*n+(i+1)%n,(j+1)*n+(i+1)%n,(j+1)*n+i)
              for j in range(len(stations)-1) for i in range(n)]
    return mesh(name,verts,faces,finish)
handle('21 | Oxblood pistol grip',[(-10.3,-3.2,2.4,3.25),(-9.3,-3.4,3.05,3.9),
       (-5.7,-2.4,2.95,3.5),(-.4,-.4,2.6,3.15),(4.8,2.,3.12,3.75),(6.,3.,3.4,3.8)],grip)
handle('22 | Brass heel cap',[(-10.7,-3.45,2.23,3.1),(-10.5,-3.45,2.86,3.75),
       (-9.75,-3.35,3.13,3.88)],brass)
drawn('23 | Trigger bow',[(0,2.5,-4.6),(0,4.1,-5.6),(0,7.5,-5.4),(0,10.8,-3.7),(0,12.7,-.3),(0,12.8,3.6)],brass,.48)
drawn('24 | Red trigger',[(0,6.5,3.4),(0,6.0,1.4),(0,5.65,-.4),(0,6.2,-1.5)],cherry,.48)

# The top dial faces back toward the player. All markings are relief geometry.
N=Vector((0,-.64,.768)).normalized()
U=Vector((1,0,0))
V=N.cross(U)
O=Vector((0,-8.2,16.55))
def on_dial(x,y,h): return O+U*x+V*y+N*h
def dial_disc(name, rings, finish):
    n=64
    verts=[on_dial(r*cos(2*pi*i/n),r*sin(2*pi*i/n),h) for h,r in rings for i in range(n)]
    faces=[tuple(reversed(range(n))),tuple(range(len(verts)-n,len(verts)))]
    faces += [(j*n+i,j*n+(i+1)%n,(j+1)*n+(i+1)%n,(j+1)*n+i)
              for j in range(len(rings)-1) for i in range(n)]
    obj=mesh(name,verts,faces,finish)
    for f in obj.data.polygons[:2]: f.use_smooth=False
dial_disc('25 | Dashboard binnacle',[(-2.3,3.65),(-1.3,4.45),(.32,4.38),(.50,4.18)],dark)
dial_disc('26 | Turned brass dial bezel',[(.42,4.24),(.60,4.36),(.88,4.19),(1.,3.94)],brass)
dial_disc('27 | Porcelain dial face',[(1.01,3.88),(1.05,3.88)],ivory)
for i in range(13):
    a=radians(25+130*i/12)
    r=2.54 if i%3==0 else 2.87
    drawn('28 | Meter graduation', [on_dial(r*cos(a),r*sin(a),1.12),on_dial(3.32*cos(a),3.32*sin(a),1.12)],
          cherry if i<3 else dial, .103 if i%3==0 else .076)
drawn('29 | Hot band', [on_dial(3.5*cos(radians(a)),3.5*sin(radians(a)),1.12) for a in range(25,53,2)],cherry,.108)
mesh('30 | Needle',[on_dial(*p,1.24) for p in [(-.32,-.57),(.25,-.71),(1.8,2.38)]],[(0,1,2)],cherry,False)
drawn('31 | Meter underline',[on_dial(-1.0,-1.82,1.13),on_dial(1.0,-1.82,1.13)],dial,.09)
dial_disc('32 | Pointer pivot',[(1.25,.42),(1.42,.40)],brass)
# A small ruby dorsal jewel ties the visible meter to the forward accelerator.
for i,(y,r) in enumerate([(8.,1.),(11.1,.72),(13.4,.50)]):
    rx,rz,z=fair(body,y)
    for finish,k,h in [(brass,1.24,0.),(ruby,1.,.13)]:
        profile=[(y-r*k,.13,.10,z+rz+h),(y-r*.72*k,r*.78*k,r*.30,z+rz+h),
                 (y,r*k,r*.50,z+rz+h),(y+r*.72*k,r*.78*k,r*.30,z+rz+h),(y+r*k,.13,.10,z+rz+h)]
        loft('33 | Dashboard jewel %d'%i,profile,finish,24)

# Freeze evaluated mesh parts for export, retaining the named objects and
# editable curves/modifiers in the source file. The defining grip is the origin.
bpy.context.scene.unit_settings.system='METRIC'
bpy.context.scene.unit_settings.scale_length=.01
bpy.context.preferences.filepaths.save_version=0
bpy.ops.wm.save_as_mainfile(filepath=str(source/'CometRailgun_Source.blend'))
deps=bpy.context.evaluated_depsgraph_get()
bpy.ops.object.select_all(action='DESELECT')
export=[]
for obj in objects:
    data=bpy.data.meshes.new_from_object(obj.evaluated_get(deps),depsgraph=deps)
    part=bpy.data.objects.new('Export '+obj.name,data)
    bpy.context.collection.objects.link(part)
    data.transform(Matrix.Scale(.01,4) @ Matrix.Rotation(pi,4,'Z') @ obj.matrix_world)
    # Grain UVs in grip space prevent the metal parent from sampling one point.
    uv=data.uv_layers.new(name='UVMap') if not data.uv_layers.active else data.uv_layers.active
    for poly in data.polygons:
        axis=max(range(3),key=lambda i:abs(poly.normal[i]))
        dims=[i for i in range(3) if i!=axis]
        for li in poly.loop_indices:
            co=data.vertices[data.loops[li].vertex_index].co
            uv.data[li].uv=(co[dims[0]]*2.,co[dims[1]]*2.)
    part.select_set(True)
    export.append(part)
bpy.context.view_layer.objects.active=export[0]
bpy.ops.object.join()
joined=bpy.context.view_layer.objects.active
joined.name='SM_PurpleRailgun'
bpy.context.scene.unit_settings.scale_length=1.
bpy.ops.export_scene.fbx(filepath=str(source/'SM_PurpleRailgun.fbx'),use_selection=True,object_types={'MESH'},
    add_leaf_bones=False,axis_forward='-Y',axis_up='Z',bake_anim=False,mesh_smooth_type='FACE')
meta={'length_cm':76.14,'muzzle':[0,57.44,10.74],'support_wrist':[5.2,31.5,-2.0]}
(source/'GripSpace.json').write_text(json.dumps(meta,indent=2))
print('COMET_RAILGUN_BUILT',len(joined.data.vertices),'vertices')
