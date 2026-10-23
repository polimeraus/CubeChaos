"""Original split-prong induction gun, authored source model and synthesized electric audio."""
import bpy, math, json, wave, struct, random
from pathlib import Path
out=Path(__file__).resolve().parents[3]/'ContentSource/ElectricTether';out.mkdir(parents=True,exist_ok=True)
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
palette={'ArcGraphite':((.018,.028,.045),.8,.32,0),'ArcCeramic':((.42,.52,.6),.7,.24,0),
         'ArcRubber':((.008,.014,.022),.05,.75,0),'ArcCopper':((.38,.13,.045),.82,.25,0),
         'ArcCyan':((.025,.68,1),.3,.23,3),'ArcViolet':((.42,.045,1),.3,.23,2.3)}
mats={}
for name,(col,metal,rough,emission) in palette.items():
 m=bpy.data.materials.new(name);m.diffuse_color=(*col,1);m.use_nodes=True;p=m.node_tree.nodes.get('Principled BSDF')
 for k,v in [('Base Color',(*col,1)),('Metallic',metal),('Roughness',rough),('Emission Color',(*col,1)),('Emission Strength',emission)]:p.inputs[k].default_value=v
 mats[name]=m
def finish(o,name,mat,bevel=.003):
 o.name=name;o.data.materials.append(mats[mat]);bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
 if bevel:
  mod=o.modifiers.new('Machined edge','BEVEL');mod.width=bevel;mod.segments=3;bpy.context.view_layer.objects.active=o;bpy.ops.object.modifier_apply(modifier=mod.name)
 return o
def box(name,loc,scale,mat,rot=(0,0,0)):
 bpy.ops.mesh.primitive_cube_add(size=1,location=loc,rotation=rot);o=bpy.context.object;o.scale=scale;return finish(o,name,mat)
def cyl(name,loc,radius,depth,mat):
 bpy.ops.mesh.primitive_cylinder_add(vertices=40,radius=radius,depth=depth,location=loc,rotation=(math.pi/2,0,0));return finish(bpy.context.object,name,mat,.0015)
def ring(name,loc,radius,tube,mat):
 bpy.ops.mesh.primitive_torus_add(major_radius=radius,minor_radius=tube,major_segments=48,minor_segments=8,location=loc,rotation=(math.pi/2,0,0));return finish(bpy.context.object,name,mat,0)
box('Rear receiver',(0,.095,.105),(.16,.29,.14),'ArcGraphite')
box('Ceramic spine',(0,.15,.18),(.10,.35,.022),'ArcCeramic')
box('Pistol grip',(0,-.012,-.045),(.058,.084,.15),'ArcRubber',(-.2,0,0))
box('Grip heel',(0,-.028,-.115),(.074,.085,.022),'ArcCeramic')
box('Foregrip',(0,.34,.022),(.075,.14,.10),'ArcRubber')
for i in range(6):box('Foregrip ribs',(0,.285+i*.022,-.032),(.083,.009,.014),'ArcGraphite')
box('Lower bridge',(0,.265,.07),(.13,.43,.025),'ArcCeramic')
cyl('Induction chamber',(0,.33,.125),.072,.22,'ArcGraphite')
for i in range(7):
 ring('Exposed copper winding',(0,.24+i*.029,.125),.073,.005,'ArcCopper')
 if i%2==0:ring('Luminous field collar',(0,.243+i*.029,.125),.08,.0025,'ArcCyan')
for side in [-1,1]:
 box('Fork shoulder',(side*.095,.445,.12),(.065,.095,.105),'ArcCeramic')
 box('Twin electrode rail',(side*.105,.55,.125),(.043,.26,.05),'ArcGraphite')
 box('Cyan conductor',(side*.078,.553,.126),(.007,.225,.026),'ArcCyan')
 box('Electrode nose',(side*.096,.68,.125),(.055,.055,.07),'ArcCeramic')
 box('Violet terminal',(side*.067,.69,.125),(.013,.03,.045),'ArcViolet')
 for i in range(4):
  box('Cooling vent',(side*.082,.015+i*.046,.115),(.005,.022,.063),'ArcRubber')
  box('Charge indicator',(side*.086,.015+i*.046,.145),(.006,.014,.014),'ArcCyan')
 box('Side panel',(side*.082,.125,.10),(.012,.17,.071),'ArcCeramic')
 # Engraved three-stroke bolt is real geometry, visible from either side.
 for y,z,angle in [(.095,.116,-.5),(.12,.10,0),(.14,.084,-.5)]:
  box('Arc insignia',(side*.091,y,z),(.003,.045,.006),'ArcViolet',(angle,0,0))
cyl('Core lens',(0,.455,.125),.045,.012,'ArcViolet')
ring('Lens bezel',(0,.461,.125),.052,.006,'ArcCeramic')
box('Rear power cell',(0,-.09,.10),(.11,.08,.095),'ArcGraphite')
box('Rear status bar',(0,-.133,.105),(.07,.008,.018),'ArcCyan')
bpy.ops.object.select_all(action='SELECT');bpy.context.view_layer.objects.active=bpy.context.selected_objects[0];bpy.ops.object.join();obj=bpy.context.object;obj.name='SM_ArcCage'
bpy.context.scene.cursor.location=(0,0,0);bpy.ops.object.origin_set(type='ORIGIN_CURSOR');bpy.ops.object.transform_apply(location=False,rotation=True,scale=True)
bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT');bpy.ops.mesh.normals_make_consistent(inside=False);bpy.ops.uv.smart_project(island_margin=.008);bpy.ops.object.mode_set(mode='OBJECT')
# FBX importer flips the authored Y axis; bake the same compensation as the rifle rig.
obj.rotation_euler[2]=math.pi;bpy.ops.object.transform_apply(location=False,rotation=True,scale=True)
bpy.ops.export_scene.fbx(filepath=str(out/'SM_ArcCage.fbx'),use_selection=True,object_types={'MESH'},axis_forward='-Y',axis_up='Z',bake_anim=False,mesh_smooth_type='FACE')
bpy.ops.wm.save_as_mainfile(filepath=str(out/'ArcCage_Source.blend'));(out/'Palette.json').write_text(json.dumps(palette,indent=2))
rng=random.Random(91)
for name,duration in [('Hum',2.),('Catch',.3),('Release',.36)]:
 data=[];rate=44100
 for i in range(int(duration*rate)):
  t=i/rate;noise=rng.uniform(-1,1)
  if name=='Hum':value=.12*(math.sin(math.tau*110*t)+.3*math.sin(math.tau*330*t))*(.65+.35*math.sin(math.tau*13*t))+.035*noise
  else:
   env=math.sin(math.pi*min(1,t/.012)/2)*max(0,1-t/duration)**2
   phase=math.tau*(900*t+(-950 if name=='Release' else 1800)*t*t)
   value=env*(.3*math.sin(phase)+.12*noise)
  data.append(struct.pack('<h',int(max(-1,min(1,value))*32767)))
 with wave.open(str(out/('S_Arc'+name+'.wav')),'wb') as f:f.setnchannels(1);f.setsampwidth(2);f.setframerate(rate);f.writeframes(b''.join(data))
print('ELECTRIC_TETHER_SOURCE_READY')
