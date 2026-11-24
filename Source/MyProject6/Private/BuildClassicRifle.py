"""Build the Solstice carbine and laser bolt. Use --rifle-only or --projectile-only for a targeted export."""
from pathlib import Path
import math
import sys

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


def rounded_profile(points, fraction=.18):
    rounded = []
    for i, corner in enumerate(points):
        previous, corner, following = Vector(points[i-1]), Vector(corner), Vector(points[(i+1) % len(points)])
        a, b = corner.lerp(previous, fraction), corner.lerp(following, fraction)
        for k in range(5):
            t = k / 4
            rounded.append((1-t)**2*a + 2*(1-t)*t*corner + t*t*b)
    return rounded


def casting(name, outer, x, thickness, bevel, mat, holes=()):
    """Extrude a filleted silhouette in the side plane, including through-openings."""
    curve = bpy.data.curves.new(name, 'CURVE')
    curve.dimensions = '2D'
    curve.fill_mode = 'BOTH'
    curve.extrude = thickness * .5 - bevel
    curve.bevel_depth = bevel
    curve.bevel_resolution = 3
    for i, outline in enumerate((outer, *holes)):
        path = rounded_profile(outline)
        area = sum(a.x*b.y - a.y*b.x for a, b in zip(path, path[1:]+path[:1]))
        if (area > 0) == (i > 0):
            path.reverse()
        line = curve.splines.new('POLY')
        line.points.add(len(path)-1)
        for point, yz in zip(line.points, path):
            point.co = (yz.x, yz.y, 0, 1)
        line.use_cyclic_u = True
    obj = bpy.data.objects.new(name, curve)
    bpy.context.collection.objects.link(obj)
    obj.matrix_world = Matrix(((0,0,1,x),(1,0,0,0),(0,1,0,0),(0,0,0,1)))
    curve.materials.append(mat)
    return obj


def enamel_band(name, stations, y0, y1, exponent, side, lift, edges, mat):
    points, count, across = [], 37, 5
    for i in range(count):
        t = i/(count-1)
        y = y0 + (y1-y0)*t
        low, high = next((a,b) for a,b in zip(stations,stations[1:]) if a[0] <= y <= b[0]+1.e-5)
        f = (y-low[0])/(high[0]-low[0])
        z,w,h = (low[j]+(high[j]-low[j])*f for j in (1,2,3))
        bottom, top = edges(t)
        for j in range(across):
            a = bottom + (top-bottom)*j/(across-1)
            points.append((side*(w+lift)*profile(math.cos(a),exponent), y, z+(h+lift)*profile(math.sin(a),exponent)))
    faces = [(i*across+j,(i+1)*across+j,(i+1)*across+j+1,i*across+j+1)
             for i in range(count-1) for j in range(across-1)]
    return mesh(name, points, faces if side>0 else [q[::-1] for q in faces], mat)


def makers_mark(keel, side, lettering, metal):
    # Draw the mirrored side plates separately so the mark reads from either side.
    def point(u,v,lift=.075):
        y=.20+side*(u-3.9)*1.26
        z=7.83+v*1.16
        a,b=next((a,b) for a,b in zip(keel,keel[1:]) if a[0]<=y<=b[0])
        t=(y-a[0])/(b[0]-a[0])
        centre,width,height=[a[k]+t*(b[k]-a[k]) for k in (1,2,3)]
        x=width*(1.-(abs(z-centre)/height)**(2/.63))**(.63/2)
        return (side*(x+lift),y,z)
    star=[(0.,.87),(.16,.17),(.84,0.),(.17,-.15),(0.,-.78),(-.16,-.16),(-.76,0.),(-.15,.16)]
    vertices=[point(.70,.03,.15)]+[point(.70+u,.03+v,.13) for u,v in star]
    mesh('Signature | Four-point sun',vertices,[(0,(i+1)%8+1,i+1) for i in range(8)],metal,False)
    for offset,strokes in [
        (1.97,[(1.17,.57),(.98,.76),(.32,.76),(.08,.53),(.10,.27),(.31,.13),(.91,.07),
               (1.09,-.09),(1.04,-.36),(.82,-.54),(.20,-.54),(0.,-.36)]),
        (3.67,[(.37,.76),(.98,.76),(1.19,.53),(1.10,-.27),(.86,-.55),(.25,-.55),
               (.04,-.31),(.13,.49),(.37,.76)]),
        (5.41,[(.23,.78),(.07,-.52),(1.18,-.52)])]:
        trim('Signature | Inlaid SOL lettering',[point(offset+y,z,.10) for y,z in strokes],.096,lettering)


