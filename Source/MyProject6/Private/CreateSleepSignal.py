"""Author the original SOMNA signal puck, portable Blender/FBX source and a soft pulse loop."""
import bpy, math, json, struct, wave
from pathlib import Path
from mathutils import Vector

root=Path(__file__).resolve().parents[3]
out=root/'ContentSource/SleepSignal';out.mkdir(parents=True,exist_ok=True)
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
palette={'SleepGraphite':((.018,.028,.055),.8,.29,0),
         'SleepCeramic':((.34,.42,.48),.65,.24,0),
         'SleepRubber':((.008,.012,.02),.1,.7,0),
         'SleepCyan':((.025,.65,1),.35,.22,2.3),
         'SleepViolet':((.4,.08,1),.4,.2,1.8)}
materials={}
for name,(color,metal,rough,emission) in palette.items():
    m=bpy.data.materials.new(name);m.diffuse_color=(*color,1);m.use_nodes=True
    p=m.node_tree.nodes.get('Principled BSDF')
    for key,value in [('Base Color',(*color,1)),('Metallic',metal),('Roughness',rough),('Emission Color',(*color,1)),('Emission Strength',emission)]:p.inputs[key].default_value=value
    materials[name]=m

def finish(obj,name,mat,bevel=0):
    obj.name=name;obj.data.materials.append(materials[mat])
    if bevel:
        mod=obj.modifiers.new('Soft machined edges','BEVEL');mod.width=bevel;mod.segments=3
        bpy.context.view_layer.objects.active=obj;bpy.ops.object.modifier_apply(modifier=mod.name)
    for poly in obj.data.polygons:poly.use_smooth=True
    return obj
def cylinder(name,radius,depth,z,mat,vertices=64):
    bpy.ops.mesh.primitive_cylinder_add(vertices=vertices,radius=radius,depth=depth,location=(0,0,z))
    return finish(bpy.context.object,name,mat,.002)
def torus(name,radius,tube,z,mat):
    bpy.ops.mesh.primitive_torus_add(major_radius=radius,minor_radius=tube,major_segments=72,minor_segments=10,location=(0,0,z))
    return finish(bpy.context.object,name,mat)
def box(name,loc,scale,mat,rot=0):
    bpy.ops.mesh.primitive_cube_add(size=1,location=loc,rotation=(0,0,rot));o=bpy.context.object;o.scale=scale
    bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
    return finish(o,name,mat,.002)

cylinder('Weighted non-slip foot',.102,.024,-.103,'SleepRubber')
cylinder('Scalloped lower chassis',.128,.035,-.078,'SleepGraphite',12)
torus('Cyan horizon light',.12,.003,-.055,'SleepCyan')
cylinder('Upper ceramic cap',.116,.045,-.032,'SleepCeramic',64)
cylinder('Recessed top panel',.09,.008,-.006,'SleepGraphite')
torus('Violet induction rim',.088,.0025,0,'SleepViolet')
cylinder('Emitter pedestal',.04,.012,.002,'SleepRubber')
cylinder('Axial transducer',.016,.105,.057,'SleepGraphite',24)
for z,r in [(.025,.034),(.05,.029),(.075,.024)]:
    cylinder('Stacked resonator',r,.008,z,'SleepCeramic',32)
    torus('Glowing resonator seam',r,.0015,z+.005,'SleepCyan')
bpy.ops.mesh.primitive_uv_sphere_add(segments=32,ring_count=16,radius=.021,location=(0,0,.118))
finish(bpy.context.object,'Luminous emitter lens','SleepViolet')
for i in range(8):
    a=i*math.tau/8;x,y=.104*math.cos(a),.104*math.sin(a)
    box('Radial grip flute',(x,y,-.03),(.018,.012,.027),'SleepGraphite',a)
    box('Status slit',(x*1.03,y*1.03,-.027),(.006,.013,.005),'SleepCyan',a)
for i in range(3):
    a=i*math.tau/3
    box('Impact bumper',(.11*math.cos(a),.11*math.sin(a),-.091),(.05,.027,.027),'SleepRubber',a)
# Raised moon / signal emblem, visible on the top face.
for i in range(24):
    a=-1.1+i*2.2/23
    box('Moon emblem',(.054+.017*math.cos(a),.017*math.sin(a),.001),(.004,.004,.003),'SleepCyan')
for i in range(3):box('Signal strength',(-.059+i*.012,0,.001),(.006,.012+i*.009,.003),'SleepCyan')
bpy.ops.object.select_all(action='SELECT');bpy.context.view_layer.objects.active=bpy.context.selected_objects[0];bpy.ops.object.join()
obj=bpy.context.object;obj.name='SM_SleepSignalDevice'
bpy.context.scene.cursor.location=(0,0,0);bpy.ops.object.origin_set(type='ORIGIN_CURSOR')
bpy.ops.object.transform_apply(location=False,rotation=True,scale=True)
bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT');bpy.ops.mesh.normals_make_consistent(inside=False)
bpy.ops.uv.smart_project(angle_limit=math.radians(66),island_margin=.008);bpy.ops.object.mode_set(mode='OBJECT')
bpy.ops.export_scene.fbx(filepath=str(out/'SM_SleepSignalDevice.fbx'),use_selection=True,object_types={'MESH'},axis_forward='-Y',axis_up='Z',bake_anim=False,mesh_smooth_type='FACE')
bpy.ops.wm.save_as_mainfile(filepath=str(out/'SleepSignal_Source.blend'))
(out/'Palette.json').write_text(json.dumps(palette,indent=2))
values=[]
for i in range(44100*4):
    t=i/44100
    env=(.5+.5*math.cos(math.tau*t))**4
    value=.13*(math.sin(math.tau*220*t)+.4*math.sin(math.tau*440*t))*(.12+.88*env)
    values.append(struct.pack('<h',int(value*32767)))
with wave.open(str(out/'S_SleepSignalHum.wav'),'wb') as f:
    f.setnchannels(1);f.setsampwidth(2);f.setframerate(44100);f.writeframes(b''.join(values))
print('SLEEP_SIGNAL_SOURCE_READY')
