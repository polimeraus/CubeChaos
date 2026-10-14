"""Reconstructed Q3-style BFG10K; original geometry, no extracted game assets.
Run in Blender. Metres, grip at origin, barrel along Unreal +Y after export.
"""
import bpy, math
from pathlib import Path

out=Path(__file__).resolve().parents[3]/'ContentSource/Railgun'
out.mkdir(parents=True,exist_ok=True)
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)

def material(name,color,metal,rough):
    m=bpy.data.materials.new(name);m.diffuse_color=(*color,1);m.use_nodes=True
    p=m.node_tree.nodes.get('Principled BSDF')
    p.inputs['Base Color'].default_value=(*color,1)
    p.inputs['Metallic'].default_value=metal;p.inputs['Roughness'].default_value=rough
    return m
steel=material('BFGSteel',(.27,.30,.25),.8,.38)
bronze=material('BFGBronze',(.35,.16,.048),.7,.4)
dark=material('BFGDark',(.022,.029,.024),.45,.5)
green=material('BFGEnergy',(.03,1,.12),.25,.28)
rubber=material('BFGRubber',(.012,.016,.013),.05,.68)

def finish(o,name,mat,bevel=.003):
    o.name=name;o.data.materials.append(mat)
    bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
    if bevel:
        b=o.modifiers.new('Machined edges','BEVEL');b.width=bevel;b.segments=3
        bpy.ops.object.modifier_apply(modifier=b.name)
        n=o.modifiers.new('Weighted normals','WEIGHTED_NORMAL');bpy.ops.object.modifier_apply(modifier=n.name)
    return o
def box(name,loc,size,mat,bevel=.003):
    bpy.ops.mesh.primitive_cube_add(size=1,location=loc);o=bpy.context.object;o.dimensions=size
    return finish(o,name,mat,bevel)
def cyl(name,loc,radius,depth,mat,axis='Y',verts=32):
    rotation=(math.pi/2,0,0) if axis=='Y' else (0,math.pi/2,0)
    bpy.ops.mesh.primitive_cylinder_add(vertices=verts,radius=radius,depth=depth,location=loc,rotation=rotation)
    return finish(bpy.context.object,name,mat,.002)
def ring(name,loc,outer,inner,depth,mat,axis='Y',n=32):
    vs=[];fs=[]
    for d,r in [(-depth/2,outer),(depth/2,outer),(-depth/2,inner),(depth/2,inner)]:
        for i in range(n):
            a=i*math.tau/n
            p=(r*math.cos(a),d,r*math.sin(a)) if axis=='Y' else (d,r*math.cos(a),r*math.sin(a))
            vs.append(tuple(loc[j]+p[j] for j in range(3)))
    for i in range(n):
        j=(i+1)%n
        fs.extend([(i,n+i,n+j,j),(2*n+i,2*n+j,3*n+j,3*n+i),
                   (i,j,2*n+j,2*n+i),(n+i,3*n+i,3*n+j,n+j)])
    mesh=bpy.data.meshes.new(name);mesh.from_pydata(vs,[],fs);mesh.update()
    o=bpy.data.objects.new(name,mesh);bpy.context.collection.objects.link(o)
    bpy.context.view_layer.objects.active=o;o.select_set(True)
    return finish(o,name,mat,.001)

# Broad olive/bronze chassis, deep front shroud, exposed side capacitor wheels.
box('Main armoured chassis',(0,.16,.12),(.225,.57,.20),steel,.025)
box('Rear bronze endcap',(0,-.133,.12),(.19,.045,.15),bronze,.012)
box('Lower receiver',(0,.09,.032),(.15,.34,.07),dark,.016)
box('Top reactor cover',(0,.14,.227),(.145,.38,.04),bronze,.008)
for y in [-.025,.135,.295]:
    box('Reactor clamp',(0,y,.252),(.17,.018,.025),steel,.004)
    for x in [-.091,.091]:box('Clamp drop',(x,y,.217),(.022,.018,.07),steel)
