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
# The initial setup and subsequent upgrades share the same shader inputs.
import sys
sys.path.insert(0, str(Path(__file__).parent))
from BuildRetroTwilightMaterial import build_material
material = build_material()

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
