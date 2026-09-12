"""Create the reusable spawn point Blueprint without editing any level or existing asset.

Run after compiling the editor target:
UnrealEditor-Cmd MyProject6.uproject -run=pythonscript -script=<this file>
-EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities -unattended -nullrhi
"""
import unreal


def main():
    package = "/Game/NPC/Spawning"
    name = "BP_NPCSpawnPoint"
    path = f"{package}/{name}"
    existing = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
    if existing:
        unreal.log(f"NPC_SPAWN_POINT_READY: {path} (existing asset preserved)")
        return

    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", unreal.NPCSpawnPoint)
    blueprint = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, package, unreal.Blueprint, factory
    )
    if not blueprint:
        raise RuntimeError(f"Could not create {path}")
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    if not unreal.EditorAssetLibrary.save_loaded_asset(blueprint, only_if_is_dirty=False):
        raise RuntimeError(f"Could not save {path}")
    defaults = unreal.get_default_object(blueprint.generated_class())
    assert not defaults.get_editor_property("spawn_on_begin_play")
    assert defaults.get_editor_property("default_count") == 1
    unreal.log(f"NPC_SPAWN_POINT_READY: {path}")


main()
