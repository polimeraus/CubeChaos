"""Run with Blender --background --python. Original editable ride meshes, in metres."""
import bpy, math, os
from pathlib import Path
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'ContentSource/BallerinaRide'
OUT.mkdir(parents=True, exist_ok=True)
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)

def mat(name, color, metal=0, rough=.4):
    m = bpy.data.materials.new(name); m.diffuse_color = (*color, 1); m.use_nodes = True
    p = m.node_tree.nodes.get('Principled BSDF')
    p.inputs['Base Color'].default_value = (*color, 1)
    p.inputs['Metallic'].default_value = metal; p.inputs['Roughness'].default_value = rough
    return m

red=mat('CrimsonVelvet',(.48,.012,.033),.12,.33)
skin=mat('Porcelain',(.86,.59,.37),.05,.36)
hair=mat('MidnightHair',(.016,.010,.022),.18,.26)
gold=mat('Brass',(.83,.46,.085),.72,.25)
white=mat('Ivory',(.91,.88,.72),.08,.26)
black=mat('Lashes',(.008,.005,.011),.1,.35)
iris=mat('JadeEyes',(.018,.21,.16),.25,.22)
lips=mat('Rouge',(.56,.017,.035),.12,.3)
teal=mat('SeatPaint',(.016,.32,.34),.45,.3)
upholstery=mat('SeatCushion',(.53,.19,.028),.0,.6)
steel=mat('Chrome',(.55,.65,.71),.88,.22)
floor=mat('TreadPlate',(.09,.13,.16),.7,.5)

def finish(o, name, material, smooth=True):
    o.name=name; o.data.materials.append(material)
    if smooth:
        for p in o.data.polygons:p.use_smooth=True
    return o

def ell(name,loc,scale,material):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=32, ring_count=20, location=loc)
    o=bpy.context.object; o.scale=scale
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    return finish(o,name,material)

def box(name,loc,size,material,bevel=.025):
    bpy.ops.mesh.primitive_cube_add(size=1,location=loc)
    o=bpy.context.object;o.dimensions=size
    bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
    finish(o,name,material,False)
    b=o.modifiers.new('Soft fabricated edges','BEVEL');b.width=bevel;b.segments=3
    bpy.context.view_layer.objects.active=o;bpy.ops.object.modifier_apply(modifier=b.name)
    return o

def tube(name,points,radius,material):
    c=bpy.data.curves.new(name,'CURVE');c.dimensions='3D';c.resolution_u=16
    c.bevel_depth=radius;c.bevel_resolution=3
    s=c.splines.new('BEZIER');s.bezier_points.add(len(points)-1)
    for p,co in zip(s.bezier_points,points):
        p.co=co;p.handle_left_type='VECTOR' if name=='Side edge' else 'AUTO';p.handle_right_type=p.handle_left_type
    o=bpy.data.objects.new(name,c);bpy.context.collection.objects.link(o);o.data.materials.append(material)
    bpy.context.view_layer.objects.active=o;o.select_set(True)
    bpy.ops.object.convert(target='MESH');o.select_set(False)
    return o

def loft(name,rings,material,segments=64,pleats=0):
    # rings are z, x-radius, y-radius, y offset. Figure faces -Y in Blender.
    v=[];f=[]
    for z,rx,ry,cy in rings:
        for j in range(segments):
            a=j*math.tau/segments;k=1+pleats*math.cos(a*16)
            v.append((rx*math.cos(a)*k,cy+ry*math.sin(a)*k,z))
    for i in range(len(rings)-1):
        for j in range(segments):
            a=i*segments+j;b=i*segments+(j+1)%segments
            f.append((a,b,b+segments,a+segments))
    f.append(tuple(reversed(range(segments))))
    f.append(tuple((len(rings)-1)*segments+j for j in range(segments)))
    mesh=bpy.data.meshes.new(name);mesh.from_pydata(v,[],f);mesh.update()
    o=bpy.data.objects.new(name,mesh);bpy.context.collection.objects.link(o);finish(o,name,material)
    sub=o.modifiers.new('Tailored curves','SUBSURF');sub.levels=2
    bpy.context.view_layer.objects.active=o;bpy.ops.object.modifier_apply(modifier=sub.name)
    return o

