"""Fresh saved/native/BP defaults, visual reference check, damaged holes, runtime spawn. No asset writes."""
import json,time,runpy
from pathlib import Path
import unreal

if '-BabylonPreviewOnly' not in unreal.SystemLibrary.get_command_line() and (Path(unreal.Paths.project_saved_dir())/'BabylonTower/PolygonUpgrade/SavedInstances.json').exists():
    runpy.run_path(str(Path(__file__).with_name('VerifyBabylonTowerDefaults.py')))

folder='/Game/Structures/Destructible/BabylonTower'
out=Path(unreal.Paths.project_saved_dir())/'BabylonTower/Preview'
out.mkdir(parents=True,exist_ok=True)
world=unreal.EditorLoadingAndSavingUtils.load_map(folder+'/Maps/Lvl_BabylonTowerPreview')
assert world
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
bp=unreal.load_asset(folder+'/BP_ChunkedDestructibleBabylonTower')
cdo=unreal.get_default_object(bp.generated_class())
placed=[a for a in actors.get_all_level_actors() if isinstance(a,unreal.ChunkedDestructibleBabylonTower)]
assert len(placed)==1
temple=placed[0]
def triangles(actor):
    return sum(c.get_dynamic_mesh().get_triangle_count() for c in actor.get_components_by_class(unreal.DynamicMeshComponent))
fresh=actors.spawn_actor_from_class(bp.generated_class(),unreal.Vector(8000,0,0))
native=actors.spawn_actor_from_class(unreal.ChunkedDestructibleBabylonTower,unreal.Vector(-8000,0,0))
assert triangles(fresh)==triangles(temple)==triangles(native)
keys=('side_count','base_width','base_depth','tier_count','tier_height','summit_height','top_scale','wall_batter',
      'wall_thickness','piece_size','piece_gap','windows_per_side','tier_window_counts','tier_window_widths',
      'upper_windows','window_width','window_height','window_sill_height','cornices','cornice_overhang',
      'cornice_height','floor_thickness','summit_corbels','entrance','entrance_count','entrance_width','entrance_height',
      'entrance_frame','entrance_stairs','stair_count','stair_tread_depth','stair_width','foundation_height',
      'foundation_overhang','stone_material','cornice_material','terrace_material','portal_material',
      'foundation_material','recess_material','open_facade_recesses','tier_heights','scale_windows_with_tiers','window_height_ratio')
for name in keys:
    assert temple.get_editor_property(name)==fresh.get_editor_property(name)==cdo.get_editor_property(name),name
    assert native.get_editor_property(name)==cdo.get_editor_property(name),('native',name)
for component in temple.get_components_by_class(unreal.DynamicMeshComponent):
    assert component.get_num_materials()==6 and all(component.get_material(i) for i in range(6))
actors.destroy_actor(fresh)
actors.destroy_actor(native)
results={'fresh_process_persistence':True,'native_and_blueprint_defaults_match':True,
         'pieces':temple.generated_piece_count,'openings':temple.generated_opening_count,
         'triangles':triangles(temple),'height_cm':temple.generated_height}

camera=actors.spawn_actor_from_class(unreal.SceneCapture2D,unreal.Vector())
capture=camera.get_component_by_class(unreal.SceneCaptureComponent2D)
for key,value in {'capture_every_frame':False,'capture_on_movement':False,'always_persist_rendering_state':True,
                  'fov_angle':40.,'capture_source':unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR}.items():
    capture.set_editor_property(key,value)
post=capture.get_editor_property('post_process_settings')
for key,value in {'override_auto_exposure_method':True,'auto_exposure_method':unreal.AutoExposureMethod.AEM_MANUAL,
                  'override_auto_exposure_apply_physical_camera_exposure':True,'auto_exposure_apply_physical_camera_exposure':False,
                  'override_auto_exposure_bias':True,'auto_exposure_bias':.2,
                  'override_ambient_occlusion_intensity':True,'ambient_occlusion_intensity':.8}.items():
    post.set_editor_property(key,value)
capture.set_editor_property('post_process_settings',post)
target=unreal.RenderingLibrary.create_render_target2d(world,1400,1200,unreal.TextureRenderTargetFormat.RTF_RGBA8)
capture.set_editor_property('texture_target',target)
views=['Reference','Front','Damage','Variant']
state={'next':time.monotonic()+15,'index':0,'captures':0,'export':False,'runtime':False}
def stop():
    unreal.unregister_slate_post_tick_callback(state['handle'])
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
def tick(dt):
    if time.monotonic()<state['next']:return
    try:
        if state['runtime']:
            game=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
            if not game:
                assert time.monotonic()<state['deadline'],'PIE timed out'
                return
            runtime=unreal.GameplayStatics.get_all_actors_of_class(game,unreal.ChunkedDestructibleBabylonTower)
            assert len(runtime)==1 and triangles(runtime[0])==results['triangles']
            original=runtime[0]
            unreal.SystemLibrary.execute_console_command(game,'EnableCheats')
            unreal.SystemLibrary.execute_console_command(game,'Summon '+bp.generated_class().get_path_name())
            runtime=unreal.GameplayStatics.get_all_actors_of_class(game,unreal.ChunkedDestructibleBabylonTower)
            assert len(runtime)==2
            spawned=next(a for a in runtime if a!=original)
            assert triangles(spawned)==results['triangles'] and not spawned.is_actor_tick_enabled()
            spawned.set_editor_property('tier_count',2)
            spawned.rebuild_tower()
            assert triangles(spawned)!=results['triangles'] and not spawned.generation_warning
            results.update(runtime_spawn=True,runtime_rebuild=True,idle_tick_disabled=True)
            (out/'Verification.json').write_text(json.dumps(results,indent=2))
            unreal.log('BABYLON_TOWER_VERIFIED')
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
                if '-BabylonRenderOnly' in unreal.SystemLibrary.get_command_line():
                    (out/'RenderMetrics.json').write_text(json.dumps(results,indent=2))
                    unreal.log('BABYLON_TOWER_RENDER_OK')
                    stop();return
                unreal.EditorLoadingAndSavingUtils.load_map(folder+'/Maps/Lvl_BabylonTowerPreview')
                state.update(runtime=True,next=time.monotonic()+5,deadline=time.monotonic()+60)
                unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_play_simulate()
                return
        name=views[state['index']]
        location,look=unreal.Vector(4200,-9300,3200),unreal.Vector(0,0,1950)
        if name=='Front':location=unreal.Vector(0,-10500,2600)
        if name=='Damage':
            temple.set_editor_property('flash_duration',0.)
            temple.set_editor_property('play_destroy_tick_sound',False)
            temple.set_editor_property('use_destruction_ripple',False)
            removed=temple.destroy_cells_in_radius(unreal.Vector(400,-1750,400),370,unreal.ChunkedFloorBlastShape.CIRCLE)
            assert removed>5 and triangles(temple)<results['triangles']
            results['explosion_removed_pieces']=removed
        if name=='Variant':
            for k,v in {'tier_count':4,'base_width':3100.,'base_depth':2400.,'summit_height':650.,'tier_height':500.,
                        'side_count':7,'windows_per_side':2,'window_width':140.,'piece_size':150.,'top_scale':.4}.items():
                temple.set_editor_property(k,v)
            temple.rebuild_tower()
            assert not temple.generation_warning
            results['variant_pieces']=temple.generated_piece_count
        camera.set_actor_location(location,False,False)
        camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(location,look),False)
        state.update(captures=4,next=time.monotonic()+2)
    except Exception:
        stop()
        raise
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state['handle']=unreal.register_slate_post_tick_callback(tick)