box('Rear cooling panel',(0,-.075,.231),(.16,.078,.02),dark)
for y in [-.103+i*.009 for i in range(8)]:box('Cooling rib',(0,y,.244),(.15,.0035,.012),steel,.001)
for x in [-.117,.117]:
    box('Energy channel',(x,.16,.103),(.006,.46,.014),green,.001)
    box('Lower channel guard',(x*1.02,.16,.085),(.012,.47,.016),dark)
    for y in [-.05,.28]:
        cyl('Capacitor hub',(x*1.04,y,.155),.063,.034,dark,'X')
        ring('Capacitor rim',(x*1.15,y,.155),.068,.046,.024,steel,'X')
        ring('Green reactor inset',(x*1.17,y,.155),.045,.035,.004,green,'X')
        cyl('Hub centre',(x*1.2,y,.155),.034,.008,dark,'X')
        for i in range(8):
            a=i*math.tau/8
            cyl('Rim bolt',(x*1.28,y+.057*math.cos(a),.155+.057*math.sin(a)),.004,.006,bronze,'X',12)
    for y in [.055,.078,.101]:box('Side vent',(x*1.04,y,.208),(.008,.012,.025),dark,.001)

cyl('Front plasma chamber',(0,.44,.12),.113,.24,dark,verts=8)
ring('Bronze muzzle shroud',(0,.475,.12),.13,.084,.23,bronze,n=8)
ring('Front steel lip',(0,.6,.12),.101,.06,.042,steel,n=8)
ring('Recessed plasma aperture',(0,.605,.12),.058,.046,.006,green)
cyl('Dark muzzle bore',(0,.576,.12),.046,.007,rubber)
for x in [-.082,.082]:
    box('Muzzle side rail',(x,.48,.18),(.024,.235,.028),steel,.005)
for y in [.398,.43,.462,.494,.526,.558]:
    box('Shroud cooling slot',(0,y,.248),(.10,.012,.009),dark,.001)
box('Front support saddle',(0,.26,-.006),(.09,.18,.042),rubber,.01)
grip=box('Pistol grip',(0,-.025,-.045),(.05,.065,.145),rubber,.01);grip.rotation_euler[0]=-.15
for z in [-.095,-.073,-.051,-.029]:box('Grip bands',(0,-.059,z),(.052,.009,.008),bronze,.002)
box('Trigger guard base',(0,.051,-.043),(.024,.095,.014),bronze)
box('Trigger guard front',(0,.096,-.012),(.024,.014,.066),bronze)
box('Trigger',(0,.033,-.009),(.012,.009,.028),steel)
# A readable identification plate, baked as geometry rather than a runtime label.
bpy.ops.object.text_add(location=(0,.075,.253),rotation=(0,0,0))
t=bpy.context.object;t.data.body='BFG 10K';t.data.align_x='CENTER';t.data.size=.025;t.data.extrude=.0003
t.data.materials.append(steel);bpy.ops.object.convert(target='MESH')
bpy.ops.object.select_all(action='SELECT');bpy.context.view_layer.objects.active=bpy.context.selected_objects[0]
bpy.ops.object.join();o=bpy.context.object;o.name='SM_BFG10K'
bpy.context.scene.cursor.location=(0,0,0);bpy.ops.object.origin_set(type='ORIGIN_CURSOR')
bpy.ops.object.transform_apply(location=False,rotation=True,scale=True)
o.rotation_euler[2]=math.pi;bpy.ops.object.transform_apply(location=False,rotation=True,scale=True)
bpy.ops.export_scene.fbx(filepath=str(out/'SM_BFG10K.fbx'),use_selection=True,object_types={'MESH'},
    add_leaf_bones=False,axis_forward='-Y',axis_up='Z',bake_anim=False,mesh_smooth_type='FACE')
bpy.ops.wm.save_as_mainfile(filepath=str(out/'BFG_Source.blend'))
print('BFG_MESH_READY')
