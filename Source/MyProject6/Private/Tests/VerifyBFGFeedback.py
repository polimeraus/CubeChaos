"""Render real BFG automation samples in a nearby room, without changing saved levels."""
import csv
import json
import time
from pathlib import Path
import unreal

out = Path(unreal.Paths.project_saved_dir()).resolve() / 'BFGFeedback'
with (out / 'Samples.csv').open(encoding='utf-8-sig') as stream:
    samples = list(csv.DictReader(stream))
frames = [('Idle', '-1.000'), ('Flash', '0.016'), ('Recoil', '0.048'), ('Recovered', '0.240')]
defaults = unreal.get_default_object(unreal.RailgunWeapon)
assert defaults.bfg_recoil_strength == 2.
assert defaults.bfg_muzzle_flash_intensity == 18000
assert defaults.bfg_muzzle_flash_radius == 650
assert abs(defaults.bfg_muzzle_flash_duration - .12) < .001
assert defaults.bfg_muzzle_flash_color.g > .99
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
player_class = unreal.load_class(None, '/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C')
player = actors.spawn_actor_from_class(player_class, unreal.Vector(0, 0, 100))
camera = player.get_component_by_class(unreal.CameraComponent)
camera.detach_from_component(unreal.DetachmentRule.KEEP_WORLD, unreal.DetachmentRule.KEEP_WORLD, unreal.DetachmentRule.KEEP_WORLD)
camera.set_world_location_and_rotation(unreal.Vector(0, 0, 170), unreal.Rotator(), False, False)
gun = actors.spawn_actor_from_class(unreal.RailgunWeapon, unreal.Vector())
gun.equip(player)
gun.activate_bfg()
gun.arms.set_update_animation_in_editor(True)
gun.arms.stop()
gun.arms.set_editor_property('only_owner_see', False)
gun.weapon_mesh.set_editor_property('only_owner_see', False)
for mesh in player.get_components_by_class(unreal.SkeletalMeshComponent):
    mesh.set_visibility(False)
for rotation, intensity in [(unreal.Rotator(pitch=-45, yaw=-30), 12.), (unreal.Rotator(pitch=-25, yaw=150), 5.)]:
    light = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 500), rotation)
    light.light_component.set_editor_property('intensity', intensity)
cube = unreal.load_asset('/Engine/BasicShapes/Cube')
for pos, scale in [(unreal.Vector(350, 0, -5), unreal.Vector(12, 10, .1)),
                   (unreal.Vector(430, 0, 200), unreal.Vector(.15, 10, 4)),
                   (unreal.Vector(350, -280, 100), unreal.Vector(12, .15, 2)),
                   (unreal.Vector(350, 280, 100), unreal.Vector(12, .15, 2))]:
    block = actors.spawn_actor_from_class(unreal.StaticMeshActor, pos)
    block.static_mesh_component.set_static_mesh(cube)
    block.set_actor_scale3d(scale)
    surface = block.static_mesh_component.create_dynamic_material_instance(0, unreal.load_asset('/Game/LevelPrototyping/Materials/M_FlatCol'))
    surface.set_vector_parameter_value('Base Color', unreal.LinearColor(.22, .24, .27, 1))
capture_actor = actors.spawn_actor_from_class(unreal.SceneCapture2D, camera.get_world_location(), camera.get_world_rotation())
capture = capture_actor.get_component_by_class(unreal.SceneCaptureComponent2D)
for key, value in {'capture_every_frame': False, 'always_persist_rendering_state': True,
                   'capture_source': unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR, 'fov_angle': 90.,
                   'enable_first_person_field_of_view': True, 'first_person_field_of_view': 70.,
                   'enable_first_person_scale': True, 'first_person_scale': .6}.items():
    capture.set_editor_property(key, value)
capture.hide_actor_components(player)
target = unreal.RenderingLibrary.create_render_target2d(world, 1280, 720, unreal.TextureRenderTargetFormat.RTF_RGBA8)
capture.set_editor_property('texture_target', target)
post = capture.get_editor_property('post_process_settings')
for key, value in {'override_auto_exposure_method': True, 'auto_exposure_method': unreal.AutoExposureMethod.AEM_MANUAL,
                   'override_auto_exposure_bias': True, 'auto_exposure_bias': 10.}.items():
    post.set_editor_property(key, value)
capture.set_editor_property('post_process_settings', post)
state = {'next': time.monotonic() + 12, 'index': 0, 'pending': False}

def finish():
    unreal.unregister_slate_post_tick_callback(state['handle'])
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)

def tick(delta):
    if time.monotonic() < state['next']:
        return
    try:
        name, sample_time = frames[state['index']]
        if state['pending']:
            unreal.RenderingLibrary.export_render_target(world, target, str(out), name + '.png')
            state['index'] += 1
            if state['index'] == len(frames):
                (out / 'Verification.json').write_text(json.dumps({
                    'fresh_process_defaults_verified': True,
                    'recoil_strength': defaults.bfg_recoil_strength,
                    'flash_lumens': defaults.bfg_muzzle_flash_intensity,
                    'flash_radius_cm': defaults.bfg_muzzle_flash_radius,
                    'flash_duration_s': defaults.bfg_muzzle_flash_duration,
                    'frames_from_actual_fire_bfg': [name for name, _ in frames]}, indent=2))
                unreal.log('BFG_FEEDBACK_VERIFY_OK')
                finish()
                return
            name, sample_time = frames[state['index']]
        sample = next(s for s in samples if s['time'] == sample_time)
        position = unreal.Vector(*[float(sample[key]) for key in ('x', 'y', 'z')])
        rotation = unreal.Rotator(pitch=float(sample['pitch']), yaw=float(sample['yaw']), roll=float(sample['roll']))
        gun.set_actor_relative_location(position, False, False)
        gun.set_actor_relative_rotation(rotation, False, False)
        gun.bfg_muzzle_light.set_world_location(gun.get_visual_muzzle_location(), False, False)
        gun.bfg_muzzle_light.set_light_color(defaults.bfg_muzzle_flash_color)
        gun.bfg_muzzle_light.set_attenuation_radius(defaults.bfg_muzzle_flash_radius)
        gun.bfg_muzzle_light.set_intensity(float(sample['light']))
        capture.capture_scene()
        state['pending'] = True
        state['next'] = time.monotonic() + .35
    except Exception:
        unreal.log_error('BFG_FEEDBACK_VERIFY_FAILED')
        finish()
        raise

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state['handle'] = unreal.register_slate_post_tick_callback(tick)
