"""Fresh-process persistence verification. Does not save temporary test mutations."""
import json
from pathlib import Path
import unreal

root = '/Game/GameplayActors/LobbyPlatform'
out = Path(unreal.Paths.project_saved_dir()) / 'LobbyPlatform'
out.mkdir(parents=True, exist_ok=True)
world = unreal.EditorLoadingAndSavingUtils.load_map(root + '/Maps/Lvl_LobbyPlatformPreview')
assert world
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
bp = unreal.load_asset(root + '/BP_ParametricLobbyPlatform')
assert bp
cdo = unreal.get_default_object(bp.generated_class())
native = unreal.get_default_object(unreal.ParametricLobbyPlatform)
placed = next(a for a in actors.get_all_level_actors() if isinstance(a, unreal.ParametricLobbyPlatform))
keys = ('shape', 'width', 'depth', 'thickness', 'corner_radius', 'rim_width', 'bevel_size', 'surface_color',
        'body_color', 'yellow_color', 'cyan_color', 'surface_roughness', 'surface_metallic',
        'surface_material', 'trim_material', 'neon_intensity', 'neon_layer_position',
        'neon_layer_thickness', 'destructible', 'fit_pads_inside_platform', 'enable_collision')
for key in keys:
    assert placed.get_editor_property(key) == cdo.get_editor_property(key) == native.get_editor_property(key), key
assert placed.shape == unreal.LobbyPlatformShape.ROUNDED_RECTANGLE
assert not placed.layout_warning, placed.layout_warning
assert [str(p.id) for p in cdo.pads] == ['Center', 'Right', 'Left', 'Entrance', 'Back']
assert len(placed.paths) == 4
assert all(p.actor_class is None for p in cdo.pads)
assert all(str(p.id) == str(q.id) and p.position == q.position and p.radius == q.radius
           and p.show_circle == q.show_circle
           for p, q in zip(placed.pads, cdo.pads))
assert all(str(p.id) == str(q.id) and p.position == q.position and p.radius == q.radius
           and p.show_circle == q.show_circle
           for p, q in zip(cdo.pads, native.pads))
component = placed.get_dynamic_mesh_component()
assert component.get_dynamic_mesh().get_triangle_count() > 100
assert component.get_material(0).get_scalar_parameter_value('PadCount') == 3
assert component.get_material(0).get_scalar_parameter_value('SegmentCount') == 4
assert component.get_material(0).get_scalar_parameter_value('Roughness') < .25
for cls in (unreal.ParametricLobbyPlatform, bp.generated_class()):
    fresh = actors.spawn_actor_from_class(cls, unreal.Vector(10000, 0, 0), transient=True)
    for key in keys:
        assert fresh.get_editor_property(key) == cdo.get_editor_property(key), key
    assert len(fresh.get_pad_ids()) == 3
    assert fresh.get_dynamic_mesh_component().get_dynamic_mesh().get_triangle_count() == component.get_dynamic_mesh().get_triangle_count()
    # Editor inspection / construction must never create permanent gameplay actors.
    assert fresh.spawn_actor_at_pad('Center', unreal.StaticMeshActor) is None
    actors.destroy_actor(fresh)
assert placed.destructible and placed.damage_cut_count == 0
assert component.get_material(1).get_scalar_parameter_value('Emission') == 0
assert component.get_material(2).get_scalar_parameter_value('Emission') == 0
assert component.get_material(3).get_scalar_parameter_value('Emission') > 0
assert component.get_material(0).get_scalar_parameter_value('PathInset2') == 65
assert component.get_material(0).get_scalar_parameter_value('PathInset3') == 65
result = {'fresh_process_saved_assets': True, 'native_blueprint_placed_and_new_instances_match': True,
          'three_circle_pads_two_edge_arms': True, 'single_neon_layer': True, 'destructible_default_true': True,
          'four_linked_paths': True, 'material_instances_restored': True,
          'no_editor_gameplay_spawns': True, 'triangles': component.get_dynamic_mesh().get_triangle_count()}
(out / 'Verification.json').write_text(json.dumps(result, indent=2))
unreal.log('LOBBY_PLATFORM_VERIFIED ' + json.dumps(result))
