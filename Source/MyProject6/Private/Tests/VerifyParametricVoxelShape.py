"""Fresh-process asset persistence and geometry integration tests. Never saves test mutations."""
import json
import math
from pathlib import Path
import unreal

folder = '/Game/Structures/Parametric/VoxelShape'
output = Path(unreal.Paths.project_saved_dir()) / 'VoxelShape'
output.mkdir(parents=True, exist_ok=True)
world = unreal.EditorLoadingAndSavingUtils.load_map(folder + '/Maps/Lvl_VoxelShapePreview')
assert world
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
bp = unreal.load_asset(folder + '/BP_ParametricVoxelShape')
assert bp
placed = next(a for a in actors.get_all_level_actors() if a.get_actor_label() == 'Voxel_Heart')
native = unreal.get_default_object(unreal.ParametricVoxelShape)
cdo = unreal.get_default_object(bp.generated_class())
keys = ('shape', 'width_cubes', 'height_cubes', 'preserve_aspect_ratio', 'depth_cubes', 'cube_size',
        'cube_gap', 'center_pivot', 'appearance', 'wire_thickness', 'color', 'emissive_intensity', 'shape_material')
original = placed.generated_cube_count
assert original > 100 and placed.wire_edges.get_instance_count() == placed.generated_edge_count
assert not placed.generation_warning
assert cdo.shape_material.get_path_name() == folder + '/Materials/M_VoxelShapeGlow.M_VoxelShapeGlow'
assert cdo.shape_material.get_editor_property('blend_mode') == unreal.BlendMode.BLEND_OPAQUE
assert cdo.shape_material.get_editor_property('used_with_instanced_static_meshes')
for cls in (unreal.ParametricVoxelShape, bp.generated_class()):
    fresh = actors.spawn_actor_from_class(cls, unreal.Vector(15000, 0, 0), transient=True)
    for key in keys:
        assert fresh.get_editor_property(key) == placed.get_editor_property(key) == cdo.get_editor_property(key) == native.get_editor_property(key), key
    assert fresh.generated_cube_count == original
    assert fresh.generated_edge_count == placed.generated_edge_count
    actors.destroy_actor(fresh)

test = actors.spawn_actor_from_class(bp.generated_class(), unreal.Vector(15000, 0, 0), transient=True)
results = {'fresh_process_persistence': True, 'native_blueprint_saved_and_new_instances_match': True,
           'saved_heart_cubes': original, 'saved_heart_edges': placed.generated_edge_count, 'presets': {}}