def aperture(name, profile_data, mat, exponent=.79, sides=48):
    # The section turns back into the throat. Its interior is visible from the front.
    rings = [((0,y,10),w,h) for y,w,h in profile_data]
    points = [(w*profile(math.cos(a*tau/sides),exponent),center[1],10+h*profile(math.sin(a*tau/sides),exponent))
              for center,w,h in rings for a in range(sides)]
    faces = []
    for row in range(len(rings)):
        end = (row+1) % len(rings)
        for i in range(sides):
            j = (i+1)%sides
            faces.append((row*sides+i,end*sides+i,end*sides+j,row*sides+j))
    return mesh(name,points,faces,mat)


def sphere(name, center, radii, mat):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=48,ring_count=24,location=center)
    obj = bpy.context.object
    obj.name = name
    obj.scale = radii
    obj.data.materials.append(mat)
    for face in obj.data.polygons:
        face.use_smooth = True
    return obj


def scope_face(center,normal,mat):
    right = Vector((1,0,0))
    up = normal.cross(right).normalized()
    vertices, uv = [center+normal*.12], [(.5,.5)]
    sides = 56
    for radius in (.52,.84,1.):
        for i in range(sides):
            a = i*tau/sides
            x = radius*profile(math.cos(a),.55)
            y = radius*profile(math.sin(a),.55)
            vertices.append(center + right*(2.98*x) + up*(1.40*y) + normal*(.12*(1-radius*radius)))
            uv.append((.5+.5*x,.5+.5*y))
    faces = [(0,1+i,1+(i+1)%sides) for i in range(sides)]
    for row in range(2):
        start = 1+row*sides
        for i in range(sides):
            j = (i+1)%sides
            faces.append((start+i,start+sides+i,start+sides+j,start+j))
    obj = mesh('Dash | Curved phosphor glass',vertices,faces,mat)
    layer = obj.data.uv_layers.new(name='UVMap')
    for loop in obj.data.loops:
        layer.data[loop.index].uv = uv[loop.vertex_index]


def source_optics(corona, screen):
    tree = corona.node_tree
    ramp = tree.nodes.new('ShaderNodeValToRGB')
    ramp.color_ramp.elements[0].color = (1.,.64,.16,1)
    ramp.color_ramp.elements[1].color = (.40,.069,.004,1)
    ramp.color_ramp.elements.new(.53).color = (1.,.33,.016,1)
    tree.links.new(tree.nodes.new('ShaderNodeLayerWeight').outputs['Facing'],ramp.inputs[0])
    bsdf = tree.nodes.get('Principled BSDF')
    for pin in ('Base Color','Emission Color'):
        tree.links.new(ramp.outputs[0],bsdf.inputs[pin])
    bsdf.inputs['Emission Strength'].default_value = .85

    # A packed frame makes the editable Blender scene show the same phosphor pattern as Unreal.
    tree = screen.node_tree
    width,height = 256,128
    raster = bpy.data.images.new('Phosphor trace - source preview',width,height,alpha=False,float_buffer=True)
    raster.colorspace_settings.name = 'Non-Color'
    values = []
    for row in range(height):
        v = (row+.5)/height
        for col in range(width):
            u = (col+.5)/width
            wave = .48+.23*math.sin(tau*(u*1.30+.07))+.065*math.sin(tau*(u*2.60-.08))
            distance = abs(v-wave)
            beam = math.exp(-distance*distance/.00050) + .18*math.exp(-distance*distance/.009)
            grid = .037 if min((u*8)%1,(v*4)%1)<.045 else 0.
            values.extend((.009+grid*.25+beam*.33,.048+grid+beam*1.15,.042+grid*.8+beam*.69,1))
    raster.pixels.foreach_set(values)
    raster.pack()
    tex = tree.nodes.new('ShaderNodeTexImage')
    tex.image = raster
    bsdf = tree.nodes.get('Principled BSDF')
    tree.links.new(tex.outputs['Color'],bsdf.inputs['Base Color'])
    tree.links.new(tex.outputs['Color'],bsdf.inputs['Emission Color'])
    bsdf.inputs['Emission Strength'].default_value = 1.0


