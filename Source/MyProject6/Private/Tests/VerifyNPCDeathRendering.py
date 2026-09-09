"""Capture the Ghoul's death pose and fade shaders in an unsaved preview world."""
import json
import time
from pathlib import Path
import unreal

output = Path(unreal.Paths.project_saved_dir()) / 'Analysis/NPCDeathRendering'
output.mkdir(parents=True, exist_ok=True)
world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
npc = actors.spawn_actor_from_class(unreal.ChasingNPC, unreal.Vector(0, 0, 96))
mesh = npc.mesh
animation = npc.get_editor_property('death_animation')
mesh.set_update_animation_in_editor(True)
mesh.play_animation(animation, False)
mesh.set_play_rate(0.0)
materials = []
for index, source in enumerate(npc.get_editor_property('death_fade_materials')):
    material = mesh.create_dynamic_material_instance(index, source)
    material.set_scalar_parameter_value('NPCDeathOpacity', 1.0)
    materials.append(material)

light = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 500), unreal.Rotator(-45, -30, 0))
light.light_component.set_editor_property('intensity', 5.0)
fill = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 500), unreal.Rotator(-35, 150, 0))
fill.light_component.set_editor_property('intensity', 2.0)
floor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, -6))
floor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
floor.set_actor_scale3d(unreal.Vector(12, 12, .1))

position = unreal.Vector(340, -410, 280)
camera = actors.spawn_actor_from_class(unreal.SceneCapture2D, position)
camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(position, unreal.Vector(0, 0, 65)), False)
capture = camera.get_component_by_class(unreal.SceneCaptureComponent2D)
capture.set_editor_property('projection_type', unreal.CameraProjectionMode.ORTHOGRAPHIC)
capture.set_editor_property('ortho_width', 360.0)
capture.set_editor_property('capture_every_frame', True)
capture.set_editor_property('always_persist_rendering_state', True)
capture.set_editor_property('capture_source', unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
target = unreal.RenderingLibrary.create_render_target2d(world, 640, 640, unreal.TextureRenderTargetFormat.RTF_RGBA8)
capture.set_editor_property('texture_target', target)
post = capture.get_editor_property('post_process_settings')
post.set_editor_property('override_auto_exposure_method', True)
post.set_editor_property('auto_exposure_method', unreal.AutoExposureMethod.AEM_MANUAL)
post.set_editor_property('override_auto_exposure_bias', True)
post.set_editor_property('auto_exposure_bias', 10.0)
capture.set_editor_property('post_process_settings', post)

cases = [('start', 0.0, 1.0), ('fallen', 1.1, 1.0), ('half_fade', 1.1, .5), ('gone', 1.1, 0.0)]
state = dict(index=0, started=time.monotonic(), since=time.monotonic(), prepared=False)
results = {}


def finish(error=None):
    unreal.unregister_slate_post_tick_callback(state['handle'])
    results.update(passed=error is None, error=error)
    (output / 'result.json').write_text(json.dumps(results, indent=2))
    if error:
        unreal.log_error('NPC_DEATH_RENDER_FAILED ' + error)
    else:
        unreal.log('NPC_DEATH_RENDER_OK')
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)


def tick(delta):
    try:
        now = time.monotonic()
        if now - state['started'] > 150:
            raise RuntimeError('Preview timeout')
        if now - state['started'] < 10:
            return
        name, position, opacity = cases[state['index']]
        if not state['prepared']:
            mesh.set_position(position, False)
            for material in materials:
                material.set_scalar_parameter_value('NPCDeathOpacity', opacity)
            state.update(prepared=True, since=now)
            return
        if now - state['since'] < 3:
            return
        capture.capture_scene()
        unreal.RenderingLibrary.export_render_target(world, target, str(output), name + '.png')
        results[name] = dict(position=position, opacity=opacity)
        state.update(index=state['index'] + 1, prepared=False)
        if state['index'] == len(cases):
            finish()
    except Exception as error:
        finish(str(error))


unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state['handle'] = unreal.register_slate_post_tick_callback(tick)
