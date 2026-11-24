"""Build the grip-space radio carbine and its orbiting pulse in Blender."""
from pathlib import Path
import math

import bpy
from mathutils import Matrix, Vector


out = Path(__file__).resolve().parents[3] / 'ContentSource/ClassicRifle'
out.mkdir(parents=True, exist_ok=True)
tau = math.tau


def material(name, color, metal, rough, emission=0.0):
    mat = bpy.data.materials.new(name)
    mat.diffuse_color = (*color, 1)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get('Principled BSDF')
    for pin, value in [('Base Color', mat.diffuse_color), ('Metallic', metal), ('Roughness', rough),
                       ('Emission Color', mat.diffuse_color), ('Emission Strength', emission)]:
        bsdf.inputs[pin].default_value = value
    return mat


def mesh(name, vertices, faces, mat, smooth=True):
    data = bpy.data.meshes.new(name)
    data.from_pydata(vertices, [], faces)
    data.update()
    obj = bpy.data.objects.new(name, data)
    bpy.context.collection.objects.link(obj)
    data.materials.append(mat)
    for face in data.polygons:
        face.use_smooth = smooth
    return obj


def profile(v, exponent):
    return math.copysign(abs(v)**exponent, v)


def shell(name, rings, mat, axis=(0,1,0), radial=(1,0,0), exponent=1.0, sides=48):
    across = Vector(radial)
    up = across.cross(Vector(axis))
    verts = []
    for center, w, h in rings:
        for i in range(sides):
            a = tau*i/sides
            verts.append(Vector(center) + across*(w*profile(math.cos(a),exponent))
                         + up*(h*profile(math.sin(a),exponent)))
    faces = [tuple(range(sides)), tuple(reversed(range(len(verts)-sides,len(verts))))]
    for j in range(len(rings)-1):
        for i in range(sides):
            a, b = j*sides+i, j*sides+(i+1)%sides
            faces.append((a,a+sides,b+sides,b))
    obj = mesh(name, verts, faces, mat)
    for face in obj.data.polygons[:2]:
        face.use_smooth = False
    return obj


def body(name, stations, mat, exponent=.78):
    return shell(name, [((0,y,z),w,h) for y,z,w,h in stations], mat, exponent=exponent)


def trim(name, points, radius, mat):
    curve = bpy.data.curves.new(name, 'CURVE')
    curve.dimensions = '3D'
    curve.bevel_depth = radius
    curve.bevel_resolution = 2
    curve.use_fill_caps = True
    spline = curve.splines.new('POLY')
    spline.points.add(len(points)-1)
    for v,p in zip(spline.points,points):
        v.co = (*p,1)
    obj = bpy.data.objects.new(name,curve)
    bpy.context.collection.objects.link(obj)
    curve.materials.append(mat)
    return obj


def save_model(source, asset, yaw):
    scene = bpy.context.scene
    scene.unit_settings.system = 'METRIC'
    scene.unit_settings.scale_length = .01
    bpy.context.preferences.filepaths.save_version = 0
    bpy.ops.wm.save_as_mainfile(filepath=str(out/source))
    parts = tuple(scene.objects)
    bpy.ops.object.select_all(action='DESELECT')
    deps = bpy.context.evaluated_depsgraph_get()
    transform = Matrix.Scale(.01,4) @ Matrix.Rotation(yaw,4,'Z')
    exported = []
    for obj in parts:
        data = bpy.data.meshes.new_from_object(obj.evaluated_get(deps), depsgraph=deps)
        data.transform(transform @ obj.matrix_world)
        if not data.uv_layers.active:
            uv = data.uv_layers.new(name='UVMap')
            for face in data.polygons:
                main = max(range(3),key=lambda i:abs(face.normal[i]))
                a,b = [i for i in range(3) if i!=main]
                for index in face.loop_indices:
                    p = data.vertices[data.loops[index].vertex_index].co
                    uv.data[index].uv = (p[a]*2,p[b]*2)
        data.uv_layers.active.name = 'UVMap'
        new = bpy.data.objects.new('Export '+obj.name,data)
        bpy.context.collection.objects.link(new)
        new.select_set(True)
        exported.append(new)
    bpy.context.view_layer.objects.active = exported[0]
    bpy.ops.object.join()
    combined = bpy.context.view_layer.objects.active
    combined.name = asset
    scene.unit_settings.scale_length = 1
    bpy.ops.export_scene.fbx(filepath=str(out/(asset+'.fbx')), use_selection=True, object_types={'MESH'},
                             axis_forward='-Y', axis_up='Z', add_leaf_bones=False, bake_anim=False,
                             mesh_smooth_type='FACE')
    print(asset,len(combined.data.vertices),'vertices')


