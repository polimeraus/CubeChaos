"""Render the vertical strip along the spiral and its stairs; no asset writes."""
import json
import math
import time
from pathlib import Path
import unreal

out = Path(unreal.Paths.project_saved_dir()).resolve() / 'SpiralTower/GlowStart/Render'
out.mkdir(parents=True, exist_ok=True)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
world = unreal.EditorLoadingAndSavingUtils.load_map('/Game/Structures/Destructible/SpiralTower/Maps/Lvl_SpiralTowerPreview')
tower = next(a for a in actors.get_all_level_actors() if isinstance(a, unreal.ChunkedDestructibleSpiralTower))
camera = actors.spawn_actor_from_class(unreal.SceneCapture2D, unreal.Vector(), transient=True)
capture = camera.get_component_by_class(unreal.SceneCaptureComponent2D)
for key, value in dict(capture_every_frame=False, capture_on_movement=False, always_persist_rendering_state=True,
                       capture_source=unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR,
                       projection_type=unreal.CameraProjectionMode.ORTHOGRAPHIC, ortho_width=5000.).items():
    capture.set_editor_property(key, value)
post = capture.get_editor_property('post_process_settings')
for key, value in dict(override_auto_exposure_method=True, auto_exposure_method=unreal.AutoExposureMethod.AEM_MANUAL,
                       override_auto_exposure_apply_physical_camera_exposure=True,
                       auto_exposure_apply_physical_camera_exposure=False,
                       override_auto_exposure_bias=True, auto_exposure_bias=0.,
                       override_bloom_intensity=True, bloom_intensity=.5).items():
    post.set_editor_property(key, value)
capture.set_editor_property('post_process_settings', post)
target = unreal.RenderingLibrary.create_render_target2d(world, 1400, 1800, unreal.TextureRenderTargetFormat.RTF_RGBA8)
capture.set_editor_property('texture_target', target)
camera.set_actor_location(unreal.Vector(6500, -10000, 6500), False, False)
camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(camera.get_actor_location(), unreal.Vector(0, 0, 2200)), False)
pad = tower.ground_launch_pad.get_editor_property('child_actor')
pad.set_editor_property('animation_speed', 0.)
pad.refresh_pad()
def at_turn(turn):
    pitch = (tower.top_radius - tower.base_radius) / tower.turns
    def speed(t):
        radius = tower.base_radius - (tower.walkway_width - tower.wall_thickness) * .5 + pitch * t
        rise = tower.rise_per_turn * (1 - (1 - tower.top_rise_scale) * t / tower.turns)
        return math.sqrt(pitch ** 2 + (math.tau * radius) ** 2 + rise ** 2)
    u = min(turn, tower.turns)
    distance = u / 48 * (speed(0) + speed(u) + sum(
        (4 if i % 2 else 2) * speed(u * i / 16) for i in range(1, 16)))
    summit_radius = tower.top_radius - (tower.walkway_width - tower.wall_thickness) * .5
    distance += max(0., turn - tower.turns) * math.sqrt(
        (math.tau * summit_radius) ** 2 + (tower.rise_per_turn * tower.top_rise_scale) ** 2)
    return (distance + tower.glow_trail_length * .5) / tower.glow_speed


duration = (tower.generated_glow_path_length + tower.glow_trail_length) / tower.glow_speed
views = [('Spiral_Normal', 0.), ('Spiral_Start', at_turn(.1)), ('Spiral_Lower', at_turn(.9)),
         ('Spiral_SecondTurn', at_turn(1.9)), ('Spiral_Middle', at_turn(2.9)),
         ('Spiral_Next', at_turn(3.1)), ('Spiral_Upper', at_turn(6.9)),
         ('Spiral_Returned', duration + .2), ('Spiral_Stairs', at_turn(2.9)),
         ('Spiral_AutoA', -1.), ('Spiral_AutoB', -1.)]
state = dict(index=0, captures=0, export=False, next=time.monotonic()+12, start=time.monotonic(), results=[])


def finish(error=None):
    unreal.unregister_slate_post_tick_callback(state['handle'])
    (out / 'Result.json').write_text(json.dumps(dict(passed=error is None, error=error, views=state['results']), indent=2))
    if error:
        unreal.log_error('TOWER_FEATURES_RENDER_FAILED ' + str(error))
    else:
        unreal.log('TOWER_FEATURES_RENDER_OK')
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)


def tick(_delta):
    if state.get('busy') or time.monotonic() < state['next']:
        return
    state['busy'] = True
    try:
        assert time.monotonic() - state['start'] < 240, 'Render timeout'
        if state['captures']:
            capture.capture_scene()
            state['captures'] -= 1
            state.update(export=state['captures'] == 0, next=time.monotonic()+.3)
            return
        if state['export']:
            name, phase = views[state['index']]
            unreal.RenderingLibrary.export_render_target(world, target, str(out), name+'.png')
            state['results'].append(dict(view=name, phase=phase))
            state['index'] += 1
            state['export'] = False
            if state['index'] == len(views):
                finish()
                return
        name, phase = views[state['index']]
        tower.set_editor_property('glow_preview_time', phase)
        tower.refresh_glow_animation()
        if name == 'Spiral_AutoA':
            mid = tower.get_components_by_class(unreal.DynamicMeshComponent)[0].get_material(2)
            mid.set_scalar_parameter_value('StartTime', unreal.GameplayStatics.get_time_seconds(world) - at_turn(2.65))
        if name == 'Spiral_Stairs':
            capture.set_editor_property('ortho_width', 1700.)
            camera.set_actor_location(unreal.Vector(6500, -10000, 6500), False, False)
            camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(
                camera.get_actor_location(), unreal.Vector(0, -950, 2200)), False)
        elif name.startswith('Spiral_Auto'):
            capture.set_editor_property('ortho_width', 5000.)
            camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(
                camera.get_actor_location(), unreal.Vector(0, 0, 2200)), False)
        state.update(captures=8, next=time.monotonic()+1)
    except Exception as error:
        finish(repr(error))
        raise
    finally:
        state['busy'] = False


unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state['handle'] = unreal.register_slate_post_tick_callback(tick)
