"""Fresh-process verification of both saved scenes and new class instances."""
import json
from pathlib import Path
import unreal

out = Path(unreal.Paths.project_saved_dir()) / 'FantasyHouse'
saved = json.loads((out/'OpenWindowsStairsSaved.json').read_text())
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
results = []
for record in saved['actors']:
    assert unreal.EditorLoadingAndSavingUtils.load_map(record['level'])
    house = next(a for a in actors.get_all_level_actors() if a.get_actor_label()==record['actor'])
    assert isinstance(house,unreal.ParametricFantasyHouse)
    assert house.house_mesh.get_dynamic_mesh().get_triangle_count()==record['triangles']
    assert house.get_editor_property('stairs').get_editor_property('enabled')
    house.rebuild_house()
    assert house.triangle_count==record['triangles']
    assert house.stair_step_count==record['steps']
    assert abs(house.generated_stair_height-record['stair_height'])<.01
    mesh = house.house_mesh.get_dynamic_mesh()
    used = set()
    tread_triangles = 0
    for tid in range(mesh.get_num_triangle_i_ds()):
        material,valid = mesh.get_triangle_material_id(tid)
        if valid: used.add(material)
        if valid and material==6:
            tread_triangles += 1
            _,a,b,c = mesh.get_triangle_positions(tid)
            # A rail would rise above the last tread; a center column would contain
            # axis vertices. The staircase must contain only closed tread prisms.
            for p in (a,b,c):
                assert 26-.01 <= p.z <= 26+house.generated_stair_height+.01
                assert p.x*p.x+p.y*p.y >= 25-.01
    assert 6 in used and 3 not in used
    assert tread_triangles==12*house.stair_step_count
    assert mesh.get_is_closed_mesh()
    results.append({'level':record['level'],'actor':record['actor'],'saved_mesh_matches_rebuild':True,
                    'triangles':house.triangle_count,'steps':house.stair_step_count})

for cls in (unreal.ParametricFantasyHouse,
            unreal.load_asset('/Game/Structures/FantasyHouse/BP_ParametricFantasyHouse').generated_class()):
    cdo = unreal.get_default_object(cls)
    assert cdo.get_editor_property('stairs').get_editor_property('enabled')
    fresh = actors.spawn_actor_from_class(cls,unreal.Vector(25000,0,0))
    assert fresh.stair_step_count==28 and fresh.triangle_count==8502
    stairs = fresh.get_editor_property('stairs')
    stairs.set_editor_property('enabled',False)
    fresh.set_editor_property('stairs',stairs)
    fresh.rebuild_house()
    assert fresh.stair_step_count==0 and fresh.triangle_count==8166
    actors.destroy_actor(fresh)
(out/'OpenWindowsStairsPersistence.json').write_text(json.dumps({'passed':True,'actors':results,
    'native_and_blueprint_defaults_verified':True,'new_instances_verified':True},indent=2))
unreal.log('FANTASY_HOUSE_OPEN_WINDOWS_STAIRS_PERSISTENCE_OK')
