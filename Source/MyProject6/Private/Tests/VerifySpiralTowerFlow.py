"""Fresh-process persistence and material parameter verification after migration."""
import json
from pathlib import Path
import unreal

out = Path(unreal.Paths.project_saved_dir()).resolve() / 'SpiralTower' / globals().get('REPORT_FOLDER', 'VerticalStrip')
manifest = json.loads((out / 'SavedInstances.json').read_text())
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
cls = unreal.ChunkedDestructibleSpiralTower
material = unreal.load_asset('/Game/Structures/Destructible/SpiralTower/Materials/M_SpiralTowerFlowGlow')
parameters = {str(p) for p in unreal.MaterialEditingLibrary.get_scalar_parameter_names(material)}
assert {'PathLength', 'TrailLength', 'BandHeight'} <= parameters, parameters
assert not {'TravelHeight', 'BaseHeight'} & parameters, parameters
expressions = unreal.MaterialEditingLibrary.get_material_expressions(material)
shader = next(e for e in expressions if isinstance(e, unreal.MaterialExpressionCustom))
code = shader.get_editor_property('code')
# Geometry tests verify that a wall column has one path coordinate. This guards
# against reintroducing a height-dependent brightness or timing into the shader.
assert 'Path.x' in code and 'Path.y' not in code and 'Position.z' not in code
assert any(isinstance(e, unreal.MaterialExpressionTextureCoordinate) and
           e.get_editor_property('coordinate_index') == 1 for e in expressions)
rows = []


def verify(tower):
    assert tower.generated_glow_path_length > tower.generated_height * 3
    assert not tower.generation_warning
    chunks = tower.get_components_by_class(unreal.DynamicMeshComponent)
    assert chunks
    mid = chunks[0].get_material(2)
    assert isinstance(mid, unreal.MaterialInstanceDynamic)
    assert abs(mid.get_scalar_parameter_value('PathLength') - tower.generated_glow_path_length) < .1
    assert abs(mid.get_scalar_parameter_value('TrailLength') - tower.glow_trail_length) < .1
    assert tower.ground_launch_pad.get_editor_property('child_actor')
    return dict(pieces=tower.generated_piece_count, path_length=tower.generated_glow_path_length,
                trail=tower.glow_trail_length, material=mid.get_editor_property('parent').get_path_name())


for path in sorted({row['map'] for row in manifest['instances']}):
    assert unreal.EditorLoadingAndSavingUtils.load_map(path)
    for row in (r for r in manifest['instances'] if r['map'] == path):
        tower = next(a for a in actors.get_all_level_actors() if isinstance(a, cls) and a.get_actor_label() == row['label'])
        p, r, s = tower.get_actor_location(), tower.get_actor_rotation(), tower.get_actor_scale3d()
        assert [p.x, p.y, p.z, r.pitch, r.yaw, r.roll, s.x, s.y, s.z] == row['transform']
        assert str(tower.get_folder_path()) == row['folder']
        assert {key: tower.get_editor_property(key) for key in row['settings']} == row['settings']
        assert tower.glow_trail_length == row['trail']
        assert abs(tower.generated_glow_path_length - row['path_length']) < .1
        rows.append(dict(map=path, label=row['label'], **verify(tower)))

for tower_class in [cls] + [unreal.load_asset(p).generated_class() for p in manifest['blueprints']]:
    cdo = unreal.get_default_object(tower_class)
    assert cdo.glow_trail_length == 600 and cdo.glow_material == material
    tower = actors.spawn_actor_from_class(tower_class, unreal.Vector(20000, 0, 0), transient=True)
    rows.append(dict(new_class=tower.get_class().get_path_name(), **verify(tower)))
    actors.destroy_actor(tower)
(out / 'Persistence.json').write_text(json.dumps(dict(passed=True, actors=rows), indent=2))
unreal.log('SPIRAL_STAIR_FLOW_PERSISTENCE_OK')
