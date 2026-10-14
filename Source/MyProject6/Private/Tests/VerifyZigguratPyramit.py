"""Fresh saved/native/BP defaults, visual reference check, damaged holes, runtime spawn. No asset writes."""
import json,time
from pathlib import Path
import unreal

folder='/Game/Structures/ZigguratPyramit'
out=Path(unreal.Paths.project_saved_dir())/'ZigguratPyramit/Preview'
out.mkdir(parents=True,exist_ok=True)
world=unreal.EditorLoadingAndSavingUtils.load_map(folder+'/Lvl_ZigguratPyramitPreview')
assert world
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
bp=unreal.load_asset(folder+'/BP_ChunkedDestructibleZigguratPyramit')
cdo=unreal.get_default_object(bp.generated_class())
placed=[a for a in actors.get_all_level_actors() if isinstance(a,unreal.ChunkedDestructibleZigguratPyramit)]
assert len(placed)==1
temple=placed[0]
def triangles(actor):
    return sum(c.get_dynamic_mesh().get_triangle_count() for c in actor.get_components_by_class(unreal.DynamicMeshComponent))
fresh=actors.spawn_actor_from_class(bp.generated_class(),unreal.Vector(8000,0,0))
native=actors.spawn_actor_from_class(unreal.ChunkedDestructibleZigguratPyramit,unreal.Vector(-8000,0,0))
assert triangles(fresh)==triangles(temple)==triangles(native)==temple.generated_piece_count*12
keys=('base_width','base_depth','tier_count','ground_tier_height','tier_height_scale','top_tier_scale','wall_batter',
      'wall_thickness','piece_size','piece_gap','ground_front_openings','ground_side_openings','ground_opening_width',
      'ground_opening_height_ratio','back_openings','front_window_counts','side_window_counts','window_width',
      'window_height_ratio','window_sill_ratio','opening_spread','window_lintels','lintel_height','lintel_overhang',
      'terrace_overhang','terrace_thickness','cornice_bands','cornice_height','railings','railing_height',
      'foundation_height','foundation_overhang','entrance_stairs','stair_count','pyramid_crown','crown_step_count',
      'crown_height','crown_base_scale','crown_top_scale','stone_material','cornice_material','terrace_material',
      'lintel_material','foundation_material','crown_material')
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
                  'fov_angle':36.,'capture_source':unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR}.items():
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
            runtime=unreal.GameplayStatics.get_all_actors_of_class(game,unreal.ChunkedDestructibleZigguratPyramit)
            assert len(runtime)==1 and triangles(runtime[0])==results['triangles']
            original=runtime[0]
            unreal.SystemLibrary.execute_console_command(game,'EnableCheats')
            unreal.SystemLibrary.execute_console_command(game,'Summon '+bp.generated_class().get_path_name())
            runtime=unreal.GameplayStatics.get_all_actors_of_class(game,unreal.ChunkedDestructibleZigguratPyramit)
            assert len(runtime)==2
            spawned=next(a for a in runtime if a!=original)
            assert triangles(spawned)==results['triangles'] and not spawned.is_actor_tick_enabled()
            spawned.set_editor_property('tier_count',2)
            spawned.rebuild_ziggurat_pyramit()
            assert triangles(spawned)!=results['triangles'] and not spawned.generation_warning
            results.update(runtime_spawn=True,runtime_rebuild=True,idle_tick_disabled=True)
            (out/'Verification.json').write_text(json.dumps(results,indent=2))
            unreal.log('ZIGGURAT_PYRAMIT_VERIFIED')
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
                if '-PyramitRenderOnly' in unreal.SystemLibrary.get_command_line():
                    (out/'RenderMetrics.json').write_text(json.dumps(results,indent=2))
                    unreal.log('ZIGGURAT_PYRAMIT_RENDER_OK')
                    stop();return
                unreal.EditorLoadingAndSavingUtils.load_map(folder+'/Lvl_ZigguratPyramitPreview')
                state.update(runtime=True,next=time.monotonic()+5,deadline=time.monotonic()+60)
                unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_play_simulate()
                return
        name=views[state['index']]
        location,look=unreal.Vector(-3100,-5500,2550),unreal.Vector(0,0,1010)
        if name=='Front':location=unreal.Vector(0,-6000,2300)
        if name=='Damage':
            temple.set_editor_property('flash_duration',0.)
            temple.set_editor_property('play_destroy_tick_sound',False)
            temple.set_editor_property('use_destruction_ripple',False)
            removed=temple.destroy_cells_in_radius(unreal.Vector(290,-990,380),245,unreal.ChunkedFloorBlastShape.CIRCLE)
            assert removed>5 and triangles(temple)==results['triangles']-12*removed
            results['explosion_removed_pieces']=removed
        if name=='Variant':
            for k,v in {'tier_count':4,'crown_step_count':6,'crown_height':450.,'ground_tier_height':500.,
                        'ground_front_openings':5,'window_width':140.,'piece_size':130.,'railings':True}.items():
                temple.set_editor_property(k,v)
            temple.rebuild_ziggurat_pyramit()
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
