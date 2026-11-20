"""Update the two shot materials, Comet mesh and existing player/pickup defaults."""
from pathlib import Path
import runpy
import unreal

here = Path(__file__).resolve().parent
lib = unreal.EditorAssetLibrary
edit = unreal.MaterialEditingLibrary
if '-RailTrailsOnly' not in unreal.SystemLibrary.get_command_line():
    # The support wrist is still at its original grip-space position.
    runpy.run_path(str(here / 'SetupPurpleRailgunAssets.py'))
    runpy.run_path(str(here / 'UpdatePickupMotifMaterial.py'))
runpy.run_path(str(here / 'SetupRailgunTrail.py'))
spiral = unreal.load_asset('/Game/Weapons/Railgun/Materials/M_RailSpiral')
shader = next(x for x in edit.get_material_expressions(spiral) if isinstance(x, unreal.MaterialExpressionCustom))
shader.set_editor_property('code', (here / 'RailgunSpiral.ush').read_text())
edit.recompile_material(spiral)
assert lib.save_loaded_asset(spiral, False)

player_bp = unreal.load_asset('/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter')
pickup_bp = unreal.load_asset('/Game/GameplayActors/Pickups/PurpleRailgun/BP_PurpleRailgunPickup')
native = unreal.get_default_object(unreal.MyProject6Character)
player_values = {p: native.get_editor_property(p) for p in ('railgun_visuals', 'purple_railgun_visuals')}
pickup_values = {'body_color': unreal.LinearColor(.065, .012, .019, 1),
                 'icon_color': unreal.LinearColor(1, .045, .065, 1)}

def apply(obj, values):
    changed = False
    for key, value in values.items():
        current = obj.get_editor_property(key)
        if isinstance(value, unreal.RailgunVisualSettings):
            fields = ('color', 'intensity', 'density', 'width', 'white_core', 'spiral_color',
                      'spiral', 'smooth_spiral', 'spiral_width', 'spiral_radius', 'spiral_pitch')
            if all(current.get_editor_property(k) == value.get_editor_property(k) for k in fields):
                continue
        elif current == value:
            continue
        obj.modify()
        obj.set_editor_property(key, value)
        changed = True
    return changed

for blueprint, values in [(player_bp, player_values), (pickup_bp, pickup_values)]:
    if apply(unreal.get_default_object(blueprint.generated_class()), values):
        unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
        assert lib.save_loaded_asset(blueprint, False)

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
changes = 0
for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    if isinstance(actor, unreal.MyProject6Character):
        changes += apply(actor, player_values)
    elif actor.get_class() == pickup_bp.generated_class() and apply(actor, pickup_values):
        actor.refresh_pickup()
        changes += 1
if changes:
    assert level.save_current_level()
unreal.log('RETRO_RAILGUN_ASSETS_READY placed_changes=' + str(changes))