loft('Gathered peplum',[(0,.87,.57,0),(.035,.9,.59,0),(.12,.82,.53,0),(.23,.60,.39,0),(.27,.57,.38,0)],red,96,.045)
loft('Fitted bodice',[(.15,.55,.36,0),(.22,.57,.39,0),(.47,.55,.37,0),(.80,.65,.40,0),(1.10,.72,.45,-.02),(1.35,.79,.39,0),(1.47,.72,.35,0),(1.55,.46,.27,0),(1.58,.34,.24,0)],red)
ell('Neck',(0,0,1.65),(.225,.22,.39),skin)
ell('Head',(0,-.01,2.17),(.385,.325,.49),skin)
ell('Chin',(0,-.205,1.91),(.225,.15,.18),skin)
for s in [-1,1]:
    ell('Cheek',(.20*s,-.249,2.10),(.155,.092,.15),skin)
    ell('Ear',(.374*s,0,2.15),(.073,.09,.14),skin)
    ell('Earring',(.385*s,-.025,1.997),(.042,.042,.082),gold)
    ell('Eye',(.151*s,-.306,2.235),(.116,.045,.053),white)
    ell('Iris',(.148*s,-.347,2.237),(.041,.013,.042),iris)
    ell('Pupil',(.148*s,-.359,2.237),(.019,.006,.025),black)
    ell('Eye glint',(.135*s,-.366,2.253),(.01,.004,.012),white)
    tube('Upper lashes',[(s*.055,-.345,2.24),(s*.14,-.355,2.28),(s*.25,-.318,2.25)],.009,black)
    tube('Arched brow',[(s*.063,-.308,2.348),(s*.14,-.325,2.371),(s*.252,-.29,2.342)],.021,hair)
ell('Nose bridge',(0,-.312,2.16),(.061,.058,.142),skin)
ell('Nose tip',(0,-.389,2.097),(.070,.073,.049),skin)
for s in [-1,1]:ell('Nostril wing',(.054*s,-.345,2.073),(.038,.036,.031),skin)
tube('Cupid upper lip',[(-.11,-.302,1.99),(-.035,-.343,2.006),(0,-.342,1.995),(.035,-.343,2.006),(.11,-.302,1.99)],.016,lips)
tube('Lower lip',[(-.102,-.307,1.987),(0,-.345,1.963),(.102,-.307,1.987)],.023,lips)
# Sculpted waved bob, framing the forehead without covering the eyes.
ell('Back coiffure',(0,.125,2.30),(.415,.29,.40),hair)
ell('Crown',(0,.008,2.545),(.36,.285,.205),hair)
for s in [-1,1]:
    for i in range(4):
        o=ell('Waved side lock',(s*(.33+.018*math.sin(i)),.015-i*.008,2.42-i*.105),(.105,.29,.12),hair)
    for i in range(3):
        tube('Sculpted wave',[(s*.36,-.12,2.46-i*.12),(s*.4,-.19,2.40-i*.12),(s*.35,-.22,2.35-i*.12)],.021,hair)
for i in range(4):
    o=ell('Swept fringe',(-.22+i*.12,-.22,2.535+.035*math.sin(i)),(.15,.105,.087),hair)
    o.rotation_euler[1]=-.28