bpy.ops.wm.read_factory_settings(use_empty=True)
jade = material('RadioJade', (.018,.305,.174), .38,.26)
ivory = material('RadioIvory', (.88,.71,.45), .19,.29)
brass = material('RadioBrass', (.74,.385,.094), .71,.25)
walnut = material('RadioWalnut', (.165,.053,.024), .15,.34)
ebony = material('RadioEbony', (.024,.033,.034), .23,.40)
amber = material('RadioAmber', (1.,.44,.022), .15,.20,1.4)

# Stretch the grain along the stock and fore-end in the editable material as well.
tree=walnut.node_tree
coords=tree.nodes.new('ShaderNodeTexCoord')
stretch=tree.nodes.new('ShaderNodeVectorMath')
stretch.operation='MULTIPLY'
stretch.inputs[1].default_value=(6.8,1.,5.3)
tree.links.new(coords.outputs['Generated'],stretch.inputs[0])
fibres=tree.nodes.new('ShaderNodeTexNoise')
fibres.inputs['Scale'].default_value=3.6
fibres.inputs['Detail'].default_value=2.1
fibres.inputs['Distortion'].default_value=.32
tree.links.new(stretch.outputs[0],fibres.inputs['Vector'])
dye=tree.nodes.new('ShaderNodeValToRGB')
for stop,t,color in zip(dye.color_ramp.elements,(.24,.77),[(.094,.031,.013,1),(.218,.078,.029,1)]):
    stop.position=t
    stop.color=color
tree.links.new(fibres.outputs['Fac'],dye.inputs[0])
tree.links.new(dye.outputs['Color'],tree.nodes.get('Principled BSDF').inputs['Base Color'])

# A downturned wooden butt, a raised jade radio cabinet and a stepped faceplate.
stock=[(-25.1,4.2,2.2,4.2),(-24.45,4.2,2.82,4.78),
     (-21.8,4.7,3.12,4.85),(-17,5.75,2.88,3.72),(-10.1,7.0,2.40,2.5),
     (-5.2,7.5,2.86,2.6),(1,7.5,3.04,2.2)]
body('01 | Walnut shoulder stock',stock,walnut,.59)
body('02 | Bakelite heel pad',[(-25.85,4.14,2.04,4.10),(-25.62,4.17,2.49,4.52),
     (-25.12,4.2,2.72,4.65),(-24.76,4.25,2.66,4.64)],ebony,.58)
body('03 | Heel coachline',[(-24.93,4.22,2.62,4.64),(-24.67,4.23,2.76,4.72),
     (-24.44,4.24,2.78,4.74)],brass,.58)
stations=[(-14.1,8.1,2.7,2.6),(-13.45,8.5,3.85,3.65),(-11.8,9.1,4.72,4.53),
          (-8.2,9.55,5.32,5.03),(-4,9.7,5.58,5.22),(4.8,9.7,5.65,5.22),
          (10.5,9.8,5.46,5.10),(14.5,9.9,5.16,4.77),(18.1,10,4.55,4.13),(19.7,10,3.85,3.7)]
body('04 | Jade broadcast cabinet',stations,jade,.60)


def on_skin(stations,y,angle,side,lift,exponent):
    for low,high in zip(stations,stations[1:]):
        if y<=high[0]:
            break
    f=(y-low[0])/(high[0]-low[0])
    z,w,h=[low[i]+(high[i]-low[i])*f for i in (1,2,3)]
    return (side*(w+lift)*profile(math.cos(angle),exponent),y,z+(h+lift)*profile(math.sin(angle),exponent))


def on_cabinet(y,angle,side,lift):
    return on_skin(stations,y,angle,side,lift,.60)


def cheek(name,y0,y1,mid,height,side,lift,mat):
    points=[]
    for i in range(45):
        t=i/44
        width=height*max(.018,math.sin(math.pi*t)**.56)
        points.extend(on_cabinet(y0+(y1-y0)*t,mid+width*(j/3),side,lift) for j in range(-3,4))
    faces=[(i*7+j,(i+1)*7+j,(i+1)*7+j+1,i*7+j+1) for i in range(44) for j in range(6)]
    return mesh(name,points,faces if side>0 else [f[::-1] for f in faces],mat)


