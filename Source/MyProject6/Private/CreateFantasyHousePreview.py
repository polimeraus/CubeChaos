"""Create an isolated preview map with one building; do not modify the gameplay map."""
import json
from pathlib import Path
import unreal

path = '/Game/Structures/DynamicMesh/FantasyHouse/Maps/Lvl_FantasyHousePreview'
assert not unreal.EditorAssetLibrary.does_asset_exist(path), 'Preview already exists; preserving it'
world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
bp = unreal.load_asset('/Game/Structures/DynamicMesh/FantasyHouse/BP_ParametricFantasyHouse')
house = actors.spawn_actor_from_class(bp.generated_class(), unreal.Vector())
house.set_actor_label('FantasyHouse_Parametric')
house.set_folder_path('Fantasy House')
assert house.triangle_count == 8502
assert len(house.get_components_by_class(unreal.DynamicMeshComponent)) == 1

for label, rotation, intensity in [
    ('Preview_Key', unreal.Rotator(pitch=-35, yaw=120, roll=0), 3.5),
    ('Preview_Rim', unreal.Rotator(pitch=-25, yaw=-50, roll=0), 2.)]:
    light = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0,0,1800), rotation)
    light.set_actor_label(label)
    light.set_folder_path('Preview Lighting')
    light.light_component.set_editor_property('intensity', intensity)
    light.light_component.set_editor_property('mobility', unreal.ComponentMobility.MOVABLE)
    if intensity < 3: light.light_component.set_editor_property('cast_shadows', False)
sky = actors.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0,0,1600))
sky.set_actor_label('Preview_Ambient')
sky.set_folder_path('Preview Lighting')
component = sky.get_component_by_class(unreal.SkyLightComponent)
component.set_editor_property('mobility', unreal.ComponentMobility.MOVABLE)
component.set_editor_property('source_type', unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
component.set_editor_property('cubemap', unreal.load_asset('/Engine/MapTemplates/Sky/DaylightAmbientCubemap'))
component.set_editor_property('intensity', 1.1)
component.set_editor_property('cast_shadows', False)
component.set_editor_property('real_time_capture', False)

# A bounded neutral pad and player start make the preview useful in Play as well.
floor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0,0,-18))
floor.set_actor_label('Preview_Ground')
floor.set_folder_path('Preview Lighting')
floor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cylinder'))
floor.static_mesh_component.set_material(0, unreal.load_asset('/Engine/MapTemplates/Materials/BasicAsset03'))
floor.set_actor_scale3d(unreal.Vector(15,15,.36))
start = actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(0,-680,130), unreal.Rotator(0,90,0))
start.set_folder_path('Preview Lighting')
actors.set_selected_level_actors([house])
unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(
    unreal.Vector(1600,-2300,1500),
    unreal.MathLibrary.find_look_at_rotation(unreal.Vector(1600,-2300,1500),unreal.Vector(0,0,620)))
assert unreal.EditorLoadingAndSavingUtils.save_map(world, path)
out = Path(unreal.Paths.project_saved_dir()) / 'FantasyHouse'
(out/'SavedAsset.json').write_text(json.dumps({'blueprint': bp.get_path_name(), 'map':path,
    'actor':'FantasyHouse_Parametric', 'triangles':house.triangle_count,
    'building_actor_count':1, 'component_count':1}, indent=2))
unreal.log('FANTASY_HOUSE_PREVIEW_SAVED ' + path)
