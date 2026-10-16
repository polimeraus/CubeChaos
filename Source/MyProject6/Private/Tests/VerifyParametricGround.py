"""Fresh-process Blueprint/default checks and an offscreen preview of all three ground shapes."""
import json
import time
from pathlib import Path
import unreal

output = Path(unreal.Paths.project_saved_dir()) / 'ParametricGround'
output.mkdir(parents=True, exist_ok=True)
blueprint = unreal.load_asset('/Game/GameplayActors/Ground/BP_ParametricGround')
assert blueprint, 'Saved Blueprint is missing'
defaults = unreal.get_default_object(blueprint.generated_class())
native = unreal.get_default_object(unreal.ParametricGround)
for key in ('shape', 'size', 'thickness', 'circle_segments', 'boundary_inset', 'boundary_width',
            'boundary_color', 'ground_color', 'ground_material', 'boundary_material', 'enable_collision'):
    assert defaults.get_editor_property(key) == native.get_editor_property(key), key
assert defaults.get_editor_property('thickness') == 50
assert defaults.get_editor_property('shape') == unreal.ParametricGroundShape.CIRCLE
assert defaults.get_editor_property('ground_material')

world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
shapes = [unreal.ParametricGroundShape.CIRCLE, unreal.ParametricGroundShape.SQUARE, unreal.ParametricGroundShape.HEXAGON]
result = {'fresh_process_saved_blueprint_verified': True, 'shapes': []}
for index, shape in enumerate(shapes):
    ground = actors.spawn_actor_from_class(blueprint.generated_class(), unreal.Vector((index - 1) * 3000, 0, 0))
    ground.set_editor_property('shape', shape)
    ground.set_editor_property('size', 2200)
    ground.set_editor_property('boundary_inset', 140)
    ground.set_editor_property('boundary_width', 75)
    ground.rebuild_ground()
    component = ground.get_dynamic_mesh_component()
    assert component.get_dynamic_mesh().get_triangle_count() > 0
    tint = component.get_material(1).get_vector_parameter_value('Color')
    expected = ground.get_editor_property('boundary_color')
    assert all(abs(getattr(tint, c) - getattr(expected, c)) < .001 for c in ('r', 'g', 'b'))
    result['shapes'].append({'shape': str(shape), 'triangles': component.get_dynamic_mesh().get_triangle_count()})

for rotation, intensity in [(unreal.Rotator(pitch=-55, yaw=-25), 5), (unreal.Rotator(pitch=-25, yaw=140), 2)]:
    light = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 4000), rotation)
    light.light_component.set_editor_property('intensity', intensity)
position = unreal.Vector(0, -7600, 10500)
camera = actors.spawn_actor_from_class(unreal.SceneCapture2D, position,
    unreal.MathLibrary.find_look_at_rotation(position, unreal.Vector(0, 0, 0)))
capture = camera.get_component_by_class(unreal.SceneCaptureComponent2D)
for key, value in {'capture_every_frame': False, 'capture_on_movement': False,
                   'always_persist_rendering_state': True, 'fov_angle': 48,
                   'capture_source': unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR}.items():
    capture.set_editor_property(key, value)
post = capture.get_editor_property('post_process_settings')
for key, value in {'override_auto_exposure_method': True, 'auto_exposure_method': unreal.AutoExposureMethod.AEM_MANUAL,
                   'override_auto_exposure_apply_physical_camera_exposure': True, 'auto_exposure_apply_physical_camera_exposure': False,
                   'override_auto_exposure_bias': True, 'auto_exposure_bias': 0}.items():
    post.set_editor_property(key, value)
capture.set_editor_property('post_process_settings', post)
target = unreal.RenderingLibrary.create_render_target2d(world, 1440, 720, unreal.TextureRenderTargetFormat.RTF_RGBA8)
capture.set_editor_property('texture_target', target)
state = {'next': time.monotonic() + 15, 'captures': 0}

def stop():
    unreal.unregister_slate_post_tick_callback(state['handle'])
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)

def tick(_):
    if time.monotonic() < state['next']:
        return
    try:
        if state['captures'] < 4:
            capture.capture_scene()
            state.update(captures=state['captures'] + 1, next=time.monotonic() + 1)
            return
        unreal.RenderingLibrary.export_render_target(world, target, str(output), 'Shapes.png')
        (output / 'Verification.json').write_text(json.dumps(result, indent=2))
        unreal.log('PARAMETRIC_GROUND_VERIFIED ' + json.dumps(result))
        stop()
    except Exception:
        stop()
        raise

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state['handle'] = unreal.register_slate_post_tick_callback(tick)
