"""Fresh saved-asset verification, plus exact preservation of the old actor and level."""
import hashlib,json
from pathlib import Path
import unreal
out=Path(unreal.Paths.project_saved_dir())/'LobbyDestructable'
original=json.loads((out/'OriginalHashes.json').read_text(encoding='utf-8-sig'))
for item in original:
    assert hashlib.sha256(Path(item['Path']).read_bytes()).hexdigest().upper()==item['Hash'],item['Path']
root='/Game/GameplayActors/LobbyPlatformDestructable'
world=unreal.EditorLoadingAndSavingUtils.load_map(root+'/Maps/Lvl_LobbyPlatformDestructablePreview')
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
bp=unreal.load_asset(root+'/BP_ParametricLobbyPlatformDestructable')
cdo=unreal.get_default_object(bp.generated_class())
native=unreal.get_default_object(unreal.ParametricLobbyPlatformDestructable)
placed=next(a for a in actors.get_all_level_actors() if isinstance(a,unreal.ParametricLobbyPlatformDestructable))
keys=('shape','width','depth','thickness','cell_size','destructible','surface_material','trim_material',
      'neon_intensity','neon_layer_position','neon_layer_thickness','planar_animation_type','music_animation_type','react_to_music')
for key in keys: assert placed.get_editor_property(key)==cdo.get_editor_property(key)==native.get_editor_property(key),key
assert placed.destructible
assert cdo.get_editor_property('planar_animation_type')==unreal.PlanarAnimationType.FLOWING_WAVE
assert cdo.get_editor_property('music_animation_type')==unreal.MusicAnimationType.EQUALIZER
def chunks(a): return [c for c in a.get_components_by_class(unreal.DynamicMeshComponent) if not c.component_has_tag('PlanarAnimationSurface')]
assert len(chunks(placed))>1, 'Saved actor did not restore chunks'
assert all(c.get_dynamic_mesh().get_triangle_count()>0 for c in chunks(placed))
for cls in (unreal.ParametricLobbyPlatformDestructable,bp.generated_class()):
    fresh=actors.spawn_actor_from_class(cls,unreal.Vector(10000,0,0),transient=True)
    for key in keys: assert fresh.get_editor_property(key)==cdo.get_editor_property(key),key
    assert len(fresh.get_pad_ids())==3
    assert len(chunks(fresh))==len(chunks(placed))
    assert fresh.spawn_actor_at_pad('Center',unreal.CameraActor) is None
    actors.destroy_actor(fresh)
for c in chunks(placed):
    assert c.get_material(0).get_scalar_parameter_value('PadCount')==3
    assert c.get_material(0).get_scalar_parameter_value('SegmentCount')==4
    assert c.get_material(3).get_scalar_parameter_value('Emission')>0
    assert c.get_material(1).get_scalar_parameter_value('Emission')==0
    assert c.get_material(2).get_scalar_parameter_value('Emission')==0
result={'passed':True,'original_actor_materials_and_level_unchanged':True,'native_blueprint_placed_and_new_defaults_match':True,
        'default_wave_and_equalizer_enabled':True,'chunks':len(chunks(placed)),'single_neon_layer':True,'named_spawn_pads':True}
(out/'Verification.json').write_text(json.dumps(result,indent=2))
unreal.log('LOBBY_DESTRUCTABLE_VERIFIED '+json.dumps(result))
