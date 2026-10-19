"""Fresh-process persistence check, including native/BP spawns and saved level identities."""
import json
from pathlib import Path
import unreal

out = Path(unreal.Paths.project_saved_dir()) / 'TowerFeatures'
manifest = json.loads((out / 'SavedInstances.json').read_text())
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
classes = dict(Babylon=unreal.ChunkedDestructibleBabylonTower, Spiral=unreal.ChunkedDestructibleSpiralTower)


def pose(actor):
    p, r, s = actor.get_actor_location(), actor.get_actor_rotation(), actor.get_actor_scale3d()
    return [p.x, p.y, p.z, r.pitch, r.yaw, r.roll, s.x, s.y, s.z]


def check(actor, kind):
    native = unreal.get_default_object(classes[kind])
    for key in manifest['keys'][kind]:
        actual, expected = actor.get_editor_property(key), native.get_editor_property(key)
        if key == 'traversal':
            assert actual.export_text() == expected.export_text(), (actor.get_name(), key, actual.export_text(), expected.export_text())
        else:
            assert actual == expected, (actor.get_name(), key, str(actual), str(expected))
    if kind == 'Babylon':
        assert actor.scale_windows_with_tiers and actor.window_height_ratio == .5625
    else:
        assert not actor.foundation and actor.flowing_glow and actor.traversal.ground_launch_pad
        assert actor.glow_material and actor.glow_material.get_name() == 'M_SpiralTowerFlowGlow'


for row in manifest['blueprints']:
    bp = unreal.load_asset(row['path'])
    check(unreal.get_default_object(bp.generated_class()), row['kind'])

verified = []
for path in sorted({row['map'] for row in manifest['instances']}):
    assert unreal.EditorLoadingAndSavingUtils.load_map(path)
    for row in [r for r in manifest['instances'] if r['map'] == path]:
        actor = next(a for a in actors.get_all_level_actors()
                     if a.get_actor_label() == row['label'] and a.get_class().get_path_name() == row['cls'])
        check(actor, row['kind'])
        assert pose(actor) == row['transform'] and str(actor.get_folder_path()) == row['folder']
        assert actor.generated_piece_count == row['pieces'] and actor.generated_height == row['height']
        assert not actor.generation_warning
        for cls in (actor.get_class(), classes[row['kind']]):
            fresh = actors.spawn_actor_from_class(cls, unreal.Vector(25000, 0, 0), transient=True)
            check(fresh, row['kind'])
            assert fresh.generated_piece_count > 0 and not fresh.generation_warning
            if row['kind'] == 'Spiral':
                pad = fresh.ground_launch_pad.get_editor_property('child_actor')
                assert pad and pad.use_target_height and pad.target_rise_height > fresh.generated_height
                meshes = fresh.get_components_by_class(unreal.DynamicMeshComponent)
                assert all(c.get_num_materials() == 3 for c in meshes)
                assert all(isinstance(c.get_material(2), unreal.MaterialInstanceDynamic) for c in meshes)
                assert sum(c.get_dynamic_mesh().get_triangle_count() for c in meshes) == fresh.generated_piece_count * 24
            actors.destroy_actor(fresh)
        verified.append(dict(map=path, label=row['label'], kind=row['kind']))

(out / 'Persistence.json').write_text(json.dumps(dict(fresh_process=True, native_and_blueprint_spawns=True,
    class_defaults=True, preserved_level_identity=True, instances=verified), indent=2))
unreal.log('TOWER_FEATURES_PERSISTENCE_OK ' + json.dumps(verified))
