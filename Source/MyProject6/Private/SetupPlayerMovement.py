"""Apply the faster movement defaults to the existing player Blueprint as well."""
import json
from pathlib import Path
import unreal

path = '/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter'
blueprint = unreal.load_asset(path)
player = unreal.get_default_object(unreal.load_class(None, path + '.BP_FirstPersonCharacter_C'))
movement = player.get_component_by_class(unreal.CharacterMovementComponent)
movement.set_editor_property('max_walk_speed', 900.0)
movement.set_editor_property('jump_z_velocity', 600.0)
unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
assert unreal.EditorAssetLibrary.save_loaded_asset(blueprint, only_if_is_dirty=False)

# Spawn the actual Blueprint so any construction-script overrides are exercised too.
unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actor = actors.spawn_actor_from_class(blueprint.generated_class(), unreal.Vector(0, 0, 10000), transient=True)
assert actor, 'Player Blueprint could not be spawned'
try:
    actual = actor.get_component_by_class(unreal.CharacterMovementComponent)
    values = {name: actual.get_editor_property(name) for name in ['max_walk_speed', 'jump_z_velocity']}
    assert values == {'max_walk_speed': 900.0, 'jump_z_velocity': 600.0}, values
    output = Path(unreal.Paths.project_saved_dir(), 'Analysis')
    output.mkdir(parents=True, exist_ok=True)
    (output / 'PlayerMovementAfter.json').write_text(json.dumps(values, indent=2))
    unreal.log('PLAYER_MOVEMENT_OK ' + json.dumps(values))
finally:
    actors.destroy_actor(actor)
