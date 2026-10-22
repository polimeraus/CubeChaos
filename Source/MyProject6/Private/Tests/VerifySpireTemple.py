"""Fresh-process persistence, Blueprint/native spawns, reference renders and PIE rebuild."""
from pathlib import Path
import json
import time
import unreal

folder = '/Game/Structures/Destructible/SpireTemple'
out = Path(unreal.Paths.project_saved_dir())/'SpireTemple/Preview'
out.mkdir(parents=True, exist_ok=True)
world = unreal.EditorLoadingAndSavingUtils.load_map(folder+'/Maps/Lvl_SpireTemplePreview')
assert world
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
bp = unreal.load_asset(folder+'/BP_ChunkedDestructibleSpireTemple')
temple = next(a for a in actors.get_all_level_actors() if isinstance(a,unreal.ChunkedDestructibleSpireTemple))
cdo = unreal.get_default_object(bp.generated_class())
native = unreal.get_default_object(unreal.ChunkedDestructibleSpireTemple)
keys = ('spire','spire_radius','spire_height','spire_tip_ratio','spire_sides','spire_courses','spire_shell_thickness',
        'stagger_spire_joints','spire_collar_height','lower_drum_radius','lower_drum_height','upper_drum_radius',
        'upper_drum_height','drum_sides','drum_wall_thickness','cornice_overhang','cornice_height','plinth_height',
        'interior_floors','drum_entrance','drum_entrance_width','drum_entrance_height','drum_windows',
        'drum_windows_around','drum_window_rows','drum_window_width_ratio','drum_window_height_ratio',
        'rectangular_enclosure','courtyard_width','courtyard_depth','enclosure_height','enclosure_thickness',
        'front_bays','side_bays','enclosure_openings','enclosure_opening_ratio','enclosure_pier_projection',
        'courtyard_floor','courtyard_floor_thickness','gate','gate_width','gate_height','gate_frame_width',
        'gate_projection','rear_gate','piece_size','piece_gap','stone_material','trim_material','spire_material','floor_material')
def triangles(a):
    return sum(c.get_dynamic_mesh().get_triangle_count() for c in a.get_components_by_class(unreal.DynamicMeshComponent))
original_tris = triangles(temple)
for cls in (bp.generated_class(), unreal.ChunkedDestructibleSpireTemple):
    fresh = actors.spawn_actor_from_class(cls,unreal.Vector(20000,0,0))
    for key in keys:
        assert fresh.get_editor_property(key)==temple.get_editor_property(key)==cdo.get_editor_property(key)==native.get_editor_property(key),key
    assert triangles(fresh)==original_tris and not fresh.generation_warning
    actors.destroy_actor(fresh)
for comp in temple.get_components_by_class(unreal.DynamicMeshComponent):
    assert comp.get_num_materials()==4 and all(comp.get_material(i) for i in range(4))
results = {'fresh_process_persistence':True,'native_blueprint_placed_and_new_instances_match':True,
           'pieces':temple.generated_piece_count,'triangles':original_tris,'height_cm':temple.generated_height,
           'enclosure_pieces':temple.generated_enclosure_pieces,'spire_pieces':temple.generated_spire_pieces}
(out/'Persistence.json').write_text(json.dumps(results,indent=2))

camera = actors.spawn_actor_from_class(unreal.SceneCapture2D,unreal.Vector())
capture = camera.get_component_by_class(unreal.SceneCaptureComponent2D)
for key,value in {'capture_every_frame':False,'capture_on_movement':False,'always_persist_rendering_state':True,
                  'fov_angle':26.,'capture_source':unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR}.items():
    capture.set_editor_property(key,value)
post = capture.get_editor_property('post_process_settings')
for key,value in {'override_auto_exposure_method':True,'auto_exposure_method':unreal.AutoExposureMethod.AEM_MANUAL,
                  'override_auto_exposure_apply_physical_camera_exposure':True,'auto_exposure_apply_physical_camera_exposure':False,
                  'override_auto_exposure_bias':True,'auto_exposure_bias':.1,
                  'override_ambient_occlusion_intensity':True,'ambient_occlusion_intensity':1.}.items():
    post.set_editor_property(key,value)
