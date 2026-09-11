"""Render the saved armed blueprint in an unsaved preview world."""
import json
import time
from pathlib import Path
import unreal

output = Path(unreal.Paths.project_saved_dir()) / 'RadicalMikeWeapon/Preview'
output.mkdir(parents=True, exist_ok=True)
world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
cls = unreal.load_class(None, '/Game/Characters/RadicalMike/BP_RadicalMike_NPC.BP_RadicalMike_NPC_C')
npc = actors.spawn_actor_from_class(cls, unreal.Vector(0, 0, 96))
mesh = npc.mesh
anim_data = mesh.get_editor_property('animation_data')
anim_data.set_editor_property('anim_to_play', npc.fire_animation)
anim_data.set_editor_property('saved_position', 0.0)
anim_data.set_editor_property('saved_play_rate', 0.0)
anim_data.set_editor_property('saved_looping', False)
mesh.set_editor_property('animation_data', anim_data)
mesh.set_update_animation_in_editor(True)
mesh.play_animation(npc.fire_animation, False)
mesh.set_play_rate(0.0)
mesh.set_position(0.0, False)
for rotation, intensity in [(unreal.Rotator(-45, -30, 0), 5.0), (unreal.Rotator(-35, 150, 0), 2.0)]:
    light = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 500), rotation)
    light.light_component.set_editor_property('intensity', intensity * 8.0)
floor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, -6))
floor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
floor.set_actor_scale3d(unreal.Vector(12, 12, .1))
position = unreal.Vector(330, 430, 260)
camera = actors.spawn_actor_from_class(unreal.SceneCapture2D, position)
camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(position, unreal.Vector(0, 0, 95)), False)
capture = camera.get_component_by_class(unreal.SceneCaptureComponent2D)
capture.set_editor_property('projection_type', unreal.CameraProjectionMode.ORTHOGRAPHIC)
capture.set_editor_property('ortho_width', 330.0)
capture.set_editor_property('capture_every_frame', True)
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
state = {'started': time.monotonic()}


def tick(delta):
    if time.monotonic() - state['started'] < 12:
        return
    try:
        capture.capture_scene()
        unreal.RenderingLibrary.export_render_target(world, target, str(output), 'RadicalMikeArmed.png')
        weapon = npc.buster_weapon
        muzzle = npc.buster_muzzle.get_world_location()
        (output / 'attachment.json').write_text(json.dumps({
            'socket': str(weapon.get_attach_socket_name()), 'muzzle': str(muzzle),
            'weapon_location': str(weapon.get_world_location()),
            'weapon_rotation': str(weapon.get_world_rotation()),
            'mesh_rotation': str(mesh.get_world_rotation()),
            'animation': str(mesh.get_anim_instance().get_animation_asset()),
            'weapon_scale': str(weapon.get_world_scale()),
        }, indent=2))
        unreal.log('RADICAL_WEAPON_RENDER_OK')
    finally:
        unreal.unregister_slate_post_tick_callback(state['handle'])
        unreal.EditorPythonScripting.set_keep_python_script_alive(False)


unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state['handle'] = unreal.register_slate_post_tick_callback(tick)