for side in (-1,1):
    for j in range(2):
        trim('Stock | Swept brass inlay',[on_skin(stock,-23.6+k*(11.2-j*2.2)/28,
             -.13+j*.13+.29*k/28,side,.047,.59) for k in range(29)],.062,brass)
    cheek('05 | Brass faceplate bead',-12.7,17.3,-.04,.44,side,.10,brass)
    cheek('06 | Porcelain station scale',-12.43,17.04,-.04,.37,side,.17,ivory)
    for j in range(3):
        z_angle=.035+(j-1)*.115
        trim('07 | Station bars',[on_cabinet(6.5+k*(9.3-j*1.6)/22,z_angle,side,.25)
                                 for k in range(23)],.092,ebony)
    for step in range(3):
        trim('08 | Cabinet pinstripes',[on_cabinet(-11.4+k*(18.1-step*3.25)/26,
             -.62-step*.12,side,.10) for k in range(27)],.095,brass)
    # The amber disc and inclined meridian are repeated on the flying projectile.
    def dial(name,levels,mat):
        return shell(name,[((side*d,.85,10.05),r,r) for d,r in levels],mat,
                     axis=(side,0,0),radial=(0,1,0),sides=56)
    dial('09 | Sun dial escutcheon',[(5.6,2.9),(5.84,3.7),(6.04,3.80),(6.24,3.62)],brass)
    dial('10 | Black dial recess',[(6.23,3.37),(6.34,3.28)],ebony)
    dial('11 | Dial glass',[(6.35,2.23),(6.51,2.13),(6.64,1.82)],amber)
    for j in range(13):
        a=math.radians(16+148*j/12)
        trim('12 | Engraved dial graduations',[(side*6.36,.85+math.cos(a)*r,10.05+math.sin(a)*r)
             for r in (2.61,3.11 if j%3==0 else 2.91)],.068,ivory)
    angle=math.radians(26)
    ring=[]
    for j in range(65):
        a=tau*j/64
        h,v=2.72*math.cos(a),.75*math.sin(a)
        ring.append((side*6.71,.85+h*math.cos(angle)-v*math.sin(angle),10.05+h*math.sin(angle)+v*math.cos(angle)))
    trim('13 | Brass planetary orbit',ring,.115,brass)

# Three amber pilot windows sit in a stepped porcelain hood facing the holder.
n=Vector((0,-.82,.572)).normalized()
u=Vector((1,0,0))
v=n.cross(u)
o=Vector((0,-13.65,13.55))
def hood(name,offset,rows,mat):
    return shell(name,[(o+u*offset+n*d,w,h) for d,w,h in rows],mat,axis=n,radial=u,exponent=.62)
hood('14 | Radio tuning hood',0,[(-2.,3.37,1.67),(-.45,3.73,2.0),(.10,3.57,1.88)],jade)
hood('15 | Knurled hood edge',0,[(.08,3.60,1.90),(.29,3.51,1.83)],brass)
hood('16 | Inset tuning panel',0,[(.30,3.26,1.59),(.40,3.15,1.49)],ebony)
for col in range(-2,3):
    h=1.06-abs(col)*.24
    hood('17 | Amber fan pilot',col*.87,[(.408,.20,h),(.45,.18,h*.95)],amber)
for j in range(4):
    y=-5.0+j*3.35
    pts=[on_cabinet(y,math.radians(a),1,.13) for a in range(45,137,3)]
    trim('18 | Brass radio grille ribs',pts,.15,brass)

# Underslung fluted wood follows the support hand of the saved two-hand animation.
body('19 | Front walnut saddle',[(14.2,4.32,1.95,1.58),(16.1,3.35,3.25,2.05),
     (21.8,2.47,3.87,2.31),(30.7,2.45,3.98,2.38),(34.0,3.03,3.58,2.08),(36.5,4.32,2.73,1.61)],walnut,.68)
for j in range(5):
    y=21.5+j*2.5
    body('20 | Palm-swell flutes',[(y-.20,2.48,3.83,2.26),(y,2.48,4.02,2.40),(y+.25,2.48,3.88,2.30)],ebony,.68)
def grip(name,profile_data,mat):
    return shell(name,[((0,y,z),w,d) for z,y,w,d in profile_data],mat,axis=(0,0,1),exponent=.79)
grip('21 | Sculpted walnut grip',[(-10.55,-3.6,2.42,3.35),(-9.7,-3.47,2.78,3.58),
     (-6.5,-2.43,2.75,3.3),(-1.0,-.55,2.44,2.92),(4.83,1.57,2.84,3.25),(5.5,1.60,3.0,3.55)],walnut)