def build_rifle():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    orange = material('SolMandarin',(.76,.164,.032),.33,.24)
    ink = material('SolPetrol',(.022,.073,.09),.53,.30)
    nickel = material('SolNickel',(.57,.62,.62),.76,.24)
    cream = material('RadioIvory',(.86,.72,.50),.17,.30)
    graphite = material('RadioEbony',(.023,.033,.033),.21,.41)
    corona = material('SolCorona',(1.,.48,.025),.10,.22,.9)
    scope = material('SolPhosphor',(.026,.18,.14),.10,.25,.9)
    source_optics(corona,scope)

    # The open shoulder frame and the two swept rear fins give a readable silhouette in profile.
    shoulder = [(-25.1,7.6),(-21.4,8.6),(-12,9.3),(-5.0,8.9),(-1.4,6.4),(-2.8,3.9),
                (-6.8,3.55),(-13.5,.10),(-21.9,-3.95),(-24.85,-4.05),(-25.75,-2.55)]
    opening = [(-22.9,5.43),(-17.4,6.16),(-9.8,6.22),(-11.2,5.02),(-18.1,1.55),(-22.55,.02)]
    casting('Tail | Hollow midnight casting',shoulder,0,4.25,.36,ink,(opening,))
    heel = [(-26.15,7.0),(-25.75,7.57),(-24.65,7.77),(-24.32,6.32),(-24.5,-3.32),
            (-25.10,-4.43),(-26.22,-4.16),(-26.65,-2.48)]
    casting('Tail | Rubber heel',heel,0,4.85,.44,graphite)
    for side in (-1,1):
        casting('Tail | Mandarin outer spar',[(-24.5,7.7),(-16.4,8.45),(-8.6,8.62),(-5.6,7.70),
                (-8.,7.04),(-17.6,7.0),(-23.9,6.27)],side*2.13,.30,.11,orange)
        trim('Tail | Nickel lower edge',[(side*2.13,y,z) for y,z in
             [(-23.3,-2.45),(-21.8,-2.30),(-17.8,-.57),(-13.3,1.66),(-7.7,4.30)]],.105,nickel)

    keel = [(-13.7,7.95,2.34,2.24),(-12.5,8.10,3.26,2.75),(-9.1,8.15,4.04,3.0),
            (-3.2,8.12,4.62,3.66),(4.8,8.0,4.80,3.76),(10.4,8.0,4.60,3.22),
            (14.4,8.13,4.02,2.78),(16.1,8.72,3.05,2.12)]
    body('Cabin | Deep petrol keel',keel,ink,.63)
    mantle = [(-13.3,8.95,2.38,1.67),(-12.7,9.73,3.09,2.18),(-10.4,10.37,3.91,2.63),
              (-6.4,10.70,4.58,3.06),(-1.6,10.92,4.88,3.02),(4.4,10.93,4.98,2.80),
              (9.9,10.70,4.87,2.36),(13.4,10.43,4.34,1.97),(15.3,10.36,3.39,1.70)]
    body('Cabin | Streamlined enamel bonnet',mantle,orange,.75)
    for side in (-1,1):
        def sweep(t):
            centre=.02+.42*math.sin(math.pi*(t-.18))-.22*t
            half=.025+.22*math.sin(t*math.pi)**.75
            return centre-half,centre+half
        enamel_band('Cabin | Nickel flash outline',mantle,-12.3,14.35,.75,side,.085,sweep,nickel)
        def ivory_sweep(t):
            lo,hi=sweep(t)
            return lo+.044, max(lo+.046,hi-.043)
        enamel_band('Cabin | Swept porcelain flash',mantle,-11.9,13.86,.75,side,.145,ivory_sweep,cream)
        for j in range(3):
            enamel_band('Cabin | Low speed flute',keel,-6.3+j*1.22,8.5-j*.27,.63,side,.095,
                        lambda t,j=j:(-.26-.19*j+.12*t,-.218-.19*j+.12*t),nickel)
        makers_mark(keel,side,cream,nickel)
        fin=[(-10.5,12.2),(-10.45,16.7),(-9.73,16.98),(-6.25,15.53),(-1.4,14.1),
             (5.10,12.86),(5.7,12.34),(.0,12.14),(-6.6,11.50)]
        casting('Crest | Swept enamel tailfin',fin,side*2.93,.68,.19,orange)
        casting('Crest | Inlaid petrol teardrop',[(-9.98,15.98),(-9.58,16.32),(-8.92,16.04),
                (-6.1,14.91),(-3.2,14.03),(-6.36,14.64),(-9.55,15.62)],side*3.28,.14,.045,ink)
        trim('Crest | Rolled silver edge',[(side*2.93,y,z) for y,z in
             [(-10.24,16.55),(-9.82,16.79),(-8.9,16.50),(-6.2,15.37),(-1.4,13.98),(3.0,13.12)]],.15,nickel)

    # Low dashboard faces back and up into the player's sightline.
    normal = Vector((0,-.64,.77)).normalized()
    right = Vector((1,0,0))
    origin = Vector((0,-11.05,13.12))
    def bezel(name, sections, mat):
        return shell(name,[(origin+normal*d,w,h) for d,w,h in sections],mat,axis=normal,radial=right,exponent=.56)
    bezel('Dash | Petrol cowl',[(-1.65,2.85,1.55),(-.8,3.56,1.92),(.14,3.64,1.91),(.38,3.43,1.75)],ink)
    bezel('Dash | Satin bezel',[(.29,3.5,1.82),(.48,3.56,1.77),(.61,3.34,1.63)],nickel)
    bezel('Dash | Insulating seal',[(.60,3.32,1.62),(.68,3.13,1.49)],graphite)
    scope_face(origin+normal*.69,normal,scope)
    # A raised comet emblem is visible from above, even in the alternate camera.
    centre=Vector((0,4.2,13.72))
    star=[(0,-1.80),(.25,-.26),(1.04,0),(.23,.20),(0,1.75),(-.23,.20),(-1.04,0),(-.25,-.26)]
    mesh('Cabin | Inlaid compass spark',[centre]+[centre+Vector((x,y,.07)) for x,y in star],
         [(0,i+1,(i+1)%len(star)+1) for i in range(len(star))],cream,False)

    # Two hands bear on the same grip-space stations as the saved animation.
    grip = [(-10.62,-3.74,2.22,3.10),(-10.1,-3.6,2.59,3.35),(-8.5,-3.25,2.63,3.35),
            (-5.1,-2.28,2.56,3.08),(-.8,-.68,2.34,2.78),(3.6,1.05,2.71,3.16),(5.57,1.54,3.0,3.39)]
    def handle(name,stations,mat):
        return shell(name,[((0,y,z),w,d) for z,y,w,d in stations],mat,axis=(0,0,1),exponent=.74)
    handle('Grip | Fluted Bakelite',grip,graphite)
    handle('Grip | Nickel pommel',[(-10.97,-3.72,2.15,3.01),(-10.78,-3.7,2.51,3.30),
                                   (-10.38,-3.67,2.62,3.36)],nickel)
    for side in (-1,1):
        panel=[(-4.40,-9.05),(-2.46,-9.22),(.53,-2.36),(1.52,.21),(.50,1.60),(-1.60,-1.05)]
        casting('Grip | Mandarin inset',panel,side*2.48,.33,.11,orange)
        for j in range(3):
            trim('Grip | Raised thumb flute',[(side*2.70,-3.55+j*.55,-7.67),(side*2.70,-.77+j*.44,-1.39)],.070,ink)
    guard=[(.8,-3.7),(2.,-4.7),(5.9,-5.14),(9.0,-4.03),(10.47,-1.58),(10.7,4.53),
           (9.51,4.87),(9.51,-1.22),(8.37,-3.11),(5.57,-4.0),(2.68,-3.90),(1.04,-2.93)]
    casting('Grip | Cast finger bow',guard,0,1.05,.24,nickel)
    trim('Grip | Curved dark paddle',[(0,5.72,4.53),(0,5.92,2.78),(0,5.06,.82),(0,5.57,-.1)],.41,graphite)
    cradle=[(6.5,4.93,2.05,1.39),(10.3,4.63,3.26,1.71),(15.1,3.45,3.66,1.80),
            (21.7,2.53,3.86,1.99),(28.5,2.63,3.91,2.03),(32.8,3.20,3.70,1.97),
            (36.65,4.35,3.03,1.71),(39.0,5.83,2.36,1.27)]
    body('Palm | Streamlined supporting shoe',cradle,ink,.64)
    body('Palm | Checkered Bakelite pad',[(17.3,1.94,2.65,1.02),(19.5,1.21,3.51,1.13),
         (24.6,1.04,3.68,1.32),(30.1,1.32,3.61,1.26),(33.2,2.02,3.15,1.19),(34.8,3.07,2.58,1.13)],graphite,.63)
    for side in (-1,1):
        enamel_band('Palm | Mandarin coachline',cradle,11.05,37.47,.64,side,.08,
                    lambda t:(.08,.09+.33*math.sin(math.pi*t)**.64),orange)
    for j in range(5):
        y=20.5+j*2.35
        trim('Palm | Scalloped grip ribs',[(3.45,y,.82),(3.20,y+.5,.28),(2.66,y+.72,.04),
             (0,y+.80,-.12),(-2.66,y+.72,.04),(-3.20,y+.5,.28),(-3.45,y,.82)],.12,ink)

    # The cell is visible through open air between the flowing spars, from both sides and above.
    for side in (-1,1):
        spars=[(10.4,11.4,3.5,.96,1.61),(13.1,12.1,4.05,1.03,1.43),
               (17.4,13.96,4.48,.72,.93),(21.2,14.59,4.7,.52,.57),
               (24.65,14.65,4.66,.48,.48),(28.1,14.16,4.41,.57,.49),
               (31.18,12.65,4.03,.75,.66),(34.3,10.36,3.18,.92,.95)]
        shell('Cell | Mandarin flying buttress',[((side*x,y,z),w,h) for y,z,x,w,h in spars],orange,exponent=.67,sides=32)
        trim('Cell | Highlight on the spar',[(side*(x+w*.78),y,z+h*.45) for y,z,x,w,h in spars[1:-1]],.12,nickel)
        lower=[(12.7,6.98,3.26,1.25,1.13),(15.1,6.01,3.75,.95,.89),(20.0,5.39,4.24,.62,.55),
               (25.8,5.37,4.27,.59,.54),(30.0,5.99,4.03,.72,.73),(34.55,8.0,3.51,.89,1.13)]
        shell('Cell | Porcelain lower spar',[((side*x,y,z),w,h) for y,z,x,w,h in lower],cream,exponent=.7,sides=32)
    body('Cell | Aft isolator',[(13.2,10.65,2.93,2.8),(14.8,10.8,3.30,3.08),(16.3,10.91,2.76,2.65),
                                (18.0,11.11,2.04,2.03)],ink)
    body('Cell | Forward isolator',[(27.8,11.18,1.78,1.91),(29.8,10.7,2.32,2.54),
                                    (31.2,10.2,3.02,3.18),(33.25,10.,3.12,3.14)],ink)
    orb=Vector((0,22.85,11.15))
    sphere('Cell | Captive sun',orb,(4.10,5.10,4.04),corona)
    u=Vector((.96,.24,-.12)).normalized()
    v=Vector((.08,.52,.87))
    v=(v-u*v.dot(u)).normalized()
    for label,r,a,b,width,mat in [('Nickel orbital cradle',5.05,-162,161,.19,nickel),
                                 ('Porcelain echo arc',5.32,40,160,.13,cream)]:
        angles=[math.radians(a+(b-a)*i/56) for i in range(57)]
        trim('Cell | '+label,[orb+r*(u*math.cos(t)+v*math.sin(t)) for t in angles],width,mat)

    # Stepped heat vanes lead to a compact recessed optical emitter.
    body('Nose | Petrol neck',[(30.4,10.05,2.72,2.79),(32.,10,3.29,3.30),(34.5,10,2.74,2.69),
                                (41.9,10,2.34,2.41),(46.2,10,2.77,2.81),(49.2,10,2.72,2.70)],ink,.77)
    for i,(y,w,h) in enumerate([(33.60,4.64,4.32),(35.93,4.53,4.21),(38.22,4.31,4.04),(40.47,4.0,3.80)]):
        aperture('Nose | Streamlined nickel vane %d'%(i+1),
                 [(y-.51,w-.48,h-.44),(y-.31,w-.13,h-.11),(y-.12,w,h),(y+.17,w,h),
                  (y+.40,w-.29,h-.22),(y+.48,2.06,2.14),(y-.49,2.06,2.14)],nickel,exponent=.83)
    for side in (-1,1):
        runner=[(32.2,8.46,3.30,.57,.86),(34.7,7.27,3.37,.55,.66),(39.3,7.11,3.24,.49,.53),
                (43.1,7.8,2.93,.49,.57),(46.4,8.99,2.52,.61,.70)]
        shell('Nose | Enamel side runner',[((side*x,y,z),w,h) for y,z,x,w,h in runner],orange,sides=28)
    aperture('Nose | Midnight ceramic surround',[(42.05,2.64,2.83),(43.34,3.36,3.38),(44.5,3.64,3.63),
             (47.85,3.69,3.65),(49.23,3.42,3.38),(49.41,2.66,2.60),(42.10,2.23,2.18)],ink)
    aperture('Nose | Ivory noseband',[(44.55,3.63,3.63),(44.75,3.79,3.77),(45.36,3.79,3.77),
             (45.57,3.66,3.65),(45.56,3.44,3.40),(44.55,3.44,3.40)],cream)
    aperture('Nose | Rolled nickel lip',[(48.7,3.55,3.51),(49.11,3.78,3.73),(50.83,3.76,3.71),
             (51.51,3.49,3.45),(51.68,2.95,2.91),(51.12,2.75,2.73),(48.76,2.80,2.78)],nickel)
    aperture('Nose | Recessed optical socket',[(48.7,2.84,2.84),(51.22,2.80,2.79),(51.38,2.56,2.55),
             (49.35,2.17,2.16),(49.15,1.71,1.70),(48.51,1.65,1.65)],graphite)
    sphere('Nose | Honey pilot lens',(0,49.44,10),(1.75,2.48,1.75),corona)
    save_model('ClassicRifle.blend','SM_Rifle',math.pi)


