"""Geometry / parameter regression checks in a disposable world. No level is saved."""
import hashlib
import json
import math
import struct
from pathlib import Path
import unreal

out = Path(unreal.Paths.project_saved_dir()) / 'FantasyHouse'
out.mkdir(parents=True, exist_ok=True)
unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
bp = unreal.load_asset('/Game/Structures/DynamicMesh/FantasyHouse/BP_ParametricFantasyHouse')
assert bp and unreal.get_default_object(bp.generated_class()).get_editor_property('base_material')
house = actors.spawn_actor_from_class(bp.generated_class(), unreal.Vector())
results = {}

def change(group, **values):
    settings = house.get_editor_property(group)
    for name, value in values.items(): settings.set_editor_property(name, value)
    house.set_editor_property(group, settings)
    house.rebuild_house()

def fingerprint():
    mesh = house.house_mesh.get_dynamic_mesh()
    sha = hashlib.sha256()
    for i in range(mesh.get_num_vertex_i_ds()):
        position, valid = mesh.get_vertex_position(i)
        if valid: sha.update(struct.pack('<ddd', position.x, position.y, position.z))
    sha.update(struct.pack('<i', mesh.get_triangle_count()))
    return sha.hexdigest()

def check(name):
    mesh = house.house_mesh.get_dynamic_mesh()
    count = mesh.get_triangle_count()
    assert 100 < count < 150000, (name, count)
    assert len(house.get_components_by_class(unreal.DynamicMeshComponent)) == 1
    assert not house.get_components_by_class(unreal.ChildActorComponent)
    assert mesh.get_is_closed_mesh(), (name, mesh.get_num_open_border_edges())
    area, volume = mesh.get_mesh_volume_area()
    assert math.isfinite(area) and math.isfinite(volume) and area > 0 and volume > 0
    assert house.house_mesh.get_num_materials() == 7
    results[name] = {'triangles': count, 'build_ms': house.last_build_milliseconds,
                     'area': area, 'closed': True}
    return count

def materials():
    mesh = house.house_mesh.get_dynamic_mesh()
    counts = {}
    for tid in range(mesh.get_num_triangle_i_ds()):
        material, valid = mesh.get_triangle_material_id(tid)
        if valid: counts[material] = counts.get(material,0)+1
    return counts

def ray(origin,direction,max_distance):
    mesh = house.house_mesh.get_dynamic_mesh()
    _, bvh = mesh.build_bvh_for_mesh()
    _, hit, outcome = mesh.find_nearest_ray_intersection_with_mesh(bvh,origin,direction,
        unreal.GeometryScriptSpatialQueryOptions(max_distance=max_distance))
    return hit

for preset in (unreal.FantasyHousePreset.WIZARD_HOUSE, unreal.FantasyHousePreset.MOON_OBSERVATORY,
               unreal.FantasyHousePreset.FLOATING_PALACE):
    house.set_editor_property('preset', preset)
    house.apply_preset()
    check(str(preset))
    before = fingerprint()
    house.rebuild_house()
    assert before == fingerprint(), 'Non-deterministic rebuild'

house.set_editor_property('preset', unreal.FantasyHousePreset.WIZARD_HOUSE)
house.apply_preset()
default_count = check('default')
for shape in (unreal.FantasyRoofShape.SWEPT_SPIRE, unreal.FantasyRoofShape.DOME,
              unreal.FantasyRoofShape.ONION, unreal.FantasyRoofShape.CONE):
    change('roof', shape=shape)
    check('roof_' + str(shape))
for shape in (unreal.FantasyApexShape.NONE, unreal.FantasyApexShape.STAR,
              unreal.FantasyApexShape.ORB, unreal.FantasyApexShape.SPEAR):
    change('apex', shape=shape)
    check('apex_' + str(shape))
for shape in (unreal.FantasyOpeningShape.ROUND_ARCH, unreal.FantasyOpeningShape.POINTED_ARCH,
              unreal.FantasyOpeningShape.RECTANGLE):
    change('windows', shape=shape)
    change('door', shape=shape, open=True)
    check('openings_' + str(shape))

