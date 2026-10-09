"""Render the support-hand contact and soft trail in an unsaved preview world."""
import json
import time
from pathlib import Path
import unreal

output = Path(unreal.Paths.project_saved_dir()) / 'Railgun/Preview'
output.mkdir(parents=True, exist_ok=True)
world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
player_cls = unreal.load_class(None, '/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C')
player = actors.spawn_actor_from_class(player_cls, unreal.Vector(0, 0, 100))
camera = player.get_component_by_class(unreal.CameraComponent)
camera.detach_from_component(unreal.DetachmentRule.KEEP_WORLD, unreal.DetachmentRule.KEEP_WORLD, unreal.DetachmentRule.KEEP_WORLD)
camera.set_world_location_and_rotation(unreal.Vector(0, 0, 170), unreal.Rotator(0, 0, 0), False, False)
gun = actors.spawn_actor_from_class(unreal.RailgunWeapon, unreal.Vector(0, 0, 0))
gun.equip(player)
gun.arms.set_editor_property('only_owner_see', False)
gun.weapon_mesh.set_editor_property('only_owner_see', False)
gun.arms.set_update_animation_in_editor(True)
for mesh in player.get_components_by_class(unreal.SkeletalMeshComponent):
    mesh.set_visibility(False)
for rotation, intensity in [(unreal.Rotator(-45, -30, 0), 7.0), (unreal.Rotator(-25, 150, 0), 3.0)]:
    light = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 500), rotation)
    light.light_component.set_editor_property('intensity', intensity * 8)
cube = unreal.load_asset('/Engine/BasicShapes/Cube')
for pos, scale in [(unreal.Vector(1000, 0, -5), unreal.Vector(35, 30, .1)),
                   (unreal.Vector(1700, 0, 250), unreal.Vector(.3, 20, 5))]:
    block = actors.spawn_actor_from_class(unreal.StaticMeshActor, pos)
    block.static_mesh_component.set_static_mesh(cube)
    block.set_actor_scale3d(scale)
    surface = block.static_mesh_component.create_dynamic_material_instance(0, unreal.load_asset('/Game/LevelPrototyping/Materials/M_FlatCol'))
    surface.set_vector_parameter_value('Base Color', unreal.LinearColor(.045, .05, .06, 1))
beam = actors.spawn_actor_from_class(unreal.RailgunBeam, unreal.Vector(0, 0, 0))
capture_actor = actors.spawn_actor_from_class(unreal.SceneCapture2D, camera.get_world_location(), camera.get_world_rotation())
capture = capture_actor.get_component_by_class(unreal.SceneCaptureComponent2D)
capture.set_editor_property('capture_every_frame', False)
capture.set_editor_property('always_persist_rendering_state', True)
capture.set_editor_property('capture_source', unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
capture.set_editor_property('fov_angle', 90.0)
capture.set_editor_property('enable_first_person_field_of_view', True)
capture.set_editor_property('first_person_field_of_view', 70.0)
capture.set_editor_property('enable_first_person_scale', True)
capture.set_editor_property('first_person_scale', .6)
target = unreal.RenderingLibrary.create_render_target2d(world, 1280, 720, unreal.TextureRenderTargetFormat.RTF_RGBA8)
capture.set_editor_property('texture_target', target)
post = capture.get_editor_property('post_process_settings')
post.set_editor_property('override_auto_exposure_method', True)
post.set_editor_property('auto_exposure_method', unreal.AutoExposureMethod.AEM_MANUAL)
post.set_editor_property('override_auto_exposure_bias', True)
post.set_editor_property('auto_exposure_bias', 10.0)
capture.set_editor_property('post_process_settings', post)
state = {'started': time.monotonic(), 'stage': 0}

def tick(delta):
    if time.monotonic() - state['started'] < 12:
        return
    try:
        if state['stage'] == 0:
            muzzle = gun.get_visual_muzzle_location()
            beam.initialize_beam(muzzle, unreal.Vector(1680, 0, 170), 2.0)
            beam.face_camera(capture_actor.get_actor_location())
            capture.capture_scene()
            state['stage'] = 1
            state['started'] = time.monotonic() - 11
            return
        if state['stage'] == 1:
            capture.capture_scene()
            unreal.RenderingLibrary.export_render_target(world, target, str(output), 'RailgunFirstPerson.png')
            (output / 'Attachment.json').write_text(json.dumps({
                'weapon_location': str(gun.weapon_mesh.get_world_location()),
                'weapon_rotation': str(gun.weapon_mesh.get_world_rotation()),
                'muzzle': str(gun.muzzle.get_world_location()),
                'arms': str(gun.arms.get_world_location()),
                'right_hand': str(gun.arms.get_socket_location('hand_r')),
                'left_hand': str(gun.arms.get_socket_location('hand_l')),
            }, indent=2))
            capture_actor.set_actor_location_and_rotation(unreal.Vector(65, -65, 165),
                unreal.MathLibrary.find_look_at_rotation(unreal.Vector(65, -65, 165), unreal.Vector(72, 8, 157)), False, False)
            capture.set_editor_property('enable_first_person_field_of_view', False)
            capture.set_editor_property('enable_first_person_scale', False)
            capture.set_editor_property('fov_angle', 40.0)
            beam.face_camera(capture_actor.get_actor_location())
            capture.capture_scene()
            state['stage'] = 2
            state['started'] = time.monotonic() - 10
            return
        if state['stage'] == 2:
            unreal.RenderingLibrary.export_render_target(world, target, str(output), 'RailgunGrip.png')
            capture_actor.set_actor_location_and_rotation(camera.get_world_location(), camera.get_world_rotation(), False, False)
            capture.set_editor_property('enable_first_person_field_of_view', True)
            capture.set_editor_property('enable_first_person_scale', True)
            capture.set_editor_property('fov_angle', 90.0)
            visuals = beam.get_editor_property('visuals')
            visuals.set_editor_property('color', unreal.LinearColor(.005, .25, 1, 1))
            visuals.set_editor_property('intensity', 2.0)
            visuals.set_editor_property('density', .4)
            beam.set_editor_property('visuals', visuals)
            beam.initialize_beam(gun.get_visual_muzzle_location(), unreal.Vector(1680, 0, 170), 2.0)
            beam.face_camera(capture_actor.get_actor_location())
            capture.capture_scene()
            state['stage'] = 3
            state['started'] = time.monotonic() - 10
            return
        unreal.RenderingLibrary.export_render_target(world, target, str(output), 'RailgunBlueLowDensity.png')
        unreal.log('RAILGUN_RENDER_OK')
    except Exception:
        unreal.log_error('RAILGUN_RENDER_FAILED')
        unreal.unregister_slate_post_tick_callback(state['handle'])
        unreal.EditorPythonScripting.set_keep_python_script_alive(False)
        raise
    finally:
        if state['stage'] == 3 and time.monotonic() - state['started'] >= 12:
            unreal.unregister_slate_post_tick_callback(state['handle'])
            unreal.EditorPythonScripting.set_keep_python_script_alive(False)

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state['handle'] = unreal.register_slate_post_tick_callback(tick)
