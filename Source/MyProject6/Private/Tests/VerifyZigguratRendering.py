"""Fresh-process asset persistence, rendered damage, variation and PIE spawn checks. No saves."""
import json
import time
from pathlib import Path
import unreal

out = Path(unreal.Paths.project_saved_dir()) / 'Ziggurat/Preview'
out.mkdir(parents=True, exist_ok=True)
world = unreal.EditorLoadingAndSavingUtils.load_map('/Game/Structures/Ziggurat/Lvl_ZigguratPreview')
assert world
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
bp = unreal.load_asset('/Game/Structures/Ziggurat/BP_DestructibleZiggurat')
cdo = unreal.get_default_object(bp.generated_class())
placed = [a for a in actors.get_all_level_actors() if isinstance(a, unreal.ChunkedDestructibleZiggurat)]
assert len(placed) == 1
temple = placed[0]
for light in actors.get_all_level_actors():
    if isinstance(light,unreal.DirectionalLight) and light.get_actor_label().startswith('Preview_'):
        assert light.get_actor_forward_vector().z < 0, 'Preview sunlight must point down'

def triangles(actor):
    return sum(c.get_dynamic_mesh().get_triangle_count() for c in actor.get_components_by_class(unreal.DynamicMeshComponent))

fresh = actors.spawn_actor_from_class(bp.generated_class(), unreal.Vector(8000, 0, 0))
assert fresh.generated_piece_count == temple.generated_piece_count
assert triangles(fresh) == triangles(temple) == temple.generated_piece_count * 12
for name in ('base_width','base_depth','tier_count','tier_height','top_scale','tier_height_scale','piece_size',
             'wall_thickness','piece_gap','front_window_count','side_window_count','window_width','window_height',
             'window_sill_height','window_frames','reduce_windows_on_upper_tiers','frame_width','entrance',
             'entrance_width','entrance_height','upper_tier_doors','entrance_stairs','stair_count','stair_tread_depth',
             'foundation_height','balconies','balcony_overhang','terrace_thickness','railings','railing_height',
             'railing_post_spacing','corner_pillars','crown','crown_steps','crown_step_height',
             'wall_material','trim_material','terrace_material','railing_material','stair_material','crown_material'):
    assert temple.get_editor_property(name) == fresh.get_editor_property(name) == cdo.get_editor_property(name), name
for component in temple.get_components_by_class(unreal.DynamicMeshComponent):
    assert component.get_num_materials() == 6
    assert all(component.get_material(i) for i in range(6))
actors.destroy_actor(fresh)
results = {'fresh_process_persistence':True,'new_spawn_matches_defaults':True,
           'pieces':temple.generated_piece_count,'windows':temple.generated_window_count,
           'default_triangles':triangles(temple)}

camera = actors.spawn_actor_from_class(unreal.SceneCapture2D, unreal.Vector())
capture = camera.get_component_by_class(unreal.SceneCaptureComponent2D)
for name, value in {'capture_every_frame':False,'capture_on_movement':False,
                    'always_persist_rendering_state':True,'fov_angle':43.,
                    'capture_source':unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR}.items():
    capture.set_editor_property(name, value)
post = capture.get_editor_property('post_process_settings')
for name, value in {'override_auto_exposure_method':True,'auto_exposure_method':unreal.AutoExposureMethod.AEM_MANUAL,
                    'override_auto_exposure_apply_physical_camera_exposure':True,
                    'auto_exposure_apply_physical_camera_exposure':False,
                    'override_auto_exposure_bias':True,'auto_exposure_bias':.1,
                    'override_ambient_occlusion_intensity':True,'ambient_occlusion_intensity':.8}.items():
    post.set_editor_property(name,value)
capture.set_editor_property('post_process_settings',post)
target = unreal.RenderingLibrary.create_render_target2d(world,1400,1200,unreal.TextureRenderTargetFormat.RTF_RGBA8)
capture.set_editor_property('texture_target',target)
views = ['Ziggurat','Facade','Damage','Variant']
state = {'next':time.monotonic()+20,'index':0,'captures':0,'export':False,'runtime':False}

