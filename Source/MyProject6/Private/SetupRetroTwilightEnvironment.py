"""Create reusable sky material/Blueprint, replace only main-map global environment actors."""
import json
import shutil
from pathlib import Path
import unreal

root = '/Game/Environment/RetroTwilight'
out = Path(unreal.Paths.project_saved_dir()) / 'TwilightEnvironment'
out.mkdir(parents=True, exist_ok=True)
content = Path(unreal.Paths.project_content_dir())
backup = out / 'BeforeLighting'
if not backup.exists():
    backup.mkdir()
    shutil.copy2(content / 'FirstPerson/Lvl_FirstPerson.umap', backup / 'Lvl_FirstPerson.umap')
    external = content / '__ExternalActors__/FirstPerson/Lvl_FirstPerson'
    if external.exists():
        shutil.copytree(external, backup / 'ExternalActors')

assets = unreal.AssetToolsHelpers.get_asset_tools()
edit = unreal.MaterialEditingLibrary
mat_path = root + '/M_RetroTwilightSky'
material = unreal.load_asset(mat_path) if unreal.EditorAssetLibrary.does_asset_exist(mat_path) else assets.create_asset(
    'M_RetroTwilightSky', root, unreal.Material, unreal.MaterialFactoryNew())
assert material
material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
material.set_editor_property('two_sided', True)
material.set_editor_property('is_sky', True)
remaining = list(edit.get_material_expressions(material))
def expression(cls):
    for node in remaining:
        if isinstance(node, cls):
            remaining.remove(node)
            return node
    return edit.create_material_expression(material, cls)

nodes = {'ViewToCamera': (expression(unreal.MaterialExpressionCameraVectorWS), '')}
for key, value in {'DominantColor': (0.32, 0.22, 0.65), 'MoonDirection': (-0.742, 0.52, 0.407)}.items():
    node = expression(unreal.MaterialExpressionVectorParameter)
    node.set_editor_property('parameter_name', key)
    node.set_editor_property('default_value', unreal.LinearColor(*value, 1))
    nodes[key] = (node, 'RGB')
for key, value in {'SkyBrightness': 0.8, 'MoonVisibility': 1.0, 'MoonRadius': 0.0349066, 'StarBrightness': 0.35}.items():
    node = expression(unreal.MaterialExpressionScalarParameter)
    node.set_editor_property('parameter_name', key)
    node.set_editor_property('default_value', value)
    nodes[key] = (node, '')
custom = expression(unreal.MaterialExpressionCustom)
custom.set_editor_property('description', 'Twilight gradient, stars and aligned textured moon')
custom.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
custom.set_editor_property('code', (Path(__file__).parent / 'RetroTwilightSky.ush').read_text())
inputs = []
for key in nodes:
    item = unreal.CustomInput()
    item.set_editor_property('input_name', key)
    inputs.append(item)
custom.set_editor_property('inputs', inputs)
for key, (node, pin) in nodes.items():
    assert edit.connect_material_expressions(node, pin, custom, key)
assert edit.connect_material_property(custom, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
edit.layout_material_expressions(material)
edit.recompile_material(material)
assert unreal.EditorAssetLibrary.save_loaded_asset(material, False)

bp_path = root + '/BP_RetroTwilightEnvironment'
if unreal.EditorAssetLibrary.does_asset_exist(bp_path):
    bp = unreal.load_asset(bp_path)
else:
    factory = unreal.BlueprintFactory()
    factory.set_editor_property('parent_class', unreal.RetroTwilightEnvironment)
    bp = assets.create_asset('BP_RetroTwilightEnvironment', root, unreal.Blueprint, factory)
assert bp
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
cdo = unreal.get_default_object(bp.generated_class())
cdo.set_editor_property('sky_material', material)
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
assert unreal.EditorAssetLibrary.save_loaded_asset(bp, False)

world = unreal.EditorLoadingAndSavingUtils.load_map('/Game/FirstPerson/Lvl_FirstPerson')
assert world
subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
old_actors = list(subsystem.get_all_level_actors())
rigs = [a for a in old_actors if isinstance(a, unreal.RetroTwilightEnvironment)]
assert len(rigs) <= 1, 'Multiple twilight rigs; resolve explicitly before editing.'
removed = []
for actor in old_actors:
    legacy_sky_mesh = isinstance(actor, unreal.StaticMeshActor) and actor.get_actor_label() == 'SM_SkySphere'
    if legacy_sky_mesh or isinstance(actor, (unreal.DirectionalLight, unreal.SkyLight, unreal.ExponentialHeightFog,
                                           unreal.SkyAtmosphere, unreal.VolumetricCloud)):
        removed.append(dict(label=actor.get_actor_label(), cls=actor.get_class().get_path_name()))
        assert subsystem.destroy_actor(actor)
rig = rigs[0] if rigs else subsystem.spawn_actor_from_class(bp.generated_class(), unreal.Vector(0, 0, 0))
assert rig
rig.set_actor_label('RetroTwilightEnvironment')
rig.set_folder_path('Environment')
# A global rig must not disappear when World Partition cells unload.
rig.set_editor_property('is_spatially_loaded', False)
rig.apply_environment()
# Explicitly save the persistent level as well as dirty external actor packages.
# Commandlet actor destruction does not necessarily mark the .umap itself dirty.
assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
assert unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
if (out / 'setup.json').exists():
    previous = json.loads((out / 'setup.json').read_text())
    removed = list({row['label']: row for row in previous['removed'] + removed}.values())
result = dict(passed=True, removed=removed, actor=rig.get_actor_label(), blueprint=bp_path,
              original_actor_count=len(old_actors), final_actor_count=len(subsystem.get_all_level_actors()))
(out / 'setup.json').write_text(json.dumps(result, indent=2))
unreal.log('TWILIGHT_SETUP_OK ' + json.dumps(result))