capture.set_editor_property('post_process_settings',post)
target = unreal.RenderingLibrary.create_render_target2d(world,1200,1200,unreal.TextureRenderTargetFormat.RTF_RGBA8)
capture.set_editor_property('texture_target',target)
reference_only = '-SpireReferenceOnly' in unreal.SystemLibrary.get_command_line()
views = ['Reference'] if reference_only else ['Reference','NoEnclosure','Damage','Variant']
state = {'next':time.monotonic()+12,'index':0,'captures':0,'export':False,'runtime':False}
def stop():
    unreal.unregister_slate_post_tick_callback(state['handle'])
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
def tick(dt):
    if time.monotonic()<state['next']:return
    try:
        if state['runtime']:
            game=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
            if not game:
                assert time.monotonic()<state['deadline'],'PIE start timeout'
                return
            runtime=unreal.GameplayStatics.get_all_actors_of_class(game,unreal.ChunkedDestructibleSpireTemple)
            assert len(runtime)==1 and triangles(runtime[0])==original_tris
            original=runtime[0]
            unreal.SystemLibrary.execute_console_command(game,'EnableCheats')
            unreal.SystemLibrary.execute_console_command(game,'Summon '+bp.generated_class().get_path_name())
            runtime=unreal.GameplayStatics.get_all_actors_of_class(game,unreal.ChunkedDestructibleSpireTemple)
            assert len(runtime)==2
            spawned=next(a for a in runtime if a!=original)
            assert triangles(spawned)==original_tris
            spawned.set_editor_property('rectangular_enclosure',False)
            spawned.rebuild_temple()
            assert spawned.generated_enclosure_pieces==0 and not spawned.is_actor_tick_enabled()
            results.update(runtime_spawn=True,runtime_rebuild=True,idle_tick_disabled=True)
            (out/'Verification.json').write_text(json.dumps(results,indent=2))
            unreal.log('SPIRE_TEMPLE_VERIFIED')
            unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
            stop();return
        if state['captures']:
            capture.capture_scene();state['captures']-=1
            state.update(export=state['captures']==0,next=time.monotonic()+.5);return
        if state['export']:
            unreal.RenderingLibrary.export_render_target(world,target,str(out),views[state['index']]+'.png')
            state['index']+=1;state['export']=False
            if state['index']==len(views):
                if reference_only:
                    unreal.log('SPIRE_TEMPLE_REFERENCE_RENDER_OK');stop();return
                unreal.EditorLoadingAndSavingUtils.load_map(folder+'/Maps/Lvl_SpireTemplePreview')
                state.update(runtime=True,next=time.monotonic()+5,deadline=time.monotonic()+60)
                unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_play_simulate();return
        name=views[state['index']]
        location,look=unreal.Vector(3900,-10500,4400),unreal.Vector(0,0,1450)
        if name=='NoEnclosure':
            temple.set_editor_property('rectangular_enclosure',False);temple.rebuild_temple()
            assert temple.generated_enclosure_pieces==0
        if name=='Damage':
            temple.set_editor_property('rectangular_enclosure',True);temple.rebuild_temple()
            for key,value in {'flash_duration':0.,'play_destroy_tick_sound':False,'use_destruction_ripple':False}.items():
                temple.set_editor_property(key,value)
            removed=temple.destroy_cells_in_radius(unreal.Vector(850,-1480,350),430,unreal.ChunkedFloorBlastShape.CIRCLE)
            removed+=temple.destroy_cells_in_radius(unreal.Vector(0,-300,1700),300,unreal.ChunkedFloorBlastShape.CIRCLE)
            assert removed>10 and triangles(temple)<original_tris
            results['blast_removed_pieces']=removed
        if name=='Variant':
            for key,value in {'spire_tip_ratio':.12,'spire_height':1500.,'spire_courses':18,'spire_sides':32,
                              'courtyard_width':4400.,'courtyard_depth':2500.,'front_bays':24,'side_bays':12,
                              'drum_window_rows':3,'upper_drum_height':650.,'rear_gate':True}.items():
                temple.set_editor_property(key,value)
            temple.rebuild_temple();assert not temple.generation_warning
        camera.set_actor_location(location,False,False)
        camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(location,look),False)
        state.update(captures=5,next=time.monotonic()+2)
    except Exception:
        stop();raise
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state['handle']=unreal.register_slate_post_tick_callback(tick)
