"""Original Quake-inspired fusion cannon. Run with Blender in background mode."""
import bpy, math, json
from pathlib import Path
from mathutils import Vector
root=Path(__file__).resolve().parents[3]
out=root/'ContentSource/BFGFusion';out.mkdir(parents=True,exist_ok=True)
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)

def material(name,color,metal,rough,emission=0):
    m=bpy.data.materials.new(name);m.diffuse_color=(*color,1);m.use_nodes=True
    p=m.node_tree.nodes.get('Principled BSDF');p.inputs['Base Color'].default_value=(*color,1)
    p.inputs['Metallic'].default_value=metal;p.inputs['Roughness'].default_value=rough
    p.inputs['Emission Color'].default_value=(*color,1);p.inputs['Emission Strength'].default_value=emission
    return m
steel=material('FusionSteel',(.15,.20,.21),.78,.32)
bronze=material('FusionBronze',(.40,.235,.085),.72,.36)
dark=material('FusionDark',(.018,.025,.03),.6,.42)
edge=material('FusionEdge',(.42,.48,.48),.83,.24)
energy=material('FusionEnergy',(.025,.85,.58),.22,.24,2.8)
rubber=material('FusionRubber',(.012,.016,.02),.05,.72)
warning=material('FusionWarning',(.9,.44,.06),.4,.4)

def finish(o,name,mat,bevel=.003):
    o.name=name;o.data.materials.append(mat)
    bpy.context.view_layer.objects.active=o
    bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
    if bevel:
        o.data.materials.append(edge if mat in (steel,bronze) else mat)
        b=o.modifiers.new('Machined bevel','BEVEL');b.width=bevel;b.segments=3;b.material=1
        bpy.ops.object.modifier_apply(modifier=b.name)
        n=o.modifiers.new('Weighted normals','WEIGHTED_NORMAL');n.keep_sharp=True;bpy.ops.object.modifier_apply(modifier=n.name)
    return o

def box(name,loc,size,mat,bevel=.003):
    bpy.ops.mesh.primitive_cube_add(size=1,location=loc);o=bpy.context.object;o.dimensions=size
    return finish(o,name,mat,bevel)

def cyl(name,loc,radius,depth,mat,axis='Y',verts=48):
    rot=(math.pi/2,0,0) if axis=='Y' else (0,math.pi/2,0) if axis=='X' else (0,0,0)
    bpy.ops.mesh.primitive_cylinder_add(vertices=verts,radius=radius,depth=depth,location=loc,rotation=rot)
    return finish(bpy.context.object,name,mat,.0015)

def mesh(name,vertices,faces,mat,bevel=.003):
    d=bpy.data.meshes.new(name);d.from_pydata(vertices,[],faces);d.update()
    o=bpy.data.objects.new(name,d);bpy.context.collection.objects.link(o);o.select_set(True)
    return finish(o,name,mat,bevel)

def ring(name,loc,outer,inner,depth,mat,n=48,axis='Y'):
    vs=[];fs=[]
    for d,r in [(-depth/2,outer),(depth/2,outer),(-depth/2,inner),(depth/2,inner)]:
        for i in range(n):
            a=i*math.tau/n+math.pi/8
            v=(r*math.cos(a),d,r*math.sin(a)) if axis=='Y' else (d,r*math.cos(a),r*math.sin(a))
            vs.append(tuple(loc[j]+v[j] for j in range(3)))
    for i in range(n):
        j=(i+1)%n
        fs.extend([(i,n+i,n+j,j),(2*n+i,2*n+j,3*n+j,3*n+i),(i,j,2*n+j,2*n+i),(n+i,3*n+i,3*n+j,n+j)])
    return mesh(name,vs,fs,mat,.0015)

