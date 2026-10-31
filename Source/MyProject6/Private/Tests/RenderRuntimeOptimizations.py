"""Verify reused railgun GPU buffers in a disposable PIE scene; save no assets."""
from pathlib import Path
import json,time,traceback,unreal

out=Path(unreal.Paths.project_saved_dir()).resolve()/'RuntimePerformance/Preview'
out.mkdir(parents=True,exist_ok=True)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
world=unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
player_class=unreal.load_class(None,'/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C')
defaults=unreal.get_default_object(player_class)
for index in range(2):
    beam=actors.spawn_actor_from_class(unreal.RailgunBeam,unreal.Vector())
    beam.set_actor_label('RuntimePerfBeam'+str(index))
    beam.visuals=defaults.railgun_visuals if index==0 else defaults.purple_railgun_visuals
floor=actors.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(0,0,-10))
floor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
floor.set_actor_scale3d(unreal.Vector(40,30,.2))
surface=floor.static_mesh_component.create_dynamic_material_instance(0,unreal.load_asset('/Game/LevelPrototyping/Materials/M_FlatCol'))
surface.set_vector_parameter_value('Base Color',unreal.LinearColor(.025,.03,.045,1))
light=actors.spawn_actor_from_class(unreal.DirectionalLight,unreal.Vector(0,0,900),unreal.Rotator(-50,-20,0))
light.light_component.set_editor_property('intensity',4.)
eye=unreal.Vector(-600,-1500,1000)
camera=actors.spawn_actor_from_class(unreal.SceneCapture2D,eye,unreal.MathLibrary.find_look_at_rotation(eye,unreal.Vector(0,0,260)))
capture=camera.get_component_by_class(unreal.SceneCaptureComponent2D)
for k,v in dict(capture_every_frame=True,capture_source=unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR,fov_angle=58.).items():capture.set_editor_property(k,v)
post=capture.get_editor_property('post_process_settings')
for k,v in dict(override_auto_exposure_method=True,auto_exposure_method=unreal.AutoExposureMethod.AEM_MANUAL,
    override_auto_exposure_bias=True,auto_exposure_bias=0.,override_auto_exposure_apply_physical_camera_exposure=True,
    auto_exposure_apply_physical_camera_exposure=False).items():post.set_editor_property(k,v)
capture.set_editor_property('post_process_settings',post)
state=dict(stage='world',next=0,index=0,started=time.monotonic(),rows=[])
ages=[.02,.15,.45,.85]
def finish(error=None):
    unreal.unregister_slate_post_tick_callback(state['handle'])
    (out/'Result.json').write_text(json.dumps(dict(error=error,frames=state['rows']),indent=2))
    unreal.log('RUNTIME_PERFORMANCE_RENDER '+('FAILED '+error if error else 'OK'))
    level.editor_request_end_play();unreal.EditorPythonScripting.set_keep_python_script_alive(False)
def tick(delta):
    global world,capture
    try:
        if time.monotonic()-state['started']>120:raise RuntimeError('Preview timed out')
        if time.monotonic()<state['next']:return
        if state['stage']=='world':
            game=editor.get_game_world()
            if not game:return
            world=game
            beams=sorted(unreal.GameplayStatics.get_all_actors_of_class(game,unreal.RailgunBeam),key=lambda b:b.get_actor_label())
            if len(beams)!=2:return
            for index,beam in enumerate(beams):
                beam.set_actor_tick_enabled(False)
                beam.initialize_beam(unreal.Vector(-650,index*250-125,250),unreal.Vector(650,index*250-125,250),.65)
                beam.set_life_span(0);beam.set_impact(unreal.Vector(650,index*250-125,250));beam.face_camera(eye)
            capture=unreal.GameplayStatics.get_all_actors_of_class(game,unreal.SceneCapture2D)[0].get_component_by_class(unreal.SceneCaptureComponent2D)
            capture.texture_target=unreal.RenderingLibrary.create_render_target2d(game,1280,720,unreal.TextureRenderTargetFormat.RTF_RGBA8)
            player=unreal.GameplayStatics.get_player_character(game,0)
            if player:
                player.character_movement.disable_movement();player.set_actor_location(unreal.Vector(10000,0,500),False,True)
                assert not player.electric_weapon.is_actor_tick_enabled()
                assert not player.electric_weapon.field.is_component_tick_enabled()
            state.update(beams=beams,stage='update',next=time.monotonic()+12);return
        if state['stage']=='update':
            # Many changes with identical topology exercise the fast GPU update path.
            age=ages[state['index']]
            for beam in state['beams']:
                for step in range(30):beam.set_preview_age(max(0.,age-.01+step*.01/29))
            state.update(stage='save',next=time.monotonic()+1.5);return
        age=ages[state['index']];name=f'Railguns_{round(age*100):03d}.png'
        unreal.RenderingLibrary.export_render_target(world,capture.texture_target,str(out),name)
        state['rows'].append(dict(name=name,age=age))
        state['index']+=1
        if state['index']==len(ages):finish();return
        state.update(stage='update',next=time.monotonic()+.2)
    except Exception:finish(traceback.format_exc())
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state['handle']=unreal.register_slate_post_tick_callback(tick)
level.editor_request_begin_play()
