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

red=mat('CrimsonVelvet',(.41,.012,.043),.035,.54)
skin=mat('Porcelain',(.84,.58,.44),.025,.44)
hair=mat('MidnightHair',(.025,.014,.022),.09,.34)
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
    bpy.ops.mesh.primitive_uv_sphere_add(segments=28, ring_count=16, location=loc)
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
    sub=o.modifiers.new('Tailored curves','SUBSURF');sub.levels=1
    bpy.context.view_layer.objects.active=o;bpy.ops.object.modifier_apply(modifier=sub.name)
    return o

loft('Gathered peplum',[(0,.87,.57,0),(.035,.9,.59,0),(.12,.82,.53,0),(.23,.60,.39,0),(.27,.57,.38,0)],red,96,.045)
# Shape the neckline in the same surface as the chest. The piping, ivory
# scallops and exposed shoulders meet along shared vertices.
rings=[(.15,.55,.36,0),(.25,.57,.39,0),(.47,.55,.37,0),(.80,.65,.40,0),
       (1.10,.72,.45,0),(1.405,.766,.441,.19),(1.426,.769,.445,.19),
       (1.475,.75,.423,.18),(1.51,.70,.397,.13),(1.57,.44,.29,.065),(1.60,.22,.213,0)]
verts=[];faces=[];slots=[];segments=96
for row,(z,rx,ry,dip) in enumerate(rings):
    for j in range(segments):
        a=math.tau*j/segments
        scallop=.014*math.cos(18*a) if row==7 else 0
        verts.append((rx*math.cos(a),ry*math.sin(a),z-dip*math.sin(a)**2+scallop))
        if row:
            k=(row-1)*segments+j;t=(row-1)*segments+(j+1)%segments
            faces.append((k,t,t+segments,k+segments))
            slots.append(0 if row<=5 else 1 if row==6 else 2 if row==7 else 3)
mesh=bpy.data.meshes.new('Sweetheart bodice');mesh.from_pydata(verts,[],faces);mesh.update()
bodice=bpy.data.objects.new(mesh.name,mesh);bpy.context.collection.objects.link(bodice)
for fabric in (red,gold,white,skin):mesh.materials.append(fabric)
for polygon,slot in zip(mesh.polygons,slots):
    polygon.material_index=slot;polygon.use_smooth=True
ell('Neck',(0,0,1.65),(.225,.22,.39),skin)
# A continuous oval narrows through the jaw and grows flatter at the temples.
face=loft('Porcelain face',[(1.76,.065,.09,-.08),(1.81,.17,.17,-.09),
    (1.91,.255,.225,-.08),(2.04,.325,.276,-.05),(2.15,.376,.305,-.027),
    (2.27,.376,.323,-.012),(2.42,.365,.31,0),(2.55,.304,.26,.013),
    (2.65,.115,.11,.02)],skin)
for s in [-1,1]:
    ell('Ear',(.374*s,0,2.15),(.073,.09,.14),skin)
    ell('Earring',(.385*s,-.025,1.997),(.042,.042,.082),gold)
    eye=ell('Eye',(.15*s,-.303,2.235),(.100,.026,.040),white)
    eye.rotation_euler.z=s*.32
    ell('Iris',(.145*s,-.330,2.237),(.031,.009,.033),iris)
    ell('Pupil',(.145*s,-.338,2.237),(.016,.005,.022),black)
    ell('Eye glint',(.138*s,-.343,2.248),(.008,.004,.008),white)
    tube('Upper lashes',[(s*.060,-.335,2.235),(s*.147,-.323,2.273),(s*.246,-.278,2.25)],.009,black)
    tube('Arched brow',[(s*.067,-.317,2.34),(s*.151,-.302,2.368),(s*.248,-.246,2.345)],.012,hair)
ell('Nose bridge',(0,-.308,2.16),(.047,.048,.124),skin)
ell('Nose tip',(0,-.365,2.097),(.057,.046,.045),skin)
for s in [-1,1]:ell('Nostril wing',(.054*s,-.345,2.073),(.038,.036,.031),skin)
tube('Cupid upper lip',[(-.11,-.302,1.99),(-.035,-.343,2.006),(0,-.342,1.995),(.035,-.343,2.006),(.11,-.302,1.99)],.016,lips)
tube('Lower lip',[(-.102,-.307,1.987),(0,-.345,1.963),(.102,-.307,1.987)],.023,lips)
# Swept hair and a high coiled chignon give the figure a ballet silhouette.
ell('Back coiffure',(0,.125,2.30),(.415,.29,.40),hair)
ell('Crown',(0,.008,2.545),(.36,.285,.205),hair)
ell('Chignon',(0,.17,2.73),(.225,.19,.205),hair)
for s in [-1,1]:
    lock=ell('Temple sweep',(s*.326,.01,2.39),(.09,.245,.222),hair)
    lock.rotation_euler.x=-.30
    for i in range(3):
        tube('Combed wave',[(s*(.06+i*.035),-.23,2.61-i*.019),
            (s*.29,-.187,2.56-i*.035),(s*.373,-.04,2.46-i*.045),
            (s*.32,.18,2.48-i*.032)],.016,hair)
    tube('Chignon ribbon',[(s*.185,.13,2.67),(s*.16,.012,2.75),
        (s*.055,-.004,2.79),(0,-.005,2.77)],.018,gold)
