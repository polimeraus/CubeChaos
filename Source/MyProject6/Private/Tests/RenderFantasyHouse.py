"""Render saved Blueprint presets in an unsaved scene using a real RHI."""
from pathlib import Path
import json
import time
import unreal

output = Path(unreal.Paths.project_saved_dir()) / 'FantasyHouse/Preview'
output.mkdir(parents=True, exist_ok=True)
world = unreal.EditorLoadingAndSavingUtils.load_map('/Game/Structures/FantasyHouse/Lvl_FantasyHousePreview')
assert world
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
bp = unreal.load_asset('/Game/Structures/FantasyHouse/BP_ParametricFantasyHouse')
houses = [a for a in actors.get_all_level_actors() if isinstance(a,unreal.ParametricFantasyHouse)]
assert len(houses)==1
house = houses[0]
saved_triangles = house.house_mesh.get_dynamic_mesh().get_triangle_count()
assert saved_triangles == 8502
house.rebuild_house()
assert house.triangle_count == saved_triangles
assert house.stair_step_count == 28
fresh = actors.spawn_actor_from_class(bp.generated_class(),unreal.Vector(5000,0,0))
assert fresh.triangle_count == saved_triangles
for group,names in {'body':('radius','floors','floor_height','top_radius_scale','wall_thickness','floor_bands'),
                    'roof':('shape','height','overhang','curvature','profile_steps','tip_offset','ribs','rib_count'),
                    'stairs':('enabled','auto_height','width','inner_radius','max_step_height','steps_per_turn')}.items():
    for name in names:
        a = fresh.get_editor_property(group).get_editor_property(name)
        b = house.get_editor_property(group).get_editor_property(name)
        assert a == b, (group,name,str(a),str(b))
actors.destroy_actor(fresh)
(output/'Persistence.json').write_text(json.dumps({'fresh_process_load':True,'saved_triangles':saved_triangles,
    'new_instance_matches':True,'building_actor_count':1},indent=2))
# Remove the neutral stage from captures so the floating foundation is visible too.
for actor in actors.get_all_level_actors():
    if actor.get_actor_label() == 'Preview_Ground': actors.destroy_actor(actor)

