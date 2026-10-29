"""Create the independent cell-based Blueprint and preview without modifying the original lobby."""
from pathlib import Path
import json
import unreal

ROOT='/Game/GameplayActors/LobbyPlatformDestructable'
assets=unreal.AssetToolsHelpers.get_asset_tools()
path=ROOT+'/BP_ParametricLobbyPlatformDestructable'
bp=unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
if not bp:
    factory=unreal.BlueprintFactory()
    factory.set_editor_property('parent_class',unreal.ParametricLobbyPlatformDestructable)
    bp=assets.create_asset('BP_ParametricLobbyPlatformDestructable',ROOT,unreal.Blueprint,factory)
assert bp
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
assert unreal.EditorAssetLibrary.save_loaded_asset(bp,False)

preview=ROOT+'/Maps/Lvl_LobbyPlatformDestructablePreview'
source='/Game/GameplayActors/LobbyPlatform/Maps/Lvl_LobbyPlatformPreview'
world=unreal.EditorLoadingAndSavingUtils.load_map(preview if unreal.EditorAssetLibrary.does_asset_exist(preview) else source)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
old=next((a for a in actors.get_all_level_actors() if isinstance(a,(unreal.ParametricLobbyPlatform,unreal.ParametricLobbyPlatformDestructable))),None)
transform=old.get_actor_transform() if old else unreal.Transform()
if old: actors.destroy_actor(old)
lobby=actors.spawn_actor_from_class(bp.generated_class(),transform.translation,transform.rotation.rotator())
lobby.set_actor_scale3d(transform.scale3d)
lobby.set_actor_label('LobbyPlatformDestructable_Reference')
lobby.set_folder_path('Lobby Platform Destructable')
lobby.rebuild_platform()
actors.set_selected_level_actors([lobby])
assert not lobby.layout_warning,lobby.layout_warning
assert unreal.EditorLoadingAndSavingUtils.save_map(world,preview)
out=Path(unreal.Paths.project_saved_dir())/'LobbyDestructable'
out.mkdir(parents=True,exist_ok=True)
(out/'Setup.json').write_text(json.dumps({'blueprint':path,'map':preview,'pads':[str(n) for n in lobby.get_pad_ids()]},indent=2))
unreal.log('LOBBY_DESTRUCTABLE_SETUP_OK')
