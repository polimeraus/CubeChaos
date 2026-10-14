"""Create reusable temple assets and a separate playable preview map. Safe to run again."""
import json
from pathlib import Path
import unreal

folder = '/Game/Structures/Ziggurat'
assets = unreal.AssetToolsHelpers.get_asset_tools()
edit = unreal.MaterialEditingLibrary
out = Path(unreal.Paths.project_saved_dir()) / 'Ziggurat'
out.mkdir(parents=True, exist_ok=True)

def load(path):
    return unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None

master = load(folder + '/M_ZigguratStone')
if not master:
    master = assets.create_asset('M_ZigguratStone', folder, unreal.Material, unreal.MaterialFactoryNew())
    tint = edit.create_material_expression(master, unreal.MaterialExpressionVectorParameter, -600, 0)
    tint.set_editor_property('parameter_name', 'StoneColor')
    tint.set_editor_property('default_value', unreal.LinearColor(.55, .34, .17, 1))
    rough = edit.create_material_expression(master, unreal.MaterialExpressionScalarParameter, -250, 300)
    rough.set_editor_property('parameter_name', 'Roughness')
    rough.set_editor_property('default_value', .85)
    position = edit.create_material_expression(master, unreal.MaterialExpressionWorldPosition, -800, 200)
    noise = edit.create_material_expression(master, unreal.MaterialExpressionNoise, -600, 180)
    noise.set_editor_property('scale', .035)
    noise.set_editor_property('levels', 2)
    noise.set_editor_property('quality', 1)
    noise.set_editor_property('output_min', .86)
    noise.set_editor_property('output_max', 1.0)
    multiply = edit.create_material_expression(master, unreal.MaterialExpressionMultiply, -200, 0)
    edit.connect_material_expressions(position, '', noise, 'Position')
    edit.connect_material_expressions(noise, '', multiply, 'B')
    edit.connect_material_expressions(tint, '', multiply, 'A')
    edit.connect_material_property(multiply, '', unreal.MaterialProperty.MP_BASE_COLOR)
    edit.connect_material_property(rough, '', unreal.MaterialProperty.MP_ROUGHNESS)
    edit.layout_material_expressions(master)
    assert not edit.recompile_material(master)
    assert unreal.EditorAssetLibrary.save_loaded_asset(master, False)

palette = {}
for name, color in {
    'Wall': (.55,.34,.17), 'Trim': (.78,.56,.31), 'Terrace': (.64,.43,.23),
    'Railing': (.40,.26,.13), 'Stair': (.49,.32,.17), 'Crown': (.065,.23,.25),
    'Stage': (.16,.145,.12),
}.items():
    material = load(folder + '/MI_Ziggurat' + name)
    if not material:
        material = assets.create_asset('MI_Ziggurat' + name, folder, unreal.MaterialInstanceConstant,
                                       unreal.MaterialInstanceConstantFactoryNew())
        edit.set_material_instance_parent(material, master)
        edit.set_material_instance_vector_parameter_value(material, 'StoneColor', unreal.LinearColor(*color, 1))
        edit.set_material_instance_scalar_parameter_value(material, 'Roughness', .78 if name == 'Crown' else .9)
        assert unreal.EditorAssetLibrary.save_loaded_asset(material, False)
    if name != 'Stage': palette[name.lower() + '_material'] = material

bp = load(folder + '/BP_DestructibleZiggurat')
if not bp:
    factory = unreal.BlueprintFactory()
    factory.set_editor_property('parent_class', unreal.ChunkedDestructibleZiggurat)
    bp = assets.create_asset('BP_DestructibleZiggurat', folder, unreal.Blueprint, factory)
    defaults = unreal.get_default_object(bp.generated_class())
    for key, material in palette.items(): defaults.set_editor_property(key, material)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    assert unreal.EditorAssetLibrary.save_loaded_asset(bp, False)

map_path = folder + '/Lvl_ZigguratPreview'
if not unreal.EditorAssetLibrary.does_asset_exist(map_path):
    world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    temple = actors.spawn_actor_from_class(bp.generated_class(), unreal.Vector())
    temple.set_actor_label('Ziggurat_Parametric')
    temple.set_folder_path('Ziggurat')
    assert temple.generated_piece_count > 100
    for label, rotation, intensity in [
        ('Key', unreal.Rotator(pitch=-38,yaw=130,roll=0), 4.5),
        ('Fill', unreal.Rotator(pitch=-25,yaw=-40,roll=0), 2.0)]:
        light = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0,0,3000), rotation)
        light.set_actor_label('Preview_' + label)
        light.set_folder_path('Preview')
        light.light_component.set_editor_property('intensity', intensity)
        light.light_component.set_editor_property('mobility', unreal.ComponentMobility.MOVABLE)
        if label == 'Fill': light.light_component.set_editor_property('cast_shadows', False)
    sky = actors.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0,0,2500))
    sky.set_folder_path('Preview')
    component = sky.get_component_by_class(unreal.SkyLightComponent)
    component.set_editor_property('mobility', unreal.ComponentMobility.MOVABLE)
    component.set_editor_property('source_type', unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
    component.set_editor_property('cubemap', unreal.load_asset('/Engine/MapTemplates/Sky/DaylightAmbientCubemap'))
    component.set_editor_property('intensity', .9)
    component.set_editor_property('cast_shadows', False)
    ground = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0,0,-35))
    ground.set_actor_label('Preview_Ground')
    ground.set_folder_path('Preview')
    ground.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
    ground.static_mesh_component.set_material(0, load(folder + '/MI_ZigguratStage'))
    ground.set_actor_scale3d(unreal.Vector(70,70,.7))
    start = actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(0,-2200,120),
                                         unreal.Rotator(pitch=0,yaw=90,roll=0))
    start.set_folder_path('Preview')
    location = unreal.Vector(3800,-4900,3200)
    unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(
        location, unreal.MathLibrary.find_look_at_rotation(location, unreal.Vector(0,0,800)))
    actors.set_selected_level_actors([temple])
    assert unreal.EditorLoadingAndSavingUtils.save_map(world, map_path)
    (out/'Setup.json').write_text(json.dumps({'blueprint':bp.get_path_name(),'map':map_path,
        'pieces':temple.generated_piece_count,'windows':temple.generated_window_count,
        'height_cm':temple.generated_height}, indent=2))
else:
    # Refresh only this tool's preview stage and generated native geometry.
    world = unreal.EditorLoadingAndSavingUtils.load_map(map_path)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for actor in actors.get_all_level_actors():
        label = actor.get_actor_label()
        if label == 'Preview_Ground':
            actor.static_mesh_component.set_material(0, load(folder + '/MI_ZigguratStage'))
        elif label in ('Preview_Key','Preview_Fill'):
            key = label == 'Preview_Key'
            actor.set_actor_rotation(unreal.Rotator(pitch=-38 if key else -25,yaw=130 if key else -40,roll=0),False)
            actor.light_component.set_editor_property('intensity',4.5 if key else 2.)
        elif isinstance(actor,unreal.PlayerStart) and str(actor.get_folder_path()) == 'Preview':
            actor.set_actor_rotation(unreal.Rotator(pitch=0,yaw=90,roll=0),False)
        elif isinstance(actor,unreal.ChunkedDestructibleZiggurat):
            actor.rebuild_ziggurat()
    assert unreal.EditorLoadingAndSavingUtils.save_map(world,map_path)
unreal.log('ZIGGURAT_SETUP_OK ' + bp.get_path_name())