def stop():
    unreal.unregister_slate_post_tick_callback(state['handle'])
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)

def tick(delta):
    if time.monotonic()<state['next']: return
    try:
        if state['runtime']:
            game = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
            if not game:
                assert time.monotonic()<state['deadline'], 'PIE startup timed out'
                return
            game_temples = unreal.GameplayStatics.get_all_actors_of_class(game,unreal.ChunkedDestructibleZiggurat)
            assert len(game_temples)==1
            assert triangles(game_temples[0]) == results['default_triangles']
            unreal.SystemLibrary.execute_console_command(game,'EnableCheats')
            unreal.SystemLibrary.execute_console_command(game,'Summon ' + bp.generated_class().get_path_name())
            game_temples = unreal.GameplayStatics.get_all_actors_of_class(game,unreal.ChunkedDestructibleZiggurat)
            assert len(game_temples)==2
            for actor in game_temples:
                assert triangles(actor) == results['default_triangles']
                assert not actor.is_actor_tick_enabled()
            spawned = game_temples[-1]
            spawned.set_editor_property('tier_count',2)
            spawned.rebuild_ziggurat()
            assert spawned.generated_piece_count < results['pieces']
            results.update(runtime_spawn=True, runtime_rebuild=True, idle_tick_disabled=True)
            (out/'Verification.json').write_text(json.dumps(results,indent=2))
            unreal.log('ZIGGURAT_RENDER_PERSISTENCE_RUNTIME_OK')
            unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
            stop()
            return
        if state['captures']:
            capture.capture_scene()
            state['captures']-=1
            state.update(export=state['captures']==0,next=time.monotonic()+.4)
            return
        if state['export']:
            unreal.RenderingLibrary.export_render_target(world,target,str(out),views[state['index']]+'.png')
            state['index']+=1
            state['export']=False
            if state['index']==len(views):
                if '-ZigguratRenderOnly' in unreal.SystemLibrary.get_command_line():
                    (out/'RenderMetrics.json').write_text(json.dumps(results,indent=2))
                    unreal.log('ZIGGURAT_RENDER_OK')
                    stop()
                    return
                # Reload the saved preview before PIE so no test damage or variant persists.
                unreal.EditorLoadingAndSavingUtils.load_map('/Game/Structures/Ziggurat/Lvl_ZigguratPreview')
                state.update(runtime=True,next=time.monotonic()+5,deadline=time.monotonic()+60)
                unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_play_simulate()
                return
        name=views[state['index']]
        location,look = unreal.Vector(3800,-4900,3200),unreal.Vector(0,0,850)
        if name in ('Facade','Damage'):
            location,look = unreal.Vector(1200,-3300,1450),unreal.Vector(0,-450,700)
        if name=='Damage':
            temple.set_editor_property('flash_duration',0.)
            temple.set_editor_property('play_destroy_tick_sound',False)
            temple.set_editor_property('use_destruction_ripple',False)
            count=temple.destroy_cells_in_radius(unreal.Vector(-680,-1030,330),310,unreal.ChunkedFloorBlastShape.CIRCLE)
            assert count>10
            assert triangles(temple) == results['default_triangles']-12*count
            results['explosion_removed_pieces']=count
        if name=='Variant':
            for key,value in {'tier_count':6,'base_width':2100.,'base_depth':1800.,'tier_height':320.,
                               'top_scale':.22,'front_window_count':4,'side_window_count':3,
                               'railings':False,'balcony_overhang':55.,'piece_size':120.}.items():
                temple.set_editor_property(key,value)
            temple.rebuild_ziggurat()
            assert not temple.generation_warning
            results['variant_pieces']=temple.generated_piece_count
        camera.set_actor_location(location,False,False)
        camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(location,look),False)
        state.update(captures=4,next=time.monotonic()+3)
    except Exception:
        stop()
        raise

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state['handle']=unreal.register_slate_post_tick_callback(tick)
