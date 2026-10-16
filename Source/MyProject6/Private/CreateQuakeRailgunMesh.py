"""Original Q3-style railgun reconstruction. Run with Blender --background --python.
Dimensions in metres, grip at origin, barrel along +Y. No game assets are extracted.
"""
import bpy, math
from pathlib import Path

out = Path(__file__).resolve().parents[3] / 'ContentSource/Railgun'
out.mkdir(parents=True, exist_ok=True)
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)

def material(name, color, metal, rough):
    m = bpy.data.materials.new(name)
    m.diffuse_color = (*color, 1)
    m.use_nodes = True
    p = m.node_tree.nodes.get('Principled BSDF')
    p.inputs['Base Color'].default_value = (*color, 1)
    p.inputs['Metallic'].default_value = metal
    p.inputs['Roughness'].default_value = rough
    return m

silver = material('RailSilver', (.39, .43, .45), .82, .3)
dark = material('RailGunmetal', (.045, .06, .065), .75, .4)
black = material('RailRubber', (.013, .019, .018), .08, .65)
energy = material('RailEnergy', (.008, .22, 1.), .25, .25)
brass = material('RailBrass', (.28, .20, .07), .8, .35)

def finish(o, name, mat, bevel=0):
    o.name = name
    o.data.materials.append(mat)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    if bevel:
        b = o.modifiers.new('Machined edges', 'BEVEL')
        b.width = bevel; b.segments = 2
        bpy.ops.object.modifier_apply(modifier=b.name)
        n = o.modifiers.new('Weighted normals', 'WEIGHTED_NORMAL')
        bpy.ops.object.modifier_apply(modifier=n.name)
    return o

def box(name, loc, size, mat, bevel=.003):
    bpy.ops.mesh.primitive_cube_add(size=1, location=loc)
    o = bpy.context.object; o.dimensions = size
    return finish(o, name, mat, bevel)

def cylinder(name, y, radius, length, mat, x=0, z=.1, verts=32):
    bpy.ops.mesh.primitive_cylinder_add(vertices=verts, radius=radius, depth=length,
        location=(x, y, z), rotation=(math.pi / 2, 0, 0))
    return finish(bpy.context.object, name, mat, .0015)

def ring(name, y, outer, inner, width, mat):
    vertices=[]; faces=[]; n=40
    for cy, radius in [(y-width/2,outer),(y+width/2,outer),(y-width/2,inner),(y+width/2,inner)]:
        vertices.extend((radius*math.cos(i*math.tau/n),cy,.1+radius*math.sin(i*math.tau/n)) for i in range(n))
    for i in range(n):
        j=(i+1)%n
        faces.extend([(i,j,n+j,n+i),(2*n+i,3*n+i,3*n+j,2*n+j),
                      (i,2*n+i,2*n+j,j),(n+i,n+j,3*n+j,3*n+i)])
    mesh=bpy.data.meshes.new(name); mesh.from_pydata(vertices,[],[tuple(reversed(f)) for f in faces]); mesh.update()
    o=bpy.data.objects.new(name,mesh); bpy.context.collection.objects.link(o)
    bpy.context.view_layer.objects.active=o; o.select_set(True)
    return finish(o,name,mat,.001)

# Broad rear housing, long exposed accelerator and the distinctive repeated clamps.
box('Rear receiver',(0,-.065,.10),(.148,.20,.135),silver,.012)
box('Receiver top',(0,-.06,.171),(.095,.15,.018),dark)
box('Energy display bezel',(0,-.055,.183),(.084,.084,.006),silver,.003)
box('Flowing energy display',(0,-.055,.187),(.068,.068,.003),energy,.001)
box('Back plate',(0,-.169,.10),(.132,.012,.112),dark)
for x in [-.076,.076]:
    box('Receiver inset',(x,-.07,.103),(.004,.115,.068),dark,.002)
    for y in [-.108,-.085,-.062,-.039]:
        box('Cooling fins',(x*1.02,y,.103),(.007,.010,.058),silver,.001)
cylinder('Accelerator dark bed',.305,.044,.55,dark)
cylinder('Blue induction chamber',.285,.046,.46,energy)
for y in [.065,.155,.245,.335,.425,.515]:
    ring('Induction clamp',y,.063,.045,.026,silver)
    ring('Clamp gasket',y+.018,.054,.044,.007,black)
for x in [-.053,.053]:
    box('Longitudinal magnetic rail',(x,.29,.133),(.017,.52,.027),silver,.004)
    box('Inset rail',(x,.29,.151),(.008,.49,.007),dark,.001)
box('Top accelerator spine',(0,.31,.159),(.024,.49,.019),dark)
box('Underslung frame',(0,.27,.043),(.066,.46,.025),dark,.007)
for y in [.08,.17,.26,.35,.44,.53]:
    for x in [-.064,.064]:
        cylinder('Clamp fastener',y,.006,.007,brass,x=x,z=.127,verts=12)
cylinder('Muzzle housing',.591,.068,.096,silver)
ring('Muzzle gasket',.635,.070,.035,.018,dark)
ring('Muzzle rim',.654,.058,.031,.027,silver)
cylinder('Recessed bore',.645,.030,.002,black)
ring('Energy aperture',.653,.034,.029,.003,energy)
box('Forward sight',(0,.58,.174),(.016,.04,.025),dark)
box('Sight tip',(0,.58,.189),(.007,.012,.006),energy,.001)

# Trigger hand and support hand use the existing two-handed pose.
grip=box('Pistol grip',(0,-.025,-.035),(.046,.062,.135),black,.01)
grip.rotation_euler[0]=-.16
for z in [-.075,-.053,-.031,-.009]:
    box('Grip ribs',(0,-.055,z),(.048,.008,.007),dark,.002)
box('Trigger guard base',(0,.048,-.043),(.025,.092,.012),silver)
box('Trigger guard front',(0,.091,-.013),(.025,.012,.068),silver)
box('Trigger',(0,.036,-.006),(.012,.011,.034),dark)
box('Fore-end contact',(0,.20,.025),(.074,.16,.022),black,.008)

bpy.ops.object.select_all(action='SELECT')
bpy.context.view_layer.objects.active=bpy.context.selected_objects[0]
bpy.ops.object.join()
o=bpy.context.object; o.name='SM_QuakeRailgun'
bpy.context.scene.cursor.location=(0,0,0)
bpy.ops.object.origin_set(type='ORIGIN_CURSOR')
bpy.ops.object.transform_apply(location=False,rotation=True,scale=True)
# Blender FBX -> Unreal flips Y. Bake the compensation so the imported barrel is +Y.
o.rotation_euler[2]=math.pi
bpy.ops.object.transform_apply(location=False,rotation=True,scale=True)
bpy.ops.export_scene.fbx(filepath=str(out/'SM_QuakeRailgun.fbx'),use_selection=True,object_types={'MESH'},
    add_leaf_bones=False,axis_forward='-Y',axis_up='Z',bake_anim=False,mesh_smooth_type='FACE')
bpy.ops.wm.save_as_mainfile(filepath=str(out/'Railgun_Source.blend'))
print('QUAKE_RAILGUN_MESH_READY')
