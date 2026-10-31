"""Convert the licensed source to centimetre-ready grip space and extract its PBR maps."""
import bpy, json, math, struct
from pathlib import Path
from mathutils import Vector, Matrix

root=Path(__file__).resolve().parents[3]
source=root/'ContentSource/PurpleRailgun'
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
bpy.ops.import_scene.gltf(filepath=str(source/'energy_weapon.glb'))
o=next(o for o in bpy.context.scene.objects if o.type=='MESH')
world=o.matrix_world.copy()
scale=.78/2.397229552268982
grip=Vector((-.79,.028942205,1.18))
for v in o.data.vertices:
    p=(world@v.co-grip)*scale
    v.co=Vector((p.y,-p.x,p.z))
o.matrix_world=Matrix.Identity(4)
o.name='SM_PurpleRailgun'
o.data.materials[0].name='PurpleBody';o.data.materials[1].name='PurpleEnergy'
# All authored textures use TEXCOORD_1; retain it as UV0 for a conventional UE material.
old=o.data.uv_layers[0];o.data.uv_layers.remove(old);o.data.uv_layers[0].name='UVMap'
o.data.uv_layers.active_index=0;o.data.uv_layers[0].active_render=True
o.data.update()
# Preserve full source geometry at LOD0; UE supplies reduced distance LODs.
bpy.context.view_layer.objects.active=o;o.select_set(True)
# FBX -> Unreal flips Y. glTF imports use quaternion rotation mode, so switch
# explicitly before baking the half turn (writing rotation_euler alone is ignored).
o.rotation_mode='XYZ'
o.rotation_euler[2]=math.pi
bpy.ops.object.transform_apply(location=False,rotation=True,scale=True)
bpy.ops.export_scene.fbx(filepath=str(source/'SM_PurpleRailgun.fbx'),use_selection=True,object_types={'MESH'},
    add_leaf_bones=False,axis_forward='-Y',axis_up='Z',bake_anim=False,mesh_smooth_type='FACE')
# Extract exact embedded images; avoid a destructive material bake or texture recompression.
blob=(source/'energy_weapon.glb').read_bytes();n=struct.unpack_from('<I',blob,12)[0]
data=json.loads(blob[20:20+n]);binary_offset=28+n
tex=source/'Textures';tex.mkdir(exist_ok=True)
for image in data['images']:
    view=data['bufferViews'][image['bufferView']];start=binary_offset+view.get('byteOffset',0)
    ext='.png' if image['mimeType']=='image/png' else '.jpg'
    (tex/(image['name']+ext)).write_bytes(blob[start:start+view['byteLength']])
def point(x,y,z):
    p=(Vector((x,y,z))-grip)*scale*100
    return [p.y,-p.x,p.z]
metadata={'grip_source':list(grip),'length_cm':78.,'muzzle':point(-2.5553,.02894,1.51),
          'support_wrist':[5.2,31.5,-2.0], 'source_listing':'https://www.fab.com/listings/8e7f9647-d063-4b8b-bd41-12d674f39500'}
(source/'GripSpace.json').write_text(json.dumps(metadata,indent=2))
bpy.ops.wm.save_as_mainfile(filepath=str(source/'PurpleRailgun.blend'))
print('PURPLE_MESH_READY',json.dumps(metadata))
