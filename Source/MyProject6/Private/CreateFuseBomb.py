"""Iconic spherical bomb with an enamel shell, neon inlays and a luminous cable fuse."""
import bpy, math, json, random, struct, wave
from pathlib import Path
from mathutils import Vector

root = Path(__file__).resolve().parents[3]
out = root / 'ContentSource/FuseBomb'
out.mkdir(parents=True, exist_ok=True)
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)

palette = {'BombIron': ((.042,.018,.085),.55,.25,0),
           'BombSeam': ((.055,.075,.12),.8,.26,0),
           'BombBrass': ((.11,.15,.22),.8,.23,0),
           'BombRope': ((.015,.65,.95),.35,.25,3.0),
           'BombChar': ((.045,.025,.07),.45,.32,0),
           'BombCyan': ((.012,.75,1.),.3,.22,3.5),
           'BombMagenta': ((.8,.018,.3),.35,.25,2.5)}
materials = {}
for name, (color, metal, rough, emission) in palette.items():
    m = bpy.data.materials.new(name)
    m.diffuse_color = (*color,1)
    m.use_nodes = True
    p = m.node_tree.nodes.get('Principled BSDF')
    p.inputs['Base Color'].default_value = (*color,1)
    p.inputs['Metallic'].default_value = metal
    p.inputs['Roughness'].default_value = rough
    p.inputs['Emission Color'].default_value = (*color,1)
    p.inputs['Emission Strength'].default_value = emission
    materials[name] = m

def finish(obj, name, mat):
    obj.name = name
    obj.data.materials.append(materials[mat])
    for p in obj.data.polygons: p.use_smooth = True
    return obj

bpy.ops.mesh.primitive_uv_sphere_add(segments=64, ring_count=40, radius=.115)
finish(bpy.context.object, 'Cast iron sphere', 'BombIron')
bpy.ops.mesh.primitive_torus_add(major_radius=.1145, minor_radius=.0012, major_segments=96, minor_segments=8)
finish(bpy.context.object, 'Subtle cast seam', 'BombSeam')
# Two fine recessed-looking latitude seams leave the silhouette instantly readable.
for z, mat in [(-.024,'BombCyan'),(-.030,'BombMagenta')]:
    bpy.ops.mesh.primitive_torus_add(major_radius=math.sqrt(.115**2-z*z),minor_radius=.00065,
        major_segments=96,minor_segments=6,location=(0,0,z))
    finish(bpy.context.object,'Neon equator inlay',mat)
for radius, depth, z, mat in [(.025,.013,.114,'BombSeam'),(.021,.010,.124,'BombBrass'),(.012,.007,.132,'BombChar')]:
    bpy.ops.mesh.primitive_cylinder_add(vertices=40, radius=radius, depth=depth, location=(0,0,z))
    obj = finish(bpy.context.object,'Fuse socket',mat)
    bevel = obj.modifiers.new('Rounded socket edge','BEVEL'); bevel.width=.0015; bevel.segments=3
    bpy.context.view_layer.objects.active=obj
    bpy.ops.object.modifier_apply(modifier=bevel.name)
for z, radius, mat in [(.119,.0252,'BombMagenta'),(.129,.0212,'BombCyan')]:
    bpy.ops.mesh.primitive_torus_add(major_radius=radius,minor_radius=.0008,major_segments=48,minor_segments=6,location=(0,0,z))
    finish(bpy.context.object,'Luminous fuse collar',mat)

def tube(name, pts, radius, mat):
    curve=bpy.data.curves.new(name,'CURVE');curve.dimensions='3D';curve.bevel_depth=radius;curve.bevel_resolution=3
    spline=curve.splines.new('POLY');spline.points.add(len(pts)-1)
    for p,v in zip(spline.points,pts):p.co=(*v,1)
    obj=bpy.data.objects.new(name,curve);bpy.context.collection.objects.link(obj);obj.data.materials.append(materials[mat])
    bpy.ops.object.select_all(action='DESELECT');obj.select_set(True);bpy.context.view_layer.objects.active=obj
    bpy.ops.object.convert(target='MESH')

def centre(t): return Vector((.06*t*t, .009*math.sin(t*math.pi), .133+.067*t))
tube('Neon cable fuse',[centre(i/48) for i in range(49)],.0026,'BombRope')
# Widely spaced dark sleeves read as an electronic fuse, retaining the classic curve.
for i in range(9):
    t=.08+i*.095
    tube('Cable sleeve',[centre(t),centre(t+.025)],.00285,'BombChar')
tube('Dark fuse tip',[centre(.96),centre(1)],.0034,'BombChar')
# Restrained vertical status bars on the camera-facing hemisphere.
for i in range(5):
    y=(i-2)*.008
    pts=[]
    for j in range(8):
        z=.008+j*.0022
        pts.append((math.sqrt(.1154**2-y*y-z*z),y,z))
    tube('Charge indicator',pts,.00065,'BombCyan' if i<4 else 'BombMagenta')

bpy.ops.object.select_all(action='SELECT');bpy.context.view_layer.objects.active=bpy.context.selected_objects[0]
bpy.ops.object.join();obj=bpy.context.object;obj.name='SM_FuseBomb'
bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT')
bpy.ops.mesh.normals_make_consistent(inside=False)
bpy.ops.uv.smart_project(angle_limit=math.radians(66),island_margin=.008)
bpy.ops.object.mode_set(mode='OBJECT')
bpy.context.scene.cursor.location=(0,0,0);bpy.ops.object.origin_set(type='ORIGIN_CURSOR')
bpy.ops.object.transform_apply(location=False,rotation=True,scale=True)
obj.rotation_euler.z=math.pi;bpy.ops.object.transform_apply(location=False,rotation=True,scale=True)
bpy.ops.export_scene.fbx(filepath=str(out/'SM_FuseBomb.fbx'),use_selection=True,object_types={'MESH'},
    add_leaf_bones=False,axis_forward='-Y',axis_up='Z',bake_anim=False,mesh_smooth_type='FACE')
bpy.ops.wm.save_as_mainfile(filepath=str(out/'FuseBomb_Source.blend'))
(out/'Palette.json').write_text(json.dumps(palette,indent=2))

rng=random.Random(2481)
for name,duration in [('S_BombThrow',.28),('S_BombFuse',1.),('S_BombExplosion',1.25)]:
    values=[];low=0.;phase=0.
    for i in range(int(44100*duration)):
        t=i/44100;noise=rng.uniform(-1,1);low=.91*low+.09*noise
        if name=='S_BombExplosion':
            phase+=math.tau*(43+85*math.exp(-t*13))/44100
            value=(.65*math.sin(phase)+1.4*low+.22*noise*math.exp(-t*16))*min(1,t/.002)*math.exp(-t*4.6)
        elif name=='S_BombFuse':
            value=(noise*.09+(noise if rng.random()<.003 else 0)*.7)*(.7+.3*math.sin(t*math.tau*13)**2)
        else: value=(noise-low)*.38*math.sin(math.pi*t/duration)**2
        values.append(struct.pack('<h',int(max(-1,min(1,value))*.88*32767)))
    with wave.open(str(out/(name+'.wav')),'wb') as stream:
        stream.setnchannels(1);stream.setsampwidth(2);stream.setframerate(44100);stream.writeframes(b''.join(values))
print('FUSE_BOMB_SOURCE_READY')