tube('Gold hair ribbon',[(-.35,.01,2.54),(-.23,-.035,2.70),(0,-.04,2.735),(.23,-.035,2.70),(.35,.01,2.54)],.016,gold)
for s in [-1,1]:
    # Continuous puff sleeve with longitudinal fabric flutes, no ball-joint look.
    ell('Tailored shoulder',(s*.64,0,1.40),(.28,.285,.27),red)
    rings=[(.59,1.39,.27),(.73,1.40,.32),(.97,1.25,.29),(1.25,1.08,.245),(1.52,.95,.25),(1.79,.88,.22),(2.02,.87,.14),(2.08,.86,.13)]
    verts=[];faces=[];n=40
    for x,z,r in rings:
        for j in range(n):
            a=math.tau*j/n;k=1+.055*math.cos(a*10)
            verts.append((s*x,r*math.cos(a)*.85,z+r*math.sin(a)*k))
    for i in range(len(rings)-1):
        for j in range(n):
            q=i*n+j;t=i*n+(j+1)%n;face=(q,t,t+n,q+n)
            faces.append(face if s>0 else tuple(reversed(face)))
    mesh=bpy.data.meshes.new('Sleeve');mesh.from_pydata(verts,[],faces);mesh.update()
    o=bpy.data.objects.new('Billowing sleeve',mesh);bpy.context.collection.objects.link(o);finish(o,o.name,red)
    sub=o.modifiers.new('Fabric smoothing','SUBSURF');sub.levels=2
    bpy.context.view_layer.objects.active=o;bpy.ops.object.modifier_apply(modifier=sub.name)
    tube('Gold cuff',[(s*2.055,.14*math.cos(a),.86+.14*math.sin(a)) for a in [j*math.tau/24 for j in range(25)]],.016,gold)
    ell('Wrist',(s*2.13,-.006,.855),(.14,.10,.084),skin)
    palm=ell('Hand',(s*2.31,-.025,.81),(.21,.12,.070),skin);palm.rotation_euler[1]=s*.20
    for k in range(4):
        y=-.108+k*.058;length=[.18,.24,.23,.18][k]
        tube('Finger',[(s*2.43,y,.797),(s*(2.47+length*.5),y-.015,.745),(s*(2.47+length),y-.023,.705)],.026,skin)
    tube('Thumb',[(s*2.25,-.115,.82),(s*2.34,-.206,.77),(s*2.43,-.217,.75)],.037,skin)
    tube('Bodice seam',[(s*.34,-.28,.24),(s*.41,-.337,.75),(s*.42,-.36,1.17)],.011,gold)
# Scalloped neckline and waist sash.
tube('Neckline',[(-.63,-.26,1.48),(-.39,-.38,1.36),(0,-.454,1.28),(.39,-.38,1.36),(.63,-.26,1.48)],.021,gold)
ell('Brooch',(0,-.47,1.31),(.055,.021,.07),gold)
loft('Waist sash',[(.21,.582,.393,0),(.225,.593,.398,0),(.29,.59,.397,0),(.30,.58,.392,0)],gold)

# Fuse the porcelain facial forms into one smooth sculpt rather than visible intersecting balls.
bpy.ops.object.select_all(action='DESELECT')
head_parts=[o for o in bpy.context.scene.objects if o.type=='MESH' and o.data.materials and o.data.materials[0]==skin and o.location.z>1.6]
for o in head_parts:o.select_set(True)
bpy.context.view_layer.objects.active=head_parts[0];bpy.ops.object.join()
head=bpy.context.object;head.name='Sculpted porcelain face and neck'
remesh=head.modifiers.new('Blend facial volumes','REMESH');remesh.mode='VOXEL';remesh.voxel_size=.012
bpy.ops.object.modifier_apply(modifier=remesh.name)
smooth=head.modifiers.new('Sculpt polish','SMOOTH');smooth.factor=.65;smooth.iterations=6
bpy.ops.object.modifier_apply(modifier=smooth.name)
dec=head.modifiers.new('Game mesh','DECIMATE');dec.ratio=.55
bpy.ops.object.modifier_apply(modifier=dec.name)
for p in head.data.polygons:p.use_smooth=True

