"""Run with -SetupPlatformFinish to save; run again in a fresh editor process to verify/render."""
import json
import time
from pathlib import Path
import unreal

ROOT = '/Game/GameplayActors/Platforms/ParametricPlatform'
CHILD = ROOT + '/Examples/BP_ParametricPlatform_Lobby'
MAP = ROOT + '/Maps/L_ParametricPlatformFinish'
OUT = Path(unreal.Paths.project_saved_dir()) / 'ParametricPlatform' / 'Finish'
OUT.mkdir(parents=True, exist_ok=True)
setup = '-setupplatformfinish' in unreal.SystemLibrary.get_command_line().lower()
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
bp = unreal.load_asset(ROOT + '/BP_ParametricPlatform')
assert bp

if setup:
    # The new switch's compatible default preserves old Blueprint radius overrides.
    rounded = unreal.load_asset(ROOT + '/Examples/BP_ParametricPlatform_RoundedU')
    if rounded:
        old_defaults = unreal.get_default_object(rounded.generated_class())
        assert old_defaults.get_editor_property('corner_radius') == 90
        assert old_defaults.get_editor_property('round_corners'), 'Legacy rounded class lost its silhouette'
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    assert unreal.EditorAssetLibrary.save_loaded_asset(bp, False)
    if rounded:
        unreal.BlueprintEditorLibrary.compile_blueprint(rounded)
        assert unreal.EditorAssetLibrary.save_loaded_asset(rounded, False)
    if not unreal.EditorAssetLibrary.does_asset_exist(CHILD):
        factory = unreal.BlueprintFactory()
        factory.set_editor_property('parent_class', bp.generated_class())
        child = unreal.AssetToolsHelpers.get_asset_tools().create_asset('BP_ParametricPlatform_Lobby', ROOT + '/Examples', unreal.Blueprint, factory)
        cdo = unreal.get_default_object(child.generated_class())
        cdo.set_editor_property('preset', unreal.PlatformPreset.LOBBY)
        cdo.apply_preset()
        unreal.BlueprintEditorLibrary.compile_blueprint(child)
        assert unreal.EditorAssetLibrary.save_loaded_asset(child, False)
    if not unreal.EditorAssetLibrary.does_asset_exist(MAP):
        world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
        world.get_world_settings().set_editor_property('default_game_mode', unreal.GameModeBase)
        cls = unreal.load_asset(CHILD).generated_class()
        cases = [('Sharp', False, False, unreal.PlatformMode.SOLID),
                 ('Horizontal', True, False, unreal.PlatformMode.SOLID),
                 ('Vertical', False, True, unreal.PlatformMode.CELLULAR),
                 ('Both', True, True, unreal.PlatformMode.CELLULAR)]
        for index, (label, horizontal, vertical, mode) in enumerate(cases):
            actor = actors.spawn_actor_from_class(cls, unreal.Vector((index % 2 - .5) * 2400, (index // 2 - .5) * 2100, 0))
            actor.set_actor_label('PlatformFinish_' + label)
            for key, value in {'shape_size':unreal.Vector2D(1800,1400), 'thickness':160.,
                               'corner_radius':220., 'round_corners':horizontal,
                               'bevel_size':20., 'bevel_edges':vertical, 'platform_mode':mode,
                               'react_to_music':False}.items():
                actor.set_editor_property(key, value)
            actor.rebuild_platform()
        for rotation, intensity in [(unreal.Rotator(pitch=-55,yaw=-30), 5), (unreal.Rotator(pitch=-25,yaw=140), 2)]:
            light = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0,0,4000), rotation)
            light.light_component.set_editor_property('intensity', intensity)
        assert unreal.EditorLoadingAndSavingUtils.save_map(world, MAP)
    unreal.log('PLATFORM_FINISH_SAVED')
else:
    native = unreal.get_default_object(unreal.ParametricPlatform)
    defaults = unreal.get_default_object(bp.generated_class())
    for key in ('bevel_edges', 'bevel_size', 'round_corners', 'corner_radius', 'body_material_index'):
        assert defaults.get_editor_property(key) == native.get_editor_property(key), key
    child = unreal.load_asset(CHILD)
    assert child
    cdo = unreal.get_default_object(child.generated_class())
    assert cdo.get_editor_property('bevel_edges') and cdo.get_editor_property('round_corners')
    assert cdo.get_editor_property('border_style') == unreal.PlatformBorder.RIM
    assert cdo.get_editor_property('body_material_index') == 2
    assert cdo.get_editor_property('pad_path_settings').get_editor_property('enable_spawning')
    rounded = unreal.load_asset(ROOT + '/Examples/BP_ParametricPlatform_RoundedU')
    if rounded:
        assert unreal.get_default_object(rounded.generated_class()).get_editor_property('round_corners')
    # Existing placed rounded overrides must keep their shape, without changing transforms.
    old_map = ROOT + '/Maps/L_ParametricPlatformBlockShapes'
    legacy_count = 0
    if unreal.EditorAssetLibrary.does_asset_exist(old_map):
        unreal.EditorLoadingAndSavingUtils.load_map(old_map)
        for actor in actors.get_all_level_actors():
            if isinstance(actor, unreal.ParametricPlatform) and actor.get_editor_property('corner_radius') > 0:
                assert actor.get_editor_property('round_corners'), actor.get_actor_label()
                legacy_count += 1
    world = unreal.EditorLoadingAndSavingUtils.load_map(MAP)
    assert world
    platforms = [a for a in actors.get_all_level_actors() if isinstance(a, unreal.ParametricPlatform)]
    assert len(platforms) == 4
    checks = []
    for actor in platforms:
        label = actor.get_actor_label().removeprefix('PlatformFinish_')
        assert actor.get_editor_property('round_corners') == (label in ('Horizontal','Both'))
        assert actor.get_editor_property('bevel_edges') == (label in ('Vertical','Both'))
        components = actor.get_components_by_class(unreal.DynamicMeshComponent)
        count = sum(c.get_dynamic_mesh().get_triangle_count() for c in components)
        assert count > 0, label
        assert (actor.get_editor_property('effective_bevel_size') > 0) == actor.get_editor_property('bevel_edges')
        checks.append({'label':label, 'triangles':count, 'effective_bevel':actor.get_editor_property('effective_bevel_size')})
    fresh = actors.spawn_actor_from_class(child.generated_class(), unreal.Vector(20000,0,0))
    assert fresh.get_editor_property('bevel_edges') and fresh.get_editor_property('round_corners')
    assert fresh.get_editor_property('effective_bevel_size') > 0 and len(fresh.get_pad_ids()) == 3
    actors.destroy_actor(fresh)
    (OUT / 'Persistence.json').write_text(json.dumps({'passed':True, 'fresh_process':True, 'legacy_rounded_instances':legacy_count, 'cases':checks}, indent=2))
    unreal.log('PLATFORM_FINISH_PERSISTENCE_OK')

    if '-nullrhi' not in unreal.SystemLibrary.get_command_line().lower():
        location = unreal.Vector(4200,-5600,6200)
        camera = actors.spawn_actor_from_class(unreal.SceneCapture2D, location,
            unreal.MathLibrary.find_look_at_rotation(location, unreal.Vector(0,0,0)))
        capture = camera.get_component_by_class(unreal.SceneCaptureComponent2D)
        for key, value in {'capture_every_frame':False, 'capture_on_movement':False,
                           'always_persist_rendering_state':True, 'fov_angle':49,
                           'capture_source':unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR}.items():
            capture.set_editor_property(key,value)
        post = capture.get_editor_property('post_process_settings')
        for key,value in {'override_auto_exposure_method':True,'auto_exposure_method':unreal.AutoExposureMethod.AEM_MANUAL,
                          'override_auto_exposure_apply_physical_camera_exposure':True,'auto_exposure_apply_physical_camera_exposure':False,
                          'override_auto_exposure_bias':True,'auto_exposure_bias':0}.items():
            post.set_editor_property(key,value)
        capture.set_editor_property('post_process_settings',post)
        target = unreal.RenderingLibrary.create_render_target2d(world,1800,1300,unreal.TextureRenderTargetFormat.RTF_RGBA8)
        capture.set_editor_property('texture_target',target)
        state={'next':time.monotonic()+12,'captures':0,'phase':0}
        def tick(_):
            if time.monotonic()<state['next']:
                return
            try:
                if state['captures']<4:
                    capture.capture_scene()
                    state.update(captures=state['captures']+1,next=time.monotonic()+1)
                    return
                unreal.RenderingLibrary.export_render_target(world,target,str(OUT),'FinishComparison.png' if state['phase']==0 else 'BeveledCorner.png')
                if state['phase']==0:
                    both = next(p for p in platforms if p.get_actor_label()=='PlatformFinish_Both')
                    for p in platforms:
                        p.set_actor_hidden_in_game(p != both)
                    focus = both.get_actor_location() + unreal.Vector(650,-550,-60)
                    close = both.get_actor_location() + unreal.Vector(1500,-1800,650)
                    camera.set_actor_location(close,False,False)
                    camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(close,focus),False)
                    state.update(phase=1,captures=0,next=time.monotonic()+1)
                    return
                unreal.log('PLATFORM_FINISH_RENDER_OK')
                unreal.unregister_slate_post_tick_callback(state['handle'])
                unreal.EditorPythonScripting.set_keep_python_script_alive(False)
            except Exception:
                unreal.unregister_slate_post_tick_callback(state['handle'])
                unreal.EditorPythonScripting.set_keep_python_script_alive(False)
                raise
        unreal.EditorPythonScripting.set_keep_python_script_alive(True)
        state['handle']=unreal.register_slate_post_tick_callback(tick)
