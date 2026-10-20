"""Fresh-process asset persistence, runtime spawn, and before/after image checks."""
import json
import math
import time
from pathlib import Path
import unreal

folder = '/Game/Structures/Destructible/RadialSculpture'
output = Path(unreal.Paths.project_saved_dir()) / 'RadialSculpture'
output.mkdir(parents=True, exist_ok=True)
before_path = output / 'BeforeHollowSphere.json'
before = json.loads(before_path.read_text()) if before_path.exists() else {'maps': {}}
upgraded = json.loads((output/'HollowSphereUpgrade.json').read_text())
actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for map_path, expected_actors in before['maps'].items():
    assert unreal.EditorLoadingAndSavingUtils.load_map(map_path)
    found = [a for a in actor_subsystem.get_all_level_actors() if isinstance(a, unreal.DestructibleRadialSculpture)]
    assert len(found) == len(expected_actors)
    for a in found:
        expected = next(r for r in expected_actors if r['name'] == a.get_name())
        t = a.get_actor_transform()
        assert [t.translation.x,t.translation.y,t.translation.z] == expected['location']
        assert [t.rotation.x,t.rotation.y,t.rotation.z,t.rotation.w] == expected['rotation']
        assert [t.scale3d.x,t.scale3d.y,t.scale3d.z] == expected['scale']
        assert a.get_actor_label() == expected['label'] and str(a.get_folder_path()) == expected['folder']
        assert a.get_editor_property('sculpture_material').get_path_name() == expected['material']
        for key, value in expected['shape'].items(): assert a.get_editor_property(key) == value, key
        saved = next(r for r in upgraded['maps'][map_path] if r['name'] == a.get_name())
        assert a.piece_size == saved['piece_size_cm'] and a.generated_piece_count == saved['pieces'] and not a.generation_warning
        assert a.sphere_wall_thickness == 15 and a.get_sphere_inner_radius() == 100
        assert (a.get_sphere_world_center() - unreal.MathLibrary.transform_location(t,a.get_sphere_local_center())).length() < .01
        assert a.get_sphere_interior_world_radius() > 0
        assert a.try_get_sphere_spawn_transform(20.) is not None
        assert not any(c.is_simulating_physics() for c in a.get_components_by_class(unreal.DynamicMeshComponent))
world = unreal.EditorLoadingAndSavingUtils.load_map(folder + '/Maps/Lvl_RadialSculpturePreview')
assert world
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
bp = unreal.load_asset(folder + '/BP_DestructibleRadialSculpture')
defaults = unreal.get_default_object(bp.generated_class())
sculptures = [a for a in actors.get_all_level_actors() if isinstance(a, unreal.DestructibleRadialSculpture)]
assert len(sculptures) == 1
sculpture = sculptures[0]
# Transient geometry must regenerate after a saved level is loaded in a new process.
count = sculpture.generated_piece_count
assert count > 1000 and sculpture.remaining_piece_count == count
assert not sculpture.generation_warning
fresh = actors.spawn_actor_from_class(bp.generated_class(), unreal.Vector(5000, 0, 0))
keys = ('fin_count', 'outer_radius', 'fin_height', 'fin_thickness', 'sphere_radius', 'neck_radius',
        'curve_exponent', 'include_base', 'base_margin', 'base_thickness', 'sculpture_material',
        'piece_size', 'piece_health', 'use_destruction_ripple', 'flash_duration', 'sphere_wall_thickness')
for key in keys:
    assert sculpture.get_editor_property(key) == defaults.get_editor_property(key) == fresh.get_editor_property(key), key
assert fresh.generated_piece_count == count
assert not fresh.is_actor_tick_enabled()
assert len(fresh.get_components_by_class(unreal.DynamicMeshComponent)) < 100
assert not any(c.is_simulating_physics() for c in fresh.get_components_by_class(unreal.DynamicMeshComponent))
actors.destroy_actor(fresh)
results = {'fresh_process_persistence': True, 'blueprint_defaults_inherited': True, 'pieces': count,
           'piece_size_cm': defaults.piece_size, 'wall_thickness_cm': defaults.sphere_wall_thickness,
           'inner_radius_cm': sculpture.get_sphere_inner_radius(), 'interior_spawn_query': True, 'shared_small_cell_system': True}

camera = actors.spawn_actor_from_class(unreal.SceneCapture2D, unreal.Vector(1100, -1450, 1100))
camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(camera.get_actor_location(), unreal.Vector(0,0,290)), False)
capture = camera.get_component_by_class(unreal.SceneCaptureComponent2D)
for key, value in {
    'capture_every_frame': False, 'capture_on_movement': False, 'always_persist_rendering_state': True,
    'fov_angle': 48., 'capture_source': unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR
}.items():
    capture.set_editor_property(key, value)
target = unreal.RenderingLibrary.create_render_target2d(world, 1200, 1000, unreal.TextureRenderTargetFormat.RTF_RGBA8)
capture.set_editor_property('texture_target', target)
state = {'next': time.monotonic() + 12, 'stage': 0, 'captures': 0, 'export': False}

def stop():
    unreal.unregister_slate_post_tick_callback(state['handle'])
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)