def build_projectile():
    """One closed hot filament and its finite aureole, with the impact point near the nose."""
    bpy.ops.wm.read_factory_settings(use_empty=True)
    core = material('PulseCore',(1.,.66,.10),0.,.35,2.6)
    halo = material('PulseGlow',(1.,.23,.012),0.,.5,.65)

    def bolt_surface(name, profile_data, mat, sides):
        vertices, faces, stations = [], [], []
        for y,radius in profile_data:
            count = sides if radius else 1
            row = []
            for i in range(count):
                angle = tau*i/sides
                row.append(len(vertices))
                vertices.append((radius*math.cos(angle),y,radius*math.sin(angle)))
            stations.append(row)
        for back,front in zip(stations,stations[1:]):
            for i in range(sides):
                j=(i+1)%sides
                if len(back)==1:
                    faces.append((back[0],front[i],front[j]))
                elif len(front)==1:
                    faces.append((back[i],front[0],back[j]))
                else:
                    faces.append((back[i],front[i],front[j],back[j]))
        obj=mesh(name,vertices,faces,mat)
        layer=obj.data.uv_layers.new(name='UVMap')
        tail,nose=profile_data[0][0],profile_data[-1][0]
        for loop in obj.data.loops:
            x,y,z=vertices[loop.vertex_index]
            layer.data[loop.index].uv=((y-tail)/(nose-tail),math.atan2(z,x)/tau+.5)

    # The existing .24-scale gameplay instance displays a 63 cm core and a 75 cm halo.
    bolt_surface('01 | Ivory plasma filament',
                 [(-225.,0.),(-222.,2.4),(-214.,4.4),(-188.,5.7),(-130.,6.1),
                  (12.,6.1),(27.,5.7),(34.,4.6),(38.,2.4),(39.,0.)],core,20)
    bolt_surface('02 | Amber corona',
                 [(-263.,0.),(-251.,5.0),(-226.,10.8),(-192.,17.8),(-132.,21.2),
                  (3.,21.2),(24.,19.3),(39.,13.6),(47.,6.4),(49.,0.)],halo,28)

    # Match the game material's feathered emission in the editable source.
    for surface,transparent in ((core,False),(halo,True)):
        tree=surface.node_tree
        geo=tree.nodes.new('ShaderNodeNewGeometry')
        dot=tree.nodes.new('ShaderNodeVectorMath')
        dot.operation='DOT_PRODUCT'
        tree.links.new(geo.outputs['Normal'],dot.inputs[0])
        tree.links.new(geo.outputs['Incoming'],dot.inputs[1])
        absolute=tree.nodes.new('ShaderNodeMath')
        absolute.operation='ABSOLUTE'
        tree.links.new(dot.outputs['Value'],absolute.inputs[0])
        emissive=tree.nodes.new('ShaderNodeEmission')
        if transparent:
            power=tree.nodes.new('ShaderNodeMath')
            power.operation='POWER'
            power.inputs[1].default_value=4.5
            tree.links.new(absolute.outputs[0],power.inputs[0])
            tree.links.new(power.outputs[0],emissive.inputs['Strength'])
            emissive.inputs['Color'].default_value=(2.4,.55,.018,1)
            add=tree.nodes.new('ShaderNodeAddShader')
            tree.links.new(tree.nodes.new('ShaderNodeBsdfTransparent').outputs[0],add.inputs[0])
            tree.links.new(emissive.outputs[0],add.inputs[1])
            final=add
        else:
            ramp=tree.nodes.new('ShaderNodeValToRGB')
            ramp.color_ramp.elements[0].color=(1.,.32,.015,1)
            ramp.color_ramp.elements[1].color=(1.,.90,.67,1)
            tree.links.new(absolute.outputs[0],ramp.inputs[0])
            tree.links.new(ramp.outputs[0],emissive.inputs['Color'])
            emissive.inputs['Strength'].default_value=4.0
            final=emissive
        tree.links.new(final.outputs[0],tree.nodes.get('Material Output').inputs['Surface'])
    save_model('SunPulse.blend','SM_SunPulse',math.pi*1.5)


if __name__=='__main__':
    if '--projectile-only' not in sys.argv:
        build_rifle()
    if '--rifle-only' not in sys.argv:
        build_projectile()