def plate(name,side,x,thickness,outline,mat,bevel=.004):
    vs=[(side*(x+d),y,z) for d in (-thickness/2,thickness/2) for y,z in outline]
    n=len(outline);fs=[tuple(reversed(range(n))),tuple(range(n,2*n))]
    fs += [(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
    return mesh(name,vs,fs,mat,bevel)

def tube(name,points,radius,mat):
    c=bpy.data.curves.new(name,'CURVE');c.dimensions='3D';c.resolution_u=8;c.bevel_depth=radius;c.bevel_resolution=3
    s=c.splines.new('BEZIER');s.bezier_points.add(len(points)-1)
    for b,p in zip(s.bezier_points,points):b.co=p;b.handle_left_type='AUTO';b.handle_right_type='AUTO'
    o=bpy.data.objects.new(name,c);bpy.context.collection.objects.link(o);o.data.materials.append(mat)
    bpy.ops.object.select_all(action='DESELECT');o.select_set(True);bpy.context.view_layer.objects.active=o;bpy.ops.object.convert(target='MESH')
    return o

# Central chamber and two broad armour shoulders: a compact cannon, not a rifle.
cyl('Reactor pressure vessel',(0,.16,.125),.121,.47,dark,verts=12)
for y in [-.075,.0,.25,.365]:ring('Reactor induction band',(0,y,.125),.127,.117,.024,energy)
for y in [-.112,-.032,.295,.393]:ring('Containment clamp',(0,y,.125),.144,.116,.032,steel,n=12)
box('Upper receiver spine',(0,.12,.268),(.11,.40,.055),bronze,.01)
box('Lower receiver',(0,.085,.002),(.14,.38,.065),steel,.012)
box('Rear recoil pad',(0,-.145,.12),(.205,.055,.19),rubber,.016)
for side in [-1,1]:
    plate('Rear shoulder plate',side,.128,.038,[(-.15,.07),(-.12,.20),(-.06,.274),(.08,.254),(.17,.17),(.12,.035)],bronze,.008)
    plate('Forward armour cheek',side,.145,.034,[(.17,.045),(.22,.235),(.39,.265),(.53,.17),(.49,.035),(.34,-.005)],steel,.009)
    plate('Inset cheek panel',side,.166,.012,[(.24,.08),(.27,.19),(.37,.215),(.45,.16),(.43,.065)],bronze,.003)
    # Oversized circular capacitor gives the weapon its recognizable heavy profile.
    cyl('Capacitor axle',(side*.18,.08,.135),.069,.07,dark,'X')
    ring('Capacitor outer armour',(side*.213,.08,.135),.075,.052,.023,bronze,n=12,axis='X')
    ring('Capacitor luminous annulus',(side*.227,.08,.135),.051,.040,.007,energy,axis='X')
    cyl('Capacitor hub',(side*.23,.08,.135),.036,.012,steel,'X')
    cyl('Hub fastener',(side*.239,.08,.135),.012,.006,bronze,'X',8)
    for i in range(6):
        a=i*math.tau/6
        cyl('Captive fastener',(side*.229,.08+.063*math.cos(a),.135+.063*math.sin(a)),.004,.004,edge,'X',8)
    # Layered heat sink banks and externally routed copper power conduits.
    for y in [.20+i*.022 for i in range(9)]:
        box('Radiator fin',(side*.115,y,.015),(.075,.009,.064),dark,.001)
        box('Radiator bright edge',(side*.157,y,.018),(.008,.008,.045),edge,.001)
    tube('Armoured power conduit',[(side*.10,-.10,.04),(side*.21,-.035,.005),(side*.205,.24,-.015),(side*.15,.40,.04)],.011,bronze)
    tube('Reactor pilot channel',[(side*.121,-.08,.245),(side*.13,.06,.249),(side*.13,.18,.227),(side*.15,.28,.22)],.005,energy)
    for y,z in [(-.08,.21),(-.09,.08),(.27,.18),(.42,.13)]:
        cyl('Panel screw',(side*.19,y,z),.006,.008,dark,'X',8)

# Forward focusing collar with a deep recessed aperture and four massive split jaws.
ring('Focusing collar',(0,.48,.125),.139,.092,.15,dark,n=12)
ring('Inner conductor',(0,.545,.125),.095,.071,.035,energy)
ring('Bore outer lip',(0,.615,.125),.108,.077,.042,edge,n=12)
ring('Bore internal sleeve',(0,.574,.125),.079,.064,.092,dark)
cyl('Deep emitter lens',(0,.53,.125),.063,.009,energy)
for side in [-1,1]:
    plate('Split muzzle jaw',side,.115,.065,[(.402,.068),(.45,.24),(.54,.28),(.65,.215),(.67,.12),(.63,.01),(.53,-.015),(.46,.045)],bronze,.009)
    plate('Jaw inset channel',side,.151,.008,[(.49,.11),(.52,.205),(.57,.22),(.62,.17),(.60,.08)],dark,.003)
    box('Jaw glow slot',(side*.157,.558,.145),(.005,.068,.016),energy,.002)
    for y,z in [(.48,.08),(.54,.244),(.635,.135)]:cyl('Jaw anchor',(side*.155,y,z),.010,.008,steel,'X',8)
for y in [.45,.478,.506,.534]:
    box('Upper barrel heatsink',(0,y,.252),(.13,.013,.025),steel,.002)
# Exposed crown energy window is visible from first person.
box('Crown dark bed',(0,.12,.298),(.072,.22,.011),dark,.004)
for y in [.035,.074,.113,.152,.191]:box('Crown energy cell',(0,y,.307),(.056,.024,.010),energy,.003)
for side in [-1,1]:box('Crown protection rail',(side*.049,.12,.314),(.012,.255,.020),steel,.003)
box('Rear caution stripe',(0,-.064,.299),(.073,.018,.006),warning,.001)
# Conventional right-hand grip, left palm under a forward saddle.
grip=box('Right pistol grip',(0,-.025,-.055),(.05,.066,.15),rubber,.009);grip.rotation_euler.x=-.15
for z in [-.11,-.087,-.064,-.041]:box('Grip rib',(0,-.061,z),(.052,.008,.009),steel,.001)
box('Trigger guard bottom',(0,.047,-.056),(.022,.10,.014),steel)
box('Trigger guard front',(0,.099,-.025),(.022,.014,.075),steel)
box('Trigger',(0,.027,-.023),(.012,.010,.038),bronze)
box('Support hand saddle',(0,.285,-.031),(.105,.19,.045),rubber,.012)
for y in [.215,.245,.275,.305,.335]:box('Saddle grip rib',(0,y,-.056),(.11,.009,.009),steel,.001)
# Small raised markings, restrained enough to read as manufacturing detail.
bpy.ops.object.text_add(location=(0,-.039,.301))
t=bpy.context.object;t.data.body='FUSION // 10K';t.data.align_x='CENTER';t.data.size=.013;t.data.extrude=.00025;t.data.materials.append(edge)
bpy.ops.object.convert(target='MESH')
bpy.ops.object.select_all(action='SELECT');bpy.context.view_layer.objects.active=bpy.context.selected_objects[0]
bpy.ops.object.join();o=bpy.context.object;o.name='SM_BFGFusion'
bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT')
bpy.ops.mesh.normals_make_consistent(inside=False)
bpy.ops.uv.smart_project(angle_limit=math.radians(66),island_margin=.008)
bpy.ops.object.mode_set(mode='OBJECT')
n=o.modifiers.new('Final armour normals','WEIGHTED_NORMAL');n.keep_sharp=True
bpy.ops.object.modifier_apply(modifier=n.name)
bpy.context.scene.cursor.location=(0,0,0);bpy.ops.object.origin_set(type='ORIGIN_CURSOR')
bpy.ops.object.transform_apply(location=False,rotation=True,scale=True)
# Same FBX convention as the working railgun, resulting in a +Y Unreal barrel.
o.rotation_euler.z=math.pi;bpy.ops.object.transform_apply(location=False,rotation=True,scale=True)
bpy.ops.export_scene.fbx(filepath=str(out/'SM_BFGFusion.fbx'),use_selection=True,object_types={'MESH'},add_leaf_bones=False,axis_forward='-Y',axis_up='Z',bake_anim=False,mesh_smooth_type='FACE')
bpy.ops.wm.save_as_mainfile(filepath=str(out/'BFGFusion_Source.blend'))
(out/'MeshStats.json').write_text(json.dumps({'vertices':len(o.data.vertices),'polygons':len(o.data.polygons),'materials':[m.name for m in o.data.materials]},indent=2))
print('BFG_FUSION_MESH_READY',len(o.data.polygons))
