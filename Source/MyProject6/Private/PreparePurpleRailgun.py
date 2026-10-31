"""Inspect the purchased GLB and prepare a grip-space FBX without changing the source."""
import bpy, json, math
from pathlib import Path
from mathutils import Vector

root = Path(__file__).resolve().parents[3]
source = root / 'ContentSource/PurpleRailgun'
out = root / 'Saved/PurpleRailgun'
out.mkdir(parents=True, exist_ok=True)
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
bpy.ops.import_scene.gltf(filepath=str(source / 'energy_weapon.glb'))
meshes = [o for o in bpy.context.scene.objects if o.type == 'MESH']
report = []
for o in meshes:
    points = [o.matrix_world @ v.co for v in o.data.vertices]
    report.append({'name':o.name,'min':[min(v[i] for v in points) for i in range(3)],
                   'max':[max(v[i] for v in points) for i in range(3)],
                   'vertices':len(o.data.vertices),'polygons':len(o.data.polygons),
                   'materials':[m.name for m in o.data.materials],
                   'uv_layers':[uv.name for uv in o.data.uv_layers]})
(out/'SourceGeometry.json').write_text(json.dumps(report,indent=2))
# Inspect the source side-on before placing the hands and muzzle.
o=meshes[0]
center=sum((o.matrix_world @ Vector(c) for c in o.bound_box),Vector())/8
span=max(o.dimensions)
scene=bpy.context.scene
scene.render.engine='BLENDER_EEVEE_NEXT'
scene.render.resolution_x=1400;scene.render.resolution_y=900;scene.render.resolution_percentage=100
scene.world.color=(.15,.15,.15)
for loc,power,size in [(center+Vector((0,-span,span)),1800,span*2),(center+Vector((span,span,span)),2400,span*2)]:
    data=bpy.data.lights.new('InspectionLight','AREA');light=bpy.data.objects.new('InspectionLight',data)
    scene.collection.objects.link(light);light.location=loc;light.rotation_euler=(center-loc).to_track_quat('-Z','Y').to_euler()
    data.energy=power;data.shape='DISK';data.size=size
cam_data=bpy.data.cameras.new('InspectCamera');cam=bpy.data.objects.new('InspectCamera',cam_data)
scene.collection.objects.link(cam);cam.location=center+Vector((0,-span*2,0));cam.rotation_euler=(center-cam.location).to_track_quat('-Z','Y').to_euler()
cam_data.type='ORTHO';cam_data.ortho_scale=span*1.12;scene.camera=cam
scene.render.filepath=str(out/'SourceSide.png')
bpy.ops.render.render(write_still=True)
bpy.ops.wm.save_as_mainfile(filepath=str(source/'SourceInspection.blend'))
print('PURPLE_SOURCE_INSPECTED',json.dumps(report))
