"""Fresh-process saved defaults/instances and real-time launch-material renders. No saves."""
import json,time
from pathlib import Path
import unreal

out=Path(unreal.Paths.project_saved_dir())/'ZigguratTraversal/Preview'
out.mkdir(parents=True,exist_ok=True)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
types=(unreal.ChunkedDestructibleZiggurat,unreal.ChunkedDestructibleZigguratPyramit)
specs=[('Ziggurat','/Game/Structures/Ziggurat/BP_DestructibleZiggurat','/Game/Structures/Ziggurat/Lvl_ZigguratPreview'),
       ('Pyramit','/Game/Structures/ZigguratPyramit/BP_ChunkedDestructibleZigguratPyramit','/Game/Structures/ZigguratPyramit/Lvl_ZigguratPyramitPreview')]
results={'fresh_process':True,'instances':[]}
keys=('floor_openings','ground_launch_pad','opening_width','pad_width','auto_launch_height','height_above_roof',
      'launch_up_speed','cooldown','affect_np_cs','pattern','color','brightness','animation_speed','ring_count',
      'ring_thickness','arrows_material','rings_material')

def pad_of(temple):return temple.get_editor_property('ground_launch_pad').get_editor_property('child_actor')
for name,bp_path,map_path in specs:
    world=unreal.EditorLoadingAndSavingUtils.load_map(map_path)
    bp=unreal.load_asset(bp_path)
    cdo=unreal.get_default_object(bp.generated_class())
    placed=[a for a in actors.get_all_level_actors() if isinstance(a,types)]
    assert len(placed)==1
    temple=placed[0]
    fresh=actors.spawn_actor_from_class(bp.generated_class(),unreal.Vector(8000,0,0))
    native=actors.spawn_actor_from_class(types[0 if name=='Ziggurat' else 1],unreal.Vector(-8000,0,0))
    for key in keys:
        expected=cdo.traversal.get_editor_property(key)
        assert all(a.traversal.get_editor_property(key)==expected for a in (temple,fresh,native)),(name,key)
    assert temple.traversal.floor_openings and temple.traversal.ground_launch_pad
    assert temple.effective_opening_width==200
    assert temple.generated_piece_count==fresh.generated_piece_count==native.generated_piece_count
    for a in (temple,fresh,native):
        pad=pad_of(a)
        assert isinstance(pad,unreal.LaunchPad) and pad.pad_material and pad.use_target_height
        assert pad.center_before_launch and pad.animation_speed==1
        assert abs((pad.get_actor_location()-a.get_actor_location()).z-a.foundation_height)<.1
    results['instances'].append({'actor':name,'opening_width':temple.effective_opening_width,
                                 'pieces':temple.generated_piece_count,'pad_material':pad_of(temple).pad_material.get_path_name()})

world=unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
world.get_world_settings().set_editor_property('default_game_mode',unreal.GameModeBase)
for i,(name,bp_path,_) in enumerate(specs):
    a=actors.spawn_actor_from_class(unreal.load_asset(bp_path).generated_class(),unreal.Vector(i*4500,0,0))
    a.set_actor_label(name)
    a.set_editor_property('tags',[name])
light=actors.spawn_actor_from_class(unreal.DirectionalLight,unreal.Vector(0,0,4000),unreal.Rotator(pitch=-50,yaw=55,roll=0))
light.light_component.set_editor_property('intensity',3.)
light.light_component.set_editor_property('cast_shadows',False)
actors.spawn_actor_from_class(unreal.SceneCapture2D,unreal.Vector())
state={'started':time.monotonic(),'stage':'start','view':0,'capture_count':0}
views=[(0,'Pad_A'),(0,'Pad_B'),(0,'Shaft'),(1,'Pad_A'),(1,'Pad_B'),(1,'Shaft')]

def stop(error=None):
    results['error']=error
    (out/'Verification.json').write_text(json.dumps(results,indent=2))
    (unreal.log_error if error else unreal.log)('ZIGGURAT_TRAVERSAL_VERIFIED '+json.dumps(results))
    unreal.unregister_slate_post_tick_callback(state['handle'])
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)

def tick(dt):
    try:
        now=time.monotonic()
        assert now-state['started']<150,'Render timed out'
        if state['stage']=='start':
            game=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
            if not game:return
            camera=unreal.GameplayStatics.get_all_actors_of_class(game,unreal.SceneCapture2D)[0]
            capture=camera.get_component_by_class(unreal.SceneCaptureComponent2D)
            capture.set_editor_property('capture_every_frame',False)
            capture.set_editor_property('capture_on_movement',False)
            capture.set_mobility(unreal.ComponentMobility.MOVABLE)
            capture.set_editor_property('capture_source',unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
            capture.set_editor_property('always_persist_rendering_state',True)
            capture.set_editor_property('fov_angle',50.)
            post=capture.get_editor_property('post_process_settings')
            for k,v in {'override_auto_exposure_method':True,'auto_exposure_method':unreal.AutoExposureMethod.AEM_MANUAL,
                        'override_auto_exposure_apply_physical_camera_exposure':True,'auto_exposure_apply_physical_camera_exposure':False,
                        'override_auto_exposure_bias':True,'auto_exposure_bias':0.}.items():post.set_editor_property(k,v)
            capture.set_editor_property('post_process_settings',post)
            target=unreal.RenderingLibrary.create_render_target2d(game,1100,900,unreal.TextureRenderTargetFormat.RTF_RGBA8)
            capture.set_editor_property('texture_target',target)
            temples=[unreal.GameplayStatics.get_all_actors_with_tag(game,name)[0] for name,_,_ in specs]
            assert all(pad_of(a) for a in temples)
            state.update(game=game,camera=camera,capture=capture,target=target,temples=temples,stage='view',next=now+8)
            return
        if now<state['next']:return
        i,kind=views[state['view']]
        temple=state['temples'][i]
        if state['stage']=='view':
            offset=temple.get_actor_location()
            if kind.startswith('Pad'):
                position=offset+unreal.Vector(210,-260,temple.foundation_height+210)
                target=offset+unreal.Vector(0,0,temple.foundation_height+8)
            else:
                # Straight down the shaft: every upper floor and the lit pad below are visible.
                position=offset+unreal.Vector(0,-.01,temple.generated_height+1300)
                target=offset+unreal.Vector(0,0,0)
            state['camera'].set_actor_location(position,False,True)
            state['camera'].set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(position,target),False)
            assert (state['camera'].get_actor_location()-position).length()<.1
            state.update(stage='capture',capture_count=3,next=now+(1.7 if kind=='Pad_B' else 3))
        elif state['stage']=='capture':
            state['capture'].capture_scene()
            state['capture_count']-=1
            state.update(stage='export' if state['capture_count']==0 else 'capture',next=now+.3)
        else:
            unreal.RenderingLibrary.export_render_target(state['game'],state['target'],str(out),specs[i][0]+'_'+kind+'.png')
            state['view']+=1
            if state['view']==len(views):
                results['runtime_child_pads']=True
                stop();return
            state.update(stage='view',next=now+.2)
    except Exception as error:stop(str(error));raise

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state['handle']=unreal.register_slate_post_tick_callback(tick)
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_play_simulate()