tube('Gold hair ribbon',[(-.35,.01,2.54),(-.23,-.035,2.70),(0,-.04,2.735),(.23,-.035,2.70),(.35,.01,2.54)],.016,gold)
for s in [-1,1]:
    verts=[];faces=[];slots=[];n=48
    for row,(x,z,r) in enumerate([(.54,1.40,.16),(.65,1.40,.245),(.79,1.377,.262),
                                 (.91,1.32,.25),(.945,1.306,.237),(.955,1.302,.224)]):
        for j in range(n):
            a=j*math.tau/n
            fold=1+.055*math.cos(a*8)
            verts.append((s*(x+.28*r*math.sin(a)),r*math.cos(a)*fold,z+r*math.sin(a)*fold))
            if row:
                k=(row-1)*n+j;t=(row-1)*n+(j+1)%n
                f=(k,t,t+n,k+n);faces.append(f if s>0 else tuple(reversed(f)))
                slots.append(0 if row<=3 else 1 if row==4 else 2)
    mesh=bpy.data.meshes.new('Pleated cap');mesh.from_pydata(verts,[],faces);mesh.update()
    cap=bpy.data.objects.new(mesh.name,mesh);bpy.context.collection.objects.link(cap)
    for fabric in (red,white,gold):mesh.materials.append(fabric)
    for polygon,slot in zip(mesh.polygons,slots):
        polygon.material_index=slot;polygon.use_smooth=True
    rings=[(.71,1.39,.184),(.88,1.34,.197),(1.09,1.20,.181),(1.34,1.05,.143),
        (1.48,1.005,.123),(1.61,.95,.139),(1.77,.914,.132),(1.99,.87,.085),(2.16,.85,.077)]
    verts=[];faces=[];n=40
    for x,z,r in rings:
        for j in range(n):
            a=math.tau*j/n
            verts.append((s*x,r*math.cos(a)*.88,z+r*math.sin(a)))
    for i in range(len(rings)-1):
        for j in range(n):
            q=i*n+j;t=i*n+(j+1)%n;face=(q,t,t+n,q+n)
            faces.append(face if s>0 else tuple(reversed(face)))
    mesh=bpy.data.meshes.new('Sleeve');mesh.from_pydata(verts,[],faces);mesh.update()
    o=bpy.data.objects.new('Porcelain arm',mesh);bpy.context.collection.objects.link(o);finish(o,o.name,skin)
    sub=o.modifiers.new('Arm contour','SUBSURF');sub.levels=1
    bpy.context.view_layer.objects.active=o;bpy.ops.object.modifier_apply(modifier=sub.name)
    tube('Gold bracelet',[(s*2.07,.081*math.cos(a),.86+.084*math.sin(a)) for a in [j*math.tau/24 for j in range(25)]],.012,gold)
    ell('Wrist',(s*2.13,-.006,.855),(.14,.10,.084),skin)
    palm=ell('Hand',(s*2.31,-.025,.81),(.21,.12,.070),skin);palm.rotation_euler[1]=s*.20
    for k in range(4):
        y=-.108+k*.058;length=[.18,.24,.23,.18][k]
        tip=(s*(2.47+length),y-.023,.705+.015*k)
        tube('Finger',[(s*2.43,y,.797),(s*(2.47+length*.5),y-.015,.753+.009*k),tip],.024,skin)
        ell('Fingertip',tip,(.026,.024,.024),skin)
    tube('Thumb',[(s*2.25,-.115,.82),(s*2.34,-.206,.77),(s*2.43,-.217,.75)],.037,skin)
    tube('Bodice seam',[(s*.34,-.32,.28),(s*.36,-.35,.74),(s*.42,-.39,1.17)],.012,gold)
    # Embroidered fronds follow the chest and frame the jewel.
    for i in range(3):
        x=.13+i*.125
        leaf=[]
        for dx,dz in [(-.046,-.044),(-.027,.007),(.042,.042),(.021,-.006),(-.046,-.044)]:
            px=x+dx
            leaf.append((s*px,-.45*math.sqrt(1-(px/.745)**2)-.004,1.135+i*.030+dz))
        tube('Leaf embroidery',leaf,.009,gold)
ell('Brooch',(0,-.442,1.216),(.062,.022,.075),gold)
ell('Brooch jade',(0,-.461,1.216),(.043,.016,.054),iris)
loft('Ivory underskirt',[(.015,.96,.62,0),(.045,.99,.64,0),(.085,.91,.59,0),(.21,.58,.38,0)],white,96,.06)
loft('Waist sash',[(.21,.582,.393,0),(.225,.593,.398,0),(.29,.59,.397,0),(.30,.58,.392,0)],gold)

# Fuse the porcelain facial forms into one smooth sculpt rather than visible intersecting balls.
bpy.ops.object.select_all(action='DESELECT')
head_parts=[o for o in bpy.context.scene.objects if o.type=='MESH' and o.data.materials and o.data.materials[0]==skin and (o==face or o.location.z>1.6)]
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

# Lift one arm into a soft diagonal and open the other. Deform in world space
# so the fingers, bracelets and shoulder seams follow the same line.
for obj in bpy.context.scene.objects:
    if obj.type!='MESH':continue
    world=obj.matrix_world.copy()
    local=world.inverted()
    for vertex in obj.data.vertices:
        p=world@vertex.co
        reach=abs(p.x)-.64
        if reach<=0 or p.z<.62:continue
        weight=min(1,reach/.38)
        angle=math.radians(39 if p.x>0 else 8)*weight*weight*(3-2*weight)
        height=p.z-1.40
        p.x=math.copysign(.64+reach*math.cos(angle)-height*math.sin(angle),p.x)
        p.z=1.40+reach*math.sin(angle)+height*math.cos(angle)
        vertex.co=local@p

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