camera = actors.spawn_actor_from_class(unreal.SceneCapture2D, unreal.Vector(1750,-2550,1600))
capture = camera.get_component_by_class(unreal.SceneCaptureComponent2D)
capture.set_editor_property('capture_every_frame', False)
capture.set_editor_property('capture_on_movement', False)
capture.set_editor_property('always_persist_rendering_state', True)
capture.set_editor_property('capture_source', unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
capture.set_editor_property('fov_angle', 42.)
post = capture.get_editor_property('post_process_settings')
for name, value in {'override_auto_exposure_method': True, 'auto_exposure_method': unreal.AutoExposureMethod.AEM_MANUAL,
                    'override_auto_exposure_apply_physical_camera_exposure': True,
                    'auto_exposure_apply_physical_camera_exposure': False,
                    'override_auto_exposure_bias': True, 'auto_exposure_bias': .25,
                    'override_bloom_intensity': True, 'bloom_intensity': .25}.items():
    post.set_editor_property(name, value)
capture.set_editor_property('post_process_settings', post)
target = unreal.RenderingLibrary.create_render_target2d(world, 1200, 1400, unreal.TextureRenderTargetFormat.RTF_RGBA8)
capture.set_editor_property('texture_target', target)
views = [('WizardHouse', unreal.FantasyHousePreset.WIZARD_HOUSE),
         ('MoonObservatory', unreal.FantasyHousePreset.MOON_OBSERVATORY),
         ('FloatingPalace', unreal.FantasyHousePreset.FLOATING_PALACE),
         ('DoorCloseup', unreal.FantasyHousePreset.WIZARD_HOUSE),
         ('StairsTop', unreal.FantasyHousePreset.WIZARD_HOUSE),
         ('StairsInterior', unreal.FantasyHousePreset.WIZARD_HOUSE)]
state = {'index': 0, 'next': time.monotonic()+15, 'captures': 0, 'export': False, 'metrics': {}, 'runtime':False}

def tick(delta):
    if time.monotonic() < state['next']: return
    try:
        if state['runtime']:
            game = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
            if not game:
                if time.monotonic() > state['runtime_deadline']: raise RuntimeError('PIE world did not start')
                return
            placed = unreal.GameplayStatics.get_all_actors_of_class(game,unreal.ParametricFantasyHouse)
            assert len(placed)==1 and placed[0].house_mesh.get_dynamic_mesh().get_triangle_count()==8502
            # Summon uses UWorld::SpawnActor in the live game world. The internal
            # deferred-spawn K2 helpers are intentionally not exported to Python.
            unreal.SystemLibrary.execute_console_command(game,'EnableCheats')
            unreal.SystemLibrary.execute_console_command(game,'Summon ' + bp.generated_class().get_path_name())
            spawned_actors = unreal.GameplayStatics.get_all_actors_of_class(game,unreal.ParametricFantasyHouse)
            spawned = next(a for a in spawned_actors if a != placed[0])
            assert spawned.house_mesh.get_dynamic_mesh().get_triangle_count()==8502
            assert spawned.stair_step_count == 28
            assert not spawned.is_actor_tick_enabled()
            assert isinstance(spawned.house_mesh.get_material(0),unreal.MaterialInstanceDynamic)
            (output/'Runtime.json').write_text(json.dumps({'loaded_actor':True,'runtime_spawn':True,
                'tick_enabled':False,'triangles':8502,'stairs':True},indent=2))
            unreal.log('FANTASY_HOUSE_RENDER_AND_RUNTIME_OK')
            unreal.unregister_slate_post_tick_callback(state['handle'])
            unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
            unreal.EditorPythonScripting.set_keep_python_script_alive(False)
            return
        if state['captures']:
            capture.capture_scene()
            state['captures'] -= 1
            state.update(export=state['captures']==0, next=time.monotonic()+.5)
            return
        if state['export']:
            name = views[state['index']][0]
            unreal.RenderingLibrary.export_render_target(world,target,str(output),name+'.png')
            state['metrics'][name] = {'triangles': house.triangle_count, 'build_ms': house.last_build_milliseconds}
            state['index'] += 1
            state['export'] = False
            if state['index'] == len(views):
                (output/'Metrics.json').write_text(json.dumps(state['metrics'],indent=2))
                house.apply_preset()
                state.update(runtime=True,next=time.monotonic()+4,runtime_deadline=time.monotonic()+45)
                unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_play_simulate()
                return
        name,preset = views[state['index']]
        house.set_editor_property('preset',preset)
        house.apply_preset()
        if name == 'StairsTop':
            # Remove only the main roof / apex for an explanatory top view; never save it.
            mesh = house.house_mesh.get_dynamic_mesh()
            remove = []
            for tid in range(mesh.get_num_triangle_i_ds()):
                valid,a,b,c = mesh.get_triangle_positions(tid)
                if valid and min(a.z,b.z,c.z)>=735: remove.append(tid)
            for tid in remove[:-1]: mesh.delete_triangle_from_mesh(tid,True)
            if remove: mesh.delete_triangle_from_mesh(remove[-1],False)
            location,look = unreal.Vector(80,-100,1100),unreal.Vector(0,0,240)
        elif name == 'StairsInterior':
            location,look = unreal.Vector(0,-210,140),unreal.Vector(0,0,285)
            capture.set_editor_property('fov_angle',85.)
        elif name == 'DoorCloseup':
            location, look = unreal.Vector(400,-1050,390), unreal.Vector(0,-200,235)
        else:
            origin, extent = house.get_actor_bounds(False)
            distance = extent.z*3.8
            look = origin
            location = origin + unreal.Vector(distance*.45,-distance,distance*.38)
        camera.set_actor_location(location,False,False)
        camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(location,look),False)
        state.update(captures=4,next=time.monotonic()+2.)
    except Exception:
        unreal.unregister_slate_post_tick_callback(state['handle'])
        unreal.EditorPythonScripting.set_keep_python_script_alive(False)
        raise

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state['handle'] = unreal.register_slate_post_tick_callback(tick)
