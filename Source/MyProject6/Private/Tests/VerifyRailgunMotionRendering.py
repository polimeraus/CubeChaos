"""Render production motion samples in an unsaved world; run RailgunMotion automation first."""
import csv
import json
import time
from pathlib import Path
import unreal

output = Path(unreal.Paths.project_saved_dir()) / 'Railgun/Motion'
frames = output / 'Frames'
frames.mkdir(parents=True, exist_ok=True)
with (output / 'Samples.csv').open(encoding='utf-8-sig') as stream:
    samples = list(csv.DictReader(stream))
for pose_time in (2, 4, 6, 7.9):
    samples.append(dict(samples[0], state='hold_pose', time=str(pose_time)))
if '-RailgunMotionQuick' in unreal.SystemLibrary.get_command_line():
    samples = [samples[index] for index in (0, 46, 66, 86, 106, 126, 146, 162)]
world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
player_cls = unreal.load_class(None, '/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C')
player = actors.spawn_actor_from_class(player_cls, unreal.Vector(0, 0, 100))
camera = player.get_component_by_class(unreal.CameraComponent)
camera.detach_from_component(unreal.DetachmentRule.KEEP_WORLD, unreal.DetachmentRule.KEEP_WORLD, unreal.DetachmentRule.KEEP_WORLD)
camera.set_world_location_and_rotation(unreal.Vector(0, 0, 170), unreal.Rotator(), False, False)
gun = actors.spawn_actor_from_class(unreal.RailgunWeapon, unreal.Vector())
gun.equip(player)
gun.arms.set_update_animation_in_editor(True)
gun.arms.stop()
options = unreal.AnimPoseEvaluationOptions()
options.set_editor_property('optional_skeletal_mesh', gun.arms.get_editor_property('skeletal_mesh_asset'))
pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(unreal.load_asset('/Game/Weapons/Railgun/A_RailgunHold'), 0, options)
reference = {}
for bone in ('lowerarm_l', 'lowerarm_r', 'hand_l', 'hand_r'):
    position = unreal.AnimPoseExtensions.get_ref_bone_pose(pose, bone, unreal.AnimPoseSpaces.WORLD).translation
    reference[bone] = [position.x, position.y, position.z]
    if bone.startswith('hand'):
        assert abs(position.x) > 45, f'Arm extension reaches wrist: {bone} {position}'
(output / 'ReferenceArms.json').write_text(json.dumps(reference, indent=2))
gun.arms.set_editor_property('only_owner_see', False)
gun.weapon_mesh.set_editor_property('only_owner_see', False)
for mesh in player.get_components_by_class(unreal.SkeletalMeshComponent):
    mesh.set_visibility(False)
for rotation, intensity in [(unreal.Rotator(-45, -30, 0), 56.0), (unreal.Rotator(-25, 150, 0), 24.0)]:
    light = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 500), rotation)
    light.light_component.set_editor_property('intensity', intensity)
cube = unreal.load_asset('/Engine/BasicShapes/Cube')
for pos, scale in [(unreal.Vector(1000, 0, -5), unreal.Vector(35, 30, .1)),
                   (unreal.Vector(1700, 0, 250), unreal.Vector(.3, 20, 5))]:
    block = actors.spawn_actor_from_class(unreal.StaticMeshActor, pos)
    block.static_mesh_component.set_static_mesh(cube)
    block.set_actor_scale3d(scale)
    surface = block.static_mesh_component.create_dynamic_material_instance(0, unreal.load_asset('/Game/LevelPrototyping/Materials/M_FlatCol'))
    surface.set_vector_parameter_value('Base Color', unreal.LinearColor(.045, .05, .06, 1))
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
capture.hide_actor_components(player)
target = unreal.RenderingLibrary.create_render_target2d(world, 960, 540, unreal.TextureRenderTargetFormat.RTF_RGBA8)
capture.set_editor_property('texture_target', target)
post = capture.get_editor_property('post_process_settings')
post.set_editor_property('override_auto_exposure_method', True)
post.set_editor_property('auto_exposure_method', unreal.AutoExposureMethod.AEM_MANUAL)
post.set_editor_property('override_auto_exposure_bias', True)
post.set_editor_property('auto_exposure_bias', 10.0)
capture.set_editor_property('post_process_settings', post)
pivot = unreal.Vector(45, 11, -16)
state = {'next': time.monotonic() + 12, 'index': 0, 'pending': False}

def finish():
    unreal.unregister_slate_post_tick_callback(state['handle'])
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)

def tick(delta):
    if time.monotonic() < state['next']:
        return
    try:
        sample = samples[state['index']]
        if state['pending']:
            name = f"{sample['state']}_{round(float(sample['time']) * 10):02d}.png"
            unreal.RenderingLibrary.export_render_target(world, target, str(frames), name)
            state['index'] += 1
            state['pending'] = False
            if state['index'] == len(samples):
                unreal.log('RAILGUN_MOTION_RENDER_OK')
                finish()
                return
            sample = samples[state['index']]
        angles = unreal.Rotator(pitch=float(sample['pitch']), yaw=float(sample['yaw']), roll=float(sample['roll']))
        offset = unreal.Vector(float(sample['x']), float(sample['y']), float(sample['z']))
        gun.arms.set_position(float(sample['time']), False)
        location = offset + pivot - unreal.MathLibrary.quat_rotate_vector(angles.quaternion(), pivot)
        gun.set_actor_relative_location(location, False, False)
        gun.set_actor_relative_rotation(angles, False, False)
        # The blank editor world does not tick deferred captures. An explicit capture
        # also flushes mesh/material updates after shader compilation and pose changes.
        capture.capture_scene()
        state['pending'] = True
        state['next'] = time.monotonic() + .1
    except Exception:
        unreal.log_error('RAILGUN_MOTION_RENDER_FAILED')
        finish()
        raise

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state['handle'] = unreal.register_slate_post_tick_callback(tick)