try:
    presets = [unreal.VoxelShapePreset.HEART, unreal.VoxelShapePreset.INVADER_CRAB,
               unreal.VoxelShapePreset.INVADER_SQUID, unreal.VoxelShapePreset.GHOST,
               unreal.VoxelShapePreset.CHOMPER, unreal.VoxelShapePreset.STAR]
    for preset in presets:
        test.configure_shape(preset, 19, 1)
        count = test.generated_cube_count
        assert count > 0 and not test.generation_warning
        test.configure_shape(preset, 19, 3)
        assert test.generated_cube_count == count * 3
        assert test.generated_grid.x == 19 and test.generated_grid.y == 3
        test.set_editor_property('appearance', unreal.VoxelShapeAppearance.SOLID_CUBES)
        test.rebuild_shape()
        assert test.wire_edges.get_instance_count() == 0
        assert test.solid_cubes.get_instance_count() == count * 3
        for i in range(test.solid_cubes.get_instance_count()):
            transform = test.solid_cubes.get_instance_transform(i, False)
            scale = transform.scale3d
            assert abs(scale.x - .4) < .0001 and scale.x == scale.y == scale.z
        results['presets'][str(preset)] = {'one_layer_cubes': count, 'three_layer_cubes': test.generated_cube_count}
        test.set_editor_property('appearance', unreal.VoxelShapeAppearance.NEON_WIRE_CUBES)

    # A joined pair has 20 unique edges; separated cubes have 24. This also catches stale geometry.
    test.set_editor_property('custom_pattern_rows', ['1'])
    test.configure_shape(unreal.VoxelShapePreset.CUSTOM, 1, 1)
    assert test.generated_cube_count == 1 and test.generated_edge_count == 12
    test.configure_shape(unreal.VoxelShapePreset.CUSTOM, 1, 2)
    assert test.generated_cube_count == 2 and test.generated_edge_count == 20
    test.set_editor_property('cube_gap', 4.)
    test.rebuild_shape()
    assert test.generated_edge_count == 24 and abs(test.generated_size.y - 84.) < .001
    test.set_editor_property('preserve_aspect_ratio', False)
    test.set_editor_property('height_cubes', 5)
    test.configure_shape(unreal.VoxelShapePreset.CUSTOM, 7, 3)
    assert test.generated_cube_count == 105
    grid = test.generated_grid
    assert (grid.x, grid.y, grid.z) == (7, 3, 5), str(grid)
    test.set_editor_property('center_pivot', False)
    test.set_editor_property('appearance', unreal.VoxelShapeAppearance.SOLID_CUBES)
    test.rebuild_shape()
    positions = [test.solid_cubes.get_instance_transform(i, False).translation for i in range(105)]
    assert abs(min(p.z for p in positions) - 20.) < .001
    assert abs(max(p.x for p in positions) - min(p.x for p in positions) + 40. - test.generated_size.x) < .001
    assert abs(max(p.y for p in positions) - min(p.y for p in positions) + 40. - test.generated_size.y) < .001
    assert abs(max(p.z for p in positions) - min(p.z for p in positions) + 40. - test.generated_size.z) < .001

    test.set_editor_property('custom_pattern_rows', ['10', '1'])
    test.rebuild_shape()
    assert test.generation_warning and test.generated_cube_count == 0 and test.solid_cubes.get_instance_count() == 0
    test.set_editor_property('custom_pattern_rows', ['00', '00'])
    test.rebuild_shape()
    assert test.generation_warning and test.generated_cube_count == 0
    test.set_editor_property('custom_pattern_rows', ['1'])
    test.set_editor_property('height_cubes', 128)
    test.configure_shape(unreal.VoxelShapePreset.CUSTOM, 128, 32)
    assert test.generation_warning and test.generated_cube_count == 0

    # Under the cube budget, but over the separated-edge budget: reject atomically.
    test.set_editor_property('height_cubes', 32)
    test.set_editor_property('appearance', unreal.VoxelShapeAppearance.NEON_WIRE_CUBES)
    test.configure_shape(unreal.VoxelShapePreset.CUSTOM, 32, 16)
    assert test.generation_warning and test.wire_edges.get_instance_count() == 0
    test.set_editor_property('cube_size', float('nan'))
    test.set_editor_property('cube_gap', -4.)
    test.set_editor_property('height_cubes', -9)
    test.configure_shape(unreal.VoxelShapePreset.HEART, -1, -2)
    assert math.isfinite(test.cube_size) and test.cube_size >= 1 and test.cube_gap == 0
    test.set_editor_property('preserve_aspect_ratio', True)
    test.configure_shape(unreal.VoxelShapePreset.HEART, 15, 2)
    assert test.generated_cube_count == original and not test.generation_warning
    before = (test.generated_cube_count, test.generated_edge_count)
    for _ in range(3):
        test.rebuild_shape()
        assert (test.generated_cube_count, test.generated_edge_count) == before
    results.update(depth_layers=True, solid_cube_dimensions=True, shared_edges=True, gaps=True,
                   independent_height=True, bottom_pivot=True, invalid_pattern_rejected=True,
                   allocation_limits=True, invalid_numbers_sanitized=True, rebuild_idempotent=True)
finally:
    actors.destroy_actor(test)

(output / 'Verification.json').write_text(json.dumps(results, indent=2))
unreal.log('VOXEL_SHAPE_VERIFIED ' + json.dumps(results))
