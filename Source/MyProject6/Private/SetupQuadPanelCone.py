"""Create an independent Blueprint and a placed clone; never modify the source cone.

Run once with the editor closed after building the new class. Re-running leaves the
existing clone alone. Original external-actor package hashes are checked after save.
"""
from datetime import datetime
from pathlib import Path
import hashlib
import json
import shutil
import unreal

output = Path(unreal.Paths.project_saved_dir()) / 'QuadCone'
output.mkdir(parents=True, exist_ok=True)
content = Path(unreal.Paths.project_content_dir())
level = '/Game/FirstPerson/Lvl_FirstPerson'
backup = output / ('LevelBackup_' + datetime.now().strftime('%Y%m%d_%H%M%S'))
files = [content / 'FirstPerson/Lvl_FirstPerson.umap'] + list((content / '__ExternalActors__/FirstPerson/Lvl_FirstPerson').rglob('*.uasset'))
before = {str(p.relative_to(content)): hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
for path in files:
    destination = backup / path.relative_to(content)
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(path, destination)

assert unreal.EditorLoadingAndSavingUtils.load_map(level)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
originals = [actor for actor in actors.get_all_level_actors() if isinstance(actor, unreal.ChunkedDestructibleConeStructure)]
assert len(originals) == 1, f'Expected one source cone, found {len(originals)}'
source = originals[0]
assert not any(isinstance(a, unreal.QuadPanelConeStructure) for a in actors.get_all_level_actors()), 'Clone already exists; leaving it unchanged'
source_transform = source.get_actor_transform()
source_metrics = source.get_cone_structure_metrics().generated_block_count
path = '/Game/Structures/DynamicMesh/QuadPanelCone/BP_QuadPanelCone'
assert not unreal.EditorAssetLibrary.does_asset_exist(path), 'Blueprint already exists; leaving it unchanged'
factory = unreal.BlueprintFactory()
factory.set_editor_property('parent_class', unreal.QuadPanelConeStructure)
blueprint = unreal.AssetToolsHelpers.get_asset_tools().create_asset('BP_QuadPanelCone', '/Game/Structures', unreal.Blueprint, factory)
assert blueprint
defaults = unreal.get_default_object(blueprint.generated_class())
defaults.modify()
defaults.copy_settings_from_cone(source)
defaults.set_editor_property('wall_panel_segments', 48)
# Use the existing seamless-panel mode; visible joints are opt-in.
defaults.set_editor_property('panel_joint_width', 0.)
unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
assert unreal.EditorAssetLibrary.save_loaded_asset(blueprint, False)

# Put the new structure beside the source with space between their outer platforms.
unit = min(source.get_editor_property('block_dimensions').x, source.get_editor_property('block_dimensions').y)
radius = (source.get_editor_property('bottom_radius_blocks') + source.get_editor_property('platform_overhang_blocks')) * unit
offset = unreal.Vector(radius * 2 + 1200., 0, 0)
clone = actors.spawn_actor_from_class(blueprint.generated_class(), source.get_actor_location() + offset, source.get_actor_rotation())
assert clone
clone.set_actor_scale3d(source.get_actor_scale3d())
clone.set_actor_label('QuadPanelCone_Simple')
clone.set_folder_path('Structures/Quad Panel Cone')
assert source.get_actor_transform() == source_transform
assert source.get_cone_structure_metrics().generated_block_count == source_metrics
assert unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
changed_original_packages = []
for relative, expected in before.items():
    if relative.endswith('.umap'): continue  # Level registry adds the new actor.
    if hashlib.sha256((content / relative).read_bytes()).hexdigest() != expected:
        changed_original_packages.append(relative)
assert not changed_original_packages, f'Original actor packages changed: {changed_original_packages}'
report = {'blueprint':path, 'actor':clone.get_actor_label(), 'source':source.get_actor_label(),
          'source_actor_packages_unchanged':True, 'backup':str(backup),
          'location':str(clone.get_actor_location()), 'height':defaults.get_editor_property('cone_height_blocks'),
          'wall_panel_segments':48, 'panel_joint_width_cm':0.}
(output / 'Setup.json').write_text(json.dumps(report, indent=2))
unreal.log('QUAD_CONE_SETUP_OK ' + json.dumps(report))