def tick(delta):
    if time.monotonic() < state['next']:
        return
    try:
        if state['stage'] == 3:
            game = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
            if not game:
                assert time.monotonic() < state['deadline'], 'PIE failed to start'
                return
            runtime = unreal.GameplayStatics.get_all_actors_of_class(game, unreal.DestructibleRadialSculpture)
            assert len(runtime) == 1 and runtime[0].generated_piece_count == count
            assert runtime[0].try_get_sphere_spawn_transform(25.) is not None
            runtime[0].shatter_sculpture(unreal.Vector(0,0,1))
            assert runtime[0].remaining_piece_count == 0
            runtime[0].rebuild_sculpture()
            assert runtime[0].remaining_piece_count == count
            unreal.SystemLibrary.execute_console_command(game, 'EnableCheats')
            unreal.SystemLibrary.execute_console_command(game, 'Summon ' + bp.generated_class().get_path_name())
            spawned = unreal.GameplayStatics.get_all_actors_of_class(game, unreal.DestructibleRadialSculpture)
            assert len(spawned) == 2
            dynamic = next(a for a in spawned if a != runtime[0])
            assert dynamic.generated_piece_count == dynamic.remaining_piece_count == count
            for key in keys:
                assert dynamic.get_editor_property(key) == defaults.get_editor_property(key), ('runtime', key)
            dynamic.shatter_sculpture(unreal.Vector(0,0,1))
            assert dynamic.remaining_piece_count == 0
            dynamic.rebuild_sculpture()
            assert dynamic.remaining_piece_count == count
            results.update(runtime_spawn=True, runtime_shatter_and_rebuild=True)
            unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
            (output / 'Verification.json').write_text(json.dumps(results, indent=2))
            unreal.log('RADIAL_SCULPTURE_VERIFY_OK ' + json.dumps(results))
            stop()
            return
        if state['captures']:
            capture.capture_scene()
            state['captures'] -= 1
            state.update(export=state['captures'] == 0, next=time.monotonic() + .35)
            return
        if state['export']:
            name = ['Intact.png', 'Damaged.png', 'InteriorCutaway.png'][state['stage']]
            unreal.RenderingLibrary.export_render_target(world, target, str(output), name)
            state['stage'] += 1
            state['export'] = False
            if state['stage'] == 3:
                # The damage demonstration is deliberately unsaved.
                sculpture.set_editor_property('flash_duration', defaults.get_editor_property('flash_duration'))
                sculpture.rebuild_sculpture()
                actors.destroy_actor(camera)
                actors.destroy_actor(state['contents'])
                actors.destroy_actor(state['interior_light'])
                unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_play_simulate()
                state.update(next=time.monotonic() + 3, deadline=time.monotonic() + 60)
                return
        if state['stage'] == 1:
            # Editor worlds do not advance the gameplay flash cleanup timer.
            sculpture.set_editor_property('flash_duration', 0.)
            # Three isolated impacts show small holes, not whole missing fin bands/octants.
            sphere = sculpture.get_sphere_local_center()
            for point in [unreal.Vector(180/math.sqrt(2),-180/math.sqrt(2),210),
                          unreal.Vector(280/math.sqrt(2),-280/math.sqrt(2),60),
                          sphere + unreal.Vector(76,-76,35)]:
                cell = sculpture.world_location_to_cell(point)
                assert cell is not None and sculpture.destroy_cell(cell), point
            assert sculpture.remaining_piece_count == count-3
            assert not any(c.is_simulating_physics() for c in sculpture.get_components_by_class(unreal.DynamicMeshComponent))
            results['isolated_hits_removed_cells'] = 3
            results['preview_remaining_pieces'] = sculpture.remaining_piece_count
        if state['stage'] == 2:
            sculpture.rebuild_sculpture()
            sphere = sculpture.get_sphere_world_center()
            spawn = sculpture.try_get_sphere_spawn_transform(25.)
            assert spawn is not None
            marker = actors.spawn_actor_from_class(unreal.StaticMeshActor, spawn.translation, spawn.rotation.rotator())
            marker.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
            marker.set_actor_scale3d(unreal.Vector(.25,.25,.25))
            marker.static_mesh_component.set_collision_profile_name('BlockAll')
            surface = marker.static_mesh_component.create_dynamic_material_instance(0, unreal.load_asset('/Game/LevelPrototyping/Materials/M_FlatCol'))
            surface.set_vector_parameter_value('Base Color', unreal.LinearColor(.025,.45,1.,1.))
            assert sculpture.try_get_sphere_spawn_transform(25.) is None
            state['contents'] = marker
            view = unreal.Vector(330,-440,250)
            direction = view / view.length()
            camera.set_actor_location_and_rotation(sphere+view,unreal.MathLibrary.find_look_at_rotation(sphere+view,sphere),False,False)
            capture.set_editor_property('fov_angle',42.)
            # Unsaved cutaway only: remove the front part of the shell to reveal real wall thickness.
            removed = 0
            for latitude in range(-9,10):
                phi = math.pi*latitude/20
                for longitude in range(40):
                    theta = 2*math.pi*longitude/40
                    d = unreal.Vector(math.cos(phi)*math.cos(theta),math.cos(phi)*math.sin(theta),math.sin(phi))
                    if d.dot(direction) <= .2: continue
                    cell = sculpture.world_location_to_cell(sphere+d*(sculpture.sphere_radius-sculpture.sphere_wall_thickness*.5))
                    if cell is not None and sculpture.destroy_cell(cell): removed += 1
            assert removed > 30
            light = actors.spawn_actor_from_class(unreal.PointLight,sphere+unreal.Vector(0,-20,30))
            light.point_light_component.set_intensity_units(unreal.LightUnits.LUMENS)
            light.point_light_component.set_intensity(100.)
            light.point_light_component.set_cast_shadows(False)
            light.point_light_component.set_attenuation_radius(230.)
            state['interior_light'] = light
            results.update(interior_occupant_spawned=True, occupied_spawn_rejected=True, cutaway_cells_removed=removed)
        state.update(captures=3, next=time.monotonic() + .5)
    except Exception:
        stop()
        raise

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state['handle'] = unreal.register_slate_post_tick_callback(tick)
