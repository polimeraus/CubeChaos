"""Fresh-process soft orb material verification, fade sequence and alternate views."""
from pathlib import Path
import json, time, traceback, unreal

out=Path(unreal.Paths.project_saved_dir()).resolve()/'BombSoftGlow/Preview'
out.mkdir(parents=True,exist_ok=True)
fx=unreal.load_asset('/Game/Weapons/FuseBomb/Materials/M_BombEffect')
shader=next(n for n in unreal.MaterialEditingLibrary.get_material_expressions(fx) if isinstance(n,unreal.MaterialExpressionCustom))
assert shader.get_editor_property('code')==Path(__file__).parent.parent.joinpath('BombEffect.ush').read_text()
assert fx.blend_mode==unreal.BlendMode.BLEND_ADDITIVE
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
world=unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
for index in range(6):
    bomb=actors.spawn_actor_from_class(unreal.BombProjectile,unreal.Vector(0,0,500))
    bomb.set_actor_label('SphericalPreview_'+str(index))
    bomb.set_editor_property('fuse_seconds',100.)
    bomb.set_editor_property('launch_velocity',unreal.Vector())
    bomb.movement.set_editor_property('projectile_gravity_scale',0.)
floor=actors.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(0,0,-20))
floor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
floor.set_actor_scale3d(unreal.Vector(40,40,.4))
surface=floor.static_mesh_component.create_dynamic_material_instance(0,unreal.load_asset('/Game/LevelPrototyping/Materials/M_FlatCol'))
surface.set_vector_parameter_value('Base Color',unreal.LinearColor(.018,.022,.035,1))
lamp=actors.spawn_actor_from_class(unreal.DirectionalLight,unreal.Vector(0,0,1000),unreal.Rotator(-55,-30,0))
lamp.light_component.set_editor_property('intensity',2.)
camera=actors.spawn_actor_from_class(unreal.SceneCapture2D,unreal.Vector())
capture=camera.get_component_by_class(unreal.SceneCaptureComponent2D)
for k,v in dict(capture_every_frame=True,always_persist_rendering_state=True,
    capture_source=unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR,fov_angle=65.).items():capture.set_editor_property(k,v)
target=unreal.RenderingLibrary.create_render_target2d(world,1280,900,unreal.TextureRenderTargetFormat.RTF_RGBA8)
capture.texture_target=target
post=capture.get_editor_property('post_process_settings')
for k,v in dict(override_auto_exposure_method=True,auto_exposure_method=unreal.AutoExposureMethod.AEM_MANUAL,
    override_auto_exposure_bias=True,auto_exposure_bias=0.,override_auto_exposure_apply_physical_camera_exposure=True,
    auto_exposure_apply_physical_camera_exposure=False).items():post.set_editor_property(k,v)
capture.set_editor_property('post_process_settings',post)
frames=[('Glow_012',.12,unreal.Vector(-1050,-1100,850)),
        ('Glow_040',.4,unreal.Vector(-1050,-1100,850)),
        ('Glow_080',.8,unreal.Vector(-1050,-1100,850)),
        ('Gone_140',1.4,unreal.Vector(-1050,-1100,850)),
        ('Side_040',.4,unreal.Vector(-1500,0,550)),
        ('Overhead_040',.4,unreal.Vector(-5,0,2000))]
state=dict(index=0,stage='world',next=0,rows=[])
def close(error=None):
    unreal.unregister_slate_post_tick_callback(state['handle'])
    (out/'Result.json').write_text(json.dumps(dict(error=error,material_persisted=True,frames=state['rows']),indent=2))
    unreal.log('SPHERICAL_BOMB_RENDER '+('FAILED '+error if error else 'OK'))
    unreal.GameplayStatics.set_game_paused(world,False)
    level.editor_request_end_play()
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
def tick(delta):
    global world,camera,capture,target
    if time.monotonic()<state['next']:return
    try:
        if state['stage']=='world':
            game=editor.get_game_world()
            if not game:return
            world=game
            state['bombs']=sorted(unreal.GameplayStatics.get_all_actors_of_class(world,unreal.BombProjectile),key=lambda b:b.get_actor_label())
            if len(state['bombs'])!=len(frames):return
            for b in state['bombs']:
                b.movement.deactivate();b.set_actor_enable_collision(False);b.set_actor_hidden_in_game(True)
                b.set_actor_tick_enabled(False);b.fuse_light.set_intensity(0.)
            camera=unreal.GameplayStatics.get_all_actors_of_class(world,unreal.SceneCapture2D)[0]
            capture=camera.get_component_by_class(unreal.SceneCaptureComponent2D)
            target=unreal.RenderingLibrary.create_render_target2d(world,1280,900,unreal.TextureRenderTargetFormat.RTF_RGBA8)
            capture.texture_target=target
            controller=unreal.GameplayStatics.get_player_controller(world,0)
            if controller:
                controller.set_view_target_with_blend(camera)
                pawn=unreal.GameplayStatics.get_player_pawn(world,0)
                if pawn:pawn.set_actor_location(unreal.Vector(10000,10000,100),False,True)
            state.update(stage=0,next=time.monotonic()+12);return
        name,age,eye=frames[state['index']]
        bomb=state['bombs'][state['index']]
        if state['stage']==0:
            unreal.GameplayStatics.set_game_paused(world,False)
            camera.set_actor_location_and_rotation(eye,unreal.MathLibrary.find_look_at_rotation(eye,bomb.get_actor_location()),False,False)
            bomb.set_actor_hidden_in_game(False);bomb.set_actor_tick_enabled(True);bomb.explode()
            state['start']=unreal.GameplayStatics.get_time_seconds(world)
            state['stage']='age';state['next']=0;return
        elif state['stage']=='age':
            elapsed=unreal.GameplayStatics.get_time_seconds(world)-state['start']
            if elapsed<age:return
            unreal.GameplayStatics.set_game_paused(world,True)
            state['actual_age']=elapsed
            capture.capture_scene();state['stage']=1
        elif state['stage']==1:
            capture.capture_scene();state['stage']=2
        else:
            unreal.RenderingLibrary.export_render_target(world,target,str(out),name+'.png')
            state['rows'].append(dict(name=name,age=state['actual_age']))
            bomb.set_actor_hidden_in_game(True);bomb.set_actor_tick_enabled(False);bomb.fuse_light.set_intensity(0.)
            state['index']+=1;state['stage']=0
            if state['index']==len(frames):close();return
        state['next']=time.monotonic()+1
    except Exception:close(traceback.format_exc())
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state['handle']=unreal.register_slate_post_tick_callback(tick)
level.editor_request_begin_play()
