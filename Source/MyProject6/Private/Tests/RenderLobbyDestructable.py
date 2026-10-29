"""Real PIE rendering of fixed waves, both music modes, and cell damage. Saves no map changes."""
import json,time
from pathlib import Path
import unreal
out=Path(unreal.Paths.project_saved_dir())/'LobbyDestructable/Preview'
out.mkdir(parents=True,exist_ok=True)
unreal.EditorLoadingAndSavingUtils.load_map('/Game/GameplayActors/LobbyPlatformDestructable/Maps/Lvl_LobbyPlatformDestructablePreview')
editor_actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
editor_actors.set_selected_level_actors([])
editor_camera=next(a for a in editor_actors.get_all_level_actors() if isinstance(a,unreal.CameraActor))
unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(editor_camera.get_actor_location(),editor_camera.get_actor_rotation())
editor_actors.spawn_actor_from_class(unreal.SceneCapture2D,editor_camera.get_actor_location(),editor_camera.get_actor_rotation())
views=['Reference','FlowEarly','FlowLate','Equalizer','TravelingColumns','Damage','DamageFlow','DamageClose']
state={'start':time.monotonic(),'index':0,'ready':False,'prepared':False,'results':{}}

def finish(error=None):
    unreal.unregister_slate_post_tick_callback(state['handle'])
    state['results'].update(passed=error is None,error=error)
    (out/'Render.json').write_text(json.dumps(state['results'],indent=2))
    if error: unreal.log_error('LOBBY_CELL_RENDER_FAILED '+error)
    else: unreal.log('LOBBY_CELL_RENDER_OK')
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)

def tick(_):
    try:
        assert time.monotonic()-state['start']<180,'PIE rendering timed out'
        game=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
        if not game: return
        if not state['ready']:
            found=unreal.GameplayStatics.get_all_actors_of_class(game,unreal.ParametricLobbyPlatformDestructable)
            if not found: return
            lobby=found[0]
            lobby.set_editor_property('use_destruction_ripple',False)
            player=unreal.GameplayStatics.get_player_controller(game,0)
            # This offscreen test has only a SceneCapture, not a rendered player viewport.
            # Remove its unused player camera so viewport-only visibility culling cannot suspend
            # the real shared animation timer. No animation phases are set by this test.
            if player: player.set_editor_property('player_camera_manager',None)
            assert unreal.GameplayStatics.get_player_camera_manager(game,0) is None
            capture_actor=unreal.GameplayStatics.get_all_actors_of_class(game,unreal.SceneCapture2D)[0]
            capture=capture_actor.get_component_by_class(unreal.SceneCaptureComponent2D)
            capture.set_editor_property('capture_every_frame',True)
            capture.set_editor_property('always_persist_rendering_state',True)
            capture.set_editor_property('capture_on_movement',False)
            capture.set_editor_property('fov_angle',40.)
            capture.set_editor_property('capture_source',unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
            post=unreal.GameplayStatics.get_all_actors_of_class(game,unreal.PostProcessVolume)[0]
            capture.set_editor_property('post_process_settings',post.get_editor_property('settings'))
            capture.set_editor_property('post_process_blend_weight',1.)
            target=unreal.RenderingLibrary.create_render_target2d(game,1440,1100,unreal.TextureRenderTargetFormat.RTF_RGBA8)
            capture.set_editor_property('texture_target',target)
            state.update(ready=True,lobby=lobby,capture_actor=capture_actor,capture=capture,target=target,ready_time=time.monotonic()+4)
            return
        if time.monotonic()<state['ready_time']: return
        lobby=state['lobby']; now=unreal.GameplayStatics.get_time_seconds(game)
        name=views[state['index']]
        if not state['prepared']:
            lobby.set_planar_animation_type(unreal.PlanarAnimationType.NONE)
            lobby.set_react_to_music(False)
            if name=='DamageClose':
                position=unreal.Vector(200,-1650,650)
                state['capture_actor'].set_actor_location(position,False,False)
                state['capture_actor'].set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(position,unreal.Vector(0,-650,-40)),False)
            if name in ('Damage','DamageFlow'):
                if name=='Damage':
                    assert lobby.remove_platform_sphere(unreal.Vector(0,-650,-40),380)
                    assert lobby.remove_platform_sphere(unreal.Vector(-1500,-450,-40),350)
            if name in ('FlowEarly','FlowLate','DamageFlow'):
                lobby.set_planar_animation_type(unreal.PlanarAnimationType.FLOWING_WAVE)
                lobby.play_fixed_animation()
            if name in ('Equalizer','TravelingColumns'):
                lobby.set_music_animation_type(unreal.MusicAnimationType.EQUALIZER if name=='Equalizer' else unreal.MusicAnimationType.TRAVELING_COLUMNS)
                lobby.set_react_to_music(True)
                lobby.trigger_music_reaction(1.,True)
            delay=.14 if name=='Equalizer' else .25 if name=='TravelingColumns' else 1.6 if name=='FlowLate' else .5
            state.update(prepared=True,capture_time=now+delay)
            return
        if now<state['capture_time']: return
        surfaces=[c for c in lobby.get_components_by_class(unreal.DynamicMeshComponent) if c.component_has_tag('PlanarAnimationSurface')]
        if name in ('FlowEarly','FlowLate','DamageFlow'):
            assert surfaces and any(c.is_visible() for c in surfaces),'Fixed wave not visible'
            assert surfaces[0].get_material(0).get_vector_parameter_value('Waves0').r>.05,'Fixed wave time did not advance'
        if name in ('Equalizer','TravelingColumns'):
            assert surfaces and surfaces[0].get_material(0).get_scalar_parameter_value('MusicEnabled')>0,'Music mask not active'
        state['capture'].capture_scene()
        # Readback fences the capture before export so each file belongs to this exact mode.
        pixels=unreal.RenderingLibrary.read_render_target(game,state['target'],False)
        assert pixels,'Empty capture'
        unreal.RenderingLibrary.export_render_target(game,state['target'],str(out),name+'.png')
        state['results'][name]={'animation_surfaces':len(surfaces),'game_time':now,
            'wave_phase':str(surfaces[0].get_material(0).get_vector_parameter_value('Waves0')) if surfaces else None}
        state.update(index=state['index']+1,prepared=False)
        if state['index']==len(views): finish()
    except Exception as error: finish(str(error))

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state['handle']=unreal.register_slate_post_tick_callback(tick)
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_play_simulate()