grip('22 | Brass grip foot',[(-10.88,-3.6,2.21,3.06),(-10.75,-3.6,2.63,3.40),(-10.36,-3.52,2.72,3.49)],brass)
trim('23 | Brass finger bow',[(0,1.6,-4.24),(0,3.5,-4.96),(0,6.7,-4.85),(0,9.45,-3.75),
     (0,10.6,-1.57),(0,10.7,3.70)],.35,brass)
trim('24 | Ebony lever',[(0,5.6,4.2),(0,5.42,2.87),(0,4.87,.74),(0,5.39,-.24)],.39,ebony)

# Bell-shaped nose with a narrow waist breaks the straight silhouette of the railguns.
def emitter(name,profile_data,mat,exponent=1.0):
    return body(name,[(y,10.,w,h) for y,w,h in profile_data],mat,exponent)
emitter('25 | Porcelain collar',[(17.2,4.7,4.4),(18.2,4.97,4.64),(19.2,4.86,4.53),(20,4.26,4.08)],ivory)
emitter('26 | Warm glass resonator',[(19.4,3.27,3.27),(20.5,3.82,3.82),(22.1,3.92,3.92),
        (26.5,3.23,3.23),(30.5,2.88,2.88),(34.2,3.2,3.2),(35.1,3.52,3.52)],amber)
for y,r in [(20.2,4.15),(23,4.02),(32.8,3.40),(35.2,3.74)]:
    emitter('27 | Brass resonator rings',[(y-.35,r-.14,r-.14),(y-.21,r,r),(y+.21,r,r),(y+.35,r-.14,r-.14)],brass)
for a in (math.radians(28),math.radians(152),math.radians(270)):
    trim('28 | Curved resonator ribs',[(r*math.cos(a),y,10+r*math.sin(a))
         for y,r in [(19.7,4.36),(20.6,4.63),(24.2,4.65),(29.7,4.13),(34.0,3.98),(35.5,3.86)]],.22,ebony)
emitter('29 | Horn throat',[(34.5,3.21,3.25),(35.5,3.78,3.70),(36.6,4.03,3.83),(38.8,3.56,3.36)],brass)
emitter('30 | Jade flared horn',[(36.4,3.89,3.79),(37.5,3.97,3.82),(39.1,3.56,3.38),
        (41.2,3.37,3.23),(43.2,3.48,3.39),(45.2,4.08,3.87),(47.2,5.36,4.91),(48.8,6.26,5.66)],jade)
emitter('31 | Cream horn band',[(47.18,5.35,4.92),(47.58,5.65,5.16),(48.1,5.97,5.43)],ivory)
emitter('32 | Rolled brass bell lip',[(48.3,6.01,5.49),(48.75,6.43,5.82),(49.50,6.55,5.92),
        (50.05,6.36,5.79),(50.38,5.86,5.34)],brass)
emitter('33 | Dark bell interior',[(50.36,5.76,5.26),(50.45,5.61,5.09)],ebony)
emitter('34 | Porcelain bulb seat',[(50.45,3.65,3.39),(50.62,3.61,3.36),(50.82,3.20,2.96)],ivory)
emitter('35 | Luminous amber lens',[(50.83,2.97,2.77),(51.16,2.88,2.63),(51.66,2.04,1.90),(52,.40,.38)],amber)
save_model('ClassicRifle.blend','SM_Rifle',math.pi)

# One render component holds the hot sphere, its open Saturn ring and a soft wake.
# The gameplay sphere remains centred on (0,0,0) and supplies all collision.
bpy.ops.wm.read_factory_settings(use_empty=True)
core = material('PulseCore',(1.,.59,.027),.03,.25,2.)
orbit = material('PulseOrbit',(1.,.35,.012),.28,.28,1.4)
glow = material('PulseGlow',(1.,.31,.014),0,.55,.19)


def facing_weight(tree):
    geo=tree.nodes.new('ShaderNodeNewGeometry')
    dot=tree.nodes.new('ShaderNodeVectorMath')
    dot.operation='DOT_PRODUCT'
    for i,key in enumerate(('Normal','Incoming')):
        tree.links.new(geo.outputs[key],dot.inputs[i])
    absolute=tree.nodes.new('ShaderNodeMath')
    absolute.operation='ABSOLUTE'
    tree.links.new(dot.outputs['Value'],absolute.inputs[0])
    return absolute.outputs[0]