house.apply_preset()
before = fingerprint()
house.new_variation()
assert before != fingerprint(), 'Seed does not change turret geometry'
house.apply_preset()
change('windows', enabled=False)
assert check('no_windows') < default_count
change('balcony', enabled=False)
change('turrets', count=0)
assert check('simple') < default_count
change('body', floors=1, radius=120., floor_height=180., top_radius_scale=.65)
change('windows', enabled=True, per_floor=16, width=300., height=400., sill_height=250.)
change('door', width=250., height=400., open=True)
check('crowded_small_body')
house.apply_preset()
house.set_editor_property('radial_segments', 8)
change('roof', profile_steps=4, tip_offset=unreal.Vector2D(160, -140))
change('balcony', arc_degrees=360., posts=40)
check('coarse_full_balcony')
house.set_editor_property('enable_collision', False)
house.rebuild_house()
assert house.house_mesh.get_collision_enabled() == unreal.CollisionEnabled.NO_COLLISION
house.set_editor_property('enable_collision', True)
house.rebuild_house()
assert house.house_mesh.get_collision_enabled() == unreal.CollisionEnabled.QUERY_AND_PHYSICS
house.apply_preset()
house.set_editor_property('flat_shading', False)
house.rebuild_house()
check('smooth_normals')
house.apply_preset()

# Openings remain empty all the way through the wall, with no legacy glazing.
assert 3 not in materials()
assert not ray(unreal.Vector(0,-350,400),unreal.Vector(0,1,0),130).hit
change('windows',enabled=False)
assert ray(unreal.Vector(0,-350,400),unreal.Vector(0,1,0),130).hit
change('windows',enabled=True)
assert not ray(unreal.Vector(0,-350,400),unreal.Vector(0,1,0),130).hit

# Toggle removes all treads together; tread elevations really ascend.
with_stairs = check('stairs_enabled')
assert house.stair_step_count == 28 and abs(house.generated_stair_height-500)<.01
assert materials().get(6,0)==house.stair_step_count*12
rise = house.generated_stair_height/house.stair_step_count
for step in (0, house.stair_step_count//2, house.stair_step_count-1):
    angle = math.radians(-90+(step+.5)*15)
    hit = ray(unreal.Vector(math.cos(angle)*95,math.sin(angle)*95,26+(step+1)*rise+2),
              unreal.Vector(0,0,-1),3)
    assert hit.hit and abs(hit.hit_position.z-(26+(step+1)*rise))<.01, (step,str(hit))
change('stairs',enabled=False)
assert check('stairs_disabled') < with_stairs
assert house.stair_step_count == 0 and 6 not in materials()
change('stairs',enabled=True,auto_height=False,height=350.,clockwise=True,start_angle=40.,
       width=100.,inner_radius=20.,max_step_height=12.,steps_per_turn=32)
check('stairs_custom_clockwise')
assert house.stair_step_count == 30 and abs(house.generated_stair_height-350)<.01
house.apply_preset()

# Full default mesh audit catches malformed roofs, zero-area caps, and boolean failures.
mesh = house.house_mesh.get_dynamic_mesh()
degenerate = 0
for tid in range(mesh.get_num_triangle_i_ds()):
    valid, a, b, c = mesh.get_triangle_positions(tid)
    if not valid: continue
    assert all(math.isfinite(v) for p in (a,b,c) for v in (p.x,p.y,p.z))
    ab, ac = b-a, c-a
    area2 = ((ab.y*ac.z-ab.z*ac.y)**2 + (ab.z*ac.x-ab.x*ac.z)**2 + (ab.x*ac.y-ab.y*ac.x)**2)
    if area2 < 1.e-12: degenerate += 1
assert degenerate == 0, ('Degenerate triangles', degenerate)
results['default_fingerprint'] = fingerprint()
results['passed'] = True
(out / 'Verification.json').write_text(json.dumps(results, indent=2))
actors.destroy_actor(house)
unreal.log('FANTASY_HOUSE_TESTS_OK ' + json.dumps(results))