def join_export(name,objects):
    bpy.ops.object.select_all(action='DESELECT')
    for o in objects:o.select_set(True)
    bpy.context.view_layer.objects.active=objects[0];bpy.ops.object.join()
    o=bpy.context.object;o.name=name
    bpy.context.scene.cursor.location=(0,0,0);bpy.ops.object.origin_set(type='ORIGIN_CURSOR')
    # +X is forward in Unreal; figure originally looks -Y.
    o.rotation_euler[2]=math.pi/2
    bpy.ops.object.transform_apply(location=False,rotation=True,scale=True)
    bpy.ops.export_scene.fbx(filepath=str(OUT/(name+'.fbx')),use_selection=True,object_types={'MESH'},add_leaf_bones=False,axis_forward='-Y',axis_up='Z',bake_anim=False,mesh_smooth_type='FACE')
    return o

figure=join_export('SM_BallerinaFigure',list(bpy.context.scene.objects))
figure.hide_set(True)
before=set(bpy.context.scene.objects)
box('Footwell',(0,0,.055),(1.65,1.56,.11),floor)
box('Seat cushion',(-.23,0,.51),(.67,1.38,.17),upholstery,.065)
back=box('Upholstered back',(-.67,0,.91),(.14,1.38,.83),upholstery,.06);back.rotation_euler[1]=-.075
for s in [-1,1]:
    # Sloping side panels: low at entry, tall at back, similar to the photographs.
    v=[(-.8,s*.76,.1),(.79,s*.76,.1),(.66,s*.76,.39),(-.58,s*.76,1.32)]
    me=bpy.data.meshes.new('Side');me.from_pydata(v,[],[(0,1,2,3)]);me.update()
    o=bpy.data.objects.new('Sloped enamel cheek',me);bpy.context.collection.objects.link(o);finish(o,o.name,teal,False)
    so=o.modifiers.new('Panel gauge','SOLIDIFY');so.thickness=.045
    be=o.modifiers.new('Rolled edge','BEVEL');be.width=.025;be.segments=3
    bpy.context.view_layer.objects.active=o
    for mo in list(o.modifiers):bpy.ops.object.modifier_apply(modifier=mo.name)
    tube('Side edge',v+[v[0]],.024,steel)
    tube('Arm rail',[(-.55,s*.665,1.09),(.10,s*.665,.78),(.60,s*.665,.38)],.026,steel)
    # Brass flash on outer cheeks.
    tube('Side flash',[(-.50,s*.794,.75),(.40,s*.794,.28)],.045,gold)
    for x in ([-.56,-.43,-.30,-.17,-.04,.09,.22,.35,.48,.61] if s==1 else []):
        for y in [-.54,-.27,0,.27,.54]:
            tread=box('Raised tread',(x,y,.115),(.065,.024,.012),steel,.004);tread.rotation_euler[2]=.65
    box('Seat support',(-.42,s*.53,.28),(.45,.07,.38),steel)
tube('Safety lap bar',[(.20,-.68,.27),(.32,-.68,.78),(.32,.68,.78),(.20,.68,.27)],.033,steel)
box('Back shell',(-.78,0,.69),(.075,1.58,1.23),teal)
seat=join_export('SM_BallerinaSeat',[o for o in bpy.context.scene.objects if o not in before])
# Seat was modelled facing +X already, undo the common figure rotation.
seat.rotation_euler[2]=-math.pi/2
bpy.ops.object.transform_apply(location=False,rotation=True,scale=True)
bpy.ops.export_scene.fbx(filepath=str(OUT/'SM_BallerinaSeat.fbx'),use_selection=True,object_types={'MESH'},add_leaf_bones=False,axis_forward='-Y',axis_up='Z',bake_anim=False,mesh_smooth_type='FACE')
figure.hide_set(False);seat.location=(0,3,0)
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'BallerinaRide_Source.blend'))
print('BALLERINA_MESHES_READY',OUT)
