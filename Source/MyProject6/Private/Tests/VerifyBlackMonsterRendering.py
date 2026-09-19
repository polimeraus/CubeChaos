"""Render idle, run and claw poses in an unsaved preview world."""
import json
import time
from pathlib import Path
import unreal

root = '/Game/Characters/black-monster'
output = Path(unreal.Paths.project_saved_dir()) / 'BlackMonster/ClawPreview'
output.mkdir(parents=True, exist_ok=True)
world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
cls = unreal.load_class(None, root + '/BP_BlackMonster_Follower.BP_BlackMonster_Follower_C')
npc = actors.spawn_actor_from_class(cls, unreal.Vector(0, 0, 100))
mesh = npc.mesh
mesh.set_update_animation_in_editor(True)
for rotation, intensity in [(unreal.Rotator(pitch=-40, yaw=-30, roll=0), 45), (unreal.Rotator(pitch=-35, yaw=150, roll=0), 30)]:
    light = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 500), rotation)
    light.light_component.set_editor_property('intensity', intensity)
floor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, -6))
floor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
floor.set_actor_scale3d(unreal.Vector(12, 12, .1))
backdrop = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(-350, 0, 300))
backdrop.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
backdrop.set_actor_scale3d(unreal.Vector(.1, 24, 12))
position = unreal.Vector(440, 300, 260)
camera = actors.spawn_actor_from_class(unreal.SceneCapture2D, position)
camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(position, unreal.Vector(0, 0, 145)), False)
capture = camera.get_component_by_class(unreal.SceneCaptureComponent2D)
capture.set_editor_property('projection_type', unreal.CameraProjectionMode.ORTHOGRAPHIC)
capture.set_editor_property('ortho_width', 440.0)
capture.set_editor_property('capture_every_frame', False)
capture.set_editor_property('always_persist_rendering_state', True)
capture.set_editor_property('capture_source', unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
target = unreal.RenderingLibrary.create_render_target2d(world, 800, 800, unreal.TextureRenderTargetFormat.RTF_RGBA8)
capture.set_editor_property('texture_target', target)
post = capture.get_editor_property('post_process_settings')
post.set_editor_property('override_auto_exposure_method', True)
post.set_editor_property('auto_exposure_method', unreal.AutoExposureMethod.AEM_MANUAL)
post.set_editor_property('override_auto_exposure_bias', True)
post.set_editor_property('auto_exposure_bias', 10.0)
capture.set_editor_property('post_process_settings', post)
poses = [('Claw', frame/20) for frame in range(40)]
state = {'index': 0, 'since': time.monotonic(), 'report': []}


def pose():
    name, seconds = poses[state['index']]
    animation = unreal.load_asset(root + '/Animations/Anim_BlackMonster_' + name)
    data = mesh.get_editor_property('animation_data')
    data.set_editor_property('anim_to_play', animation)
    data.set_editor_property('saved_position', seconds)
    data.set_editor_property('saved_play_rate', 0.0)
    data.set_editor_property('saved_looping', False)
    mesh.set_editor_property('animation_data', data)
    mesh.play_animation(animation, False)
    mesh.set_play_rate(0)
    mesh.set_position(seconds, False)


def tick(delta):
    if time.monotonic() - state['since'] < (12 if state['index'] == 0 else .5):
        return
    try:
        name, seconds = poses[state['index']]
        capture.capture_scene()
        filename = f'{state["index"]}_{name}.png'
        unreal.RenderingLibrary.export_render_target(world, target, str(output), filename)
        state['report'].append({'image': filename, 'seconds': seconds, 'head': str(mesh.get_socket_location('Head')),
                                'hand': str(mesh.get_socket_location('RightHand')),
                                'left_hand': str(mesh.get_socket_location('LeftHand')),
                                'foot': str(mesh.get_socket_location('LeftToeBase'))})
        state['index'] += 1
        if state['index'] == len(poses):
            (output / 'poses.json').write_text(json.dumps(state['report'], indent=2))
            unreal.log('BLACK_MONSTER_RENDER_OK')
            unreal.unregister_slate_post_tick_callback(state['handle'])
            unreal.EditorPythonScripting.set_keep_python_script_alive(False)
        else:
            state['since'] = time.monotonic()
            pose()
    except Exception:
        unreal.unregister_slate_post_tick_callback(state['handle'])
        unreal.EditorPythonScripting.set_keep_python_script_alive(False)
        raise


pose()
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state['handle'] = unreal.register_slate_post_tick_callback(tick)