# Preview the hot centre and transparent falloffs in the source scene.
tree=core.node_tree
heat=tree.nodes.new('ShaderNodeValToRGB')
heat.color_ramp.elements[0].color=(.29,.038,.002,1)
heat.color_ramp.elements[1].color=(1.,.88,.46,1)
heat.color_ramp.elements.new(.65).color=(.74,.38,.012,1)
tree.links.new(facing_weight(tree),heat.inputs[0])
tree.links.new(heat.outputs[0],tree.nodes.get('Principled BSDF').inputs['Emission Color'])
tree.nodes.get('Principled BSDF').inputs['Emission Strength'].default_value=3.3

tree=glow.node_tree
def optic_math(operation,a,b):
    node=tree.nodes.new('ShaderNodeMath')
    node.operation=operation
    for i,value in enumerate((a,b)):
        if isinstance(value,(int,float)):
            node.inputs[i].default_value=value
        else:
            tree.links.new(value,node.inputs[i])
    return node.outputs[0]


uv=tree.nodes.new('ShaderNodeSeparateXYZ')
tree.links.new(tree.nodes.new('ShaderNodeTexCoord').outputs['UV'],uv.inputs[0])
x,y=uv.outputs['X'],uv.outputs['Y']
edge=optic_math('ABSOLUTE',optic_math('SUBTRACT',optic_math('MULTIPLY',y,2),1),0)
tail=optic_math('MULTIPLY',optic_math('POWER',optic_math('SUBTRACT',1,edge),2.5),
                optic_math('MAXIMUM',0,optic_math('SUBTRACT',1,x)))
selector=optic_math('GREATER_THAN',x,1.5)
halo_weight=optic_math('MULTIPLY',optic_math('POWER',facing_weight(tree),2.35),.34)
weight=optic_math('ADD',optic_math('MULTIPLY',tail,optic_math('SUBTRACT',1,selector)),
                  optic_math('MULTIPLY',halo_weight,selector))
light=tree.nodes.new('ShaderNodeEmission')
light.inputs[0].default_value=(1.,.37,.02,1)
tree.links.new(weight,light.inputs['Strength'])
add=tree.nodes.new('ShaderNodeAddShader')
tree.links.new(tree.nodes.new('ShaderNodeBsdfTransparent').outputs[0],add.inputs[0])
tree.links.new(light.outputs[0],add.inputs[1])
tree.links.new(add.outputs[0],tree.nodes.get('Material Output').inputs['Surface'])


def globe(name,radius,mat,segments,rings):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=segments,ring_count=rings,radius=radius)
    obj=bpy.context.object
    obj.name=name
    obj.data.materials.append(mat)
    for f in obj.data.polygons:
        f.use_smooth=True
    return obj
globe('01 | Incandescent amber heart',46.,core,36,18)
equator=Vector((1,.14,.29)).normalized()
meridian=Vector((-.31,.75,.59))
meridian=(meridian-equator*meridian.dot(equator)).normalized()
def arc(name,radius,start,end,width):
    steps=math.ceil((end-start)/4.8)
    points=[radius*(equator*math.cos(a)+meridian*math.sin(a))
            for a in [math.radians(start+(end-start)*i/steps) for i in range(steps+1)]]
    return trim(name,points,width,orbit)
arc('02 | Main solar orbit',61.,-168,168,3.05)
arc('03 | Fine echo arc',67.3,16,140,1.35)
halo=globe('04 | Optical aureole',65.,glow,24,12)
for loop in halo.data.uv_layers.active.data:
    loop.uv=(2.,.5)
# Four feathered strips keep the afterimage readable across viewing angles.
for j in range(4):
    radial=Vector((math.cos(j*math.pi/4),0,math.sin(j*math.pi/4)))
    stations=[(0.,16.,25.),(.15,-2.,28.),(.34,-32.,23.),(.62,-76.,14.),(.85,-117.,6.),(1.,-151.,.2)]
    verts=[Vector((0,y,0))+radial*w*s for _,y,w in stations for s in (-1,1)]
    strip=mesh('05 | Flight afterimage',verts,[(2*i,2*i+1,2*i+3,2*i+2) for i in range(len(stations)-1)],glow,False)
    uv=strip.data.uv_layers.new(name='UVMap')
    for loop in strip.data.loops:
        idx=loop.vertex_index
        uv.data[loop.index].uv=(stations[idx//2][0],float(idx%2))
save_model('SunPulse.blend','SM_SunPulse',math.pi*1.5)
