"""Render a destroyed cone from the interior in a disposable editor world."""
import json
import math
import time
from pathlib import Path
import unreal

output = Path(unreal.Paths.project_saved_dir()) / "Analysis/ConeSurfaceReview"
output.mkdir(parents=True, exist_ok=True)
world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
cone = actors.spawn_actor_from_class(unreal.ChunkedDestructibleConeStructure, unreal.Vector())
for key, value in {
    "cone_animation_type": unreal.ConeStructureAnimationType.NONE,
    "generate_entrance": False, "scatter_destroyed_pieces": False,
    "play_destroy_tick_sound": False, "flash_duration": 0.0,
    "generate_platforms": True, "platform_style": unreal.ConeStructurePlatformStyle.RISING_SPIRAL,
    "platform_spiral_turns": 1.25, "spiral_segments_per_turn": 64,
    "lowest_platform_height_blocks": 2, "spiral_end_height_blocks": 9,
    "platform_thickness_blocks": 2, "platform_overhang_blocks": 3.0,
    "taper_platforms_upward": False, "platform_spiral_clockwise": True,
}.items():
    cone.set_editor_property(key, value)
cone.configure_cone_structure(12, 5, 2, .8, unreal.Vector(100, 100, 100), unreal.Vector(), False)
cone.set_react_to_music(False)
angle = -math.tau * 1.25 / 3.0
radial = unreal.Vector(math.cos(angle), math.sin(angle), 0)
focus = radial * 380 + unreal.Vector(0, 0, 530)
camera = actors.spawn_actor_from_class(unreal.SceneCapture2D, unreal.Vector(0, 0, 540))
camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(camera.get_actor_location(), focus), False)
capture = camera.get_component_by_class(unreal.SceneCaptureComponent2D)
capture.set_editor_property("capture_every_frame", False)
capture.set_editor_property("capture_on_movement", False)
capture.set_editor_property("fov_angle", 85)
capture.set_editor_property("capture_source", unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
capture.set_editor_property("always_persist_rendering_state", True)
unreal.SystemLibrary.execute_console_command(world, "r.MotionBlurQuality 0")
post = capture.get_editor_property("post_process_settings")
post.set_editor_property("override_auto_exposure_method", True)
post.set_editor_property("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL)
post.set_editor_property("override_auto_exposure_apply_physical_camera_exposure", True)
post.set_editor_property("auto_exposure_apply_physical_camera_exposure", False)
post.set_editor_property("override_auto_exposure_bias", True)
post.set_editor_property("auto_exposure_bias", 0)
capture.set_editor_property("post_process_settings", post)
target = unreal.RenderingLibrary.create_render_target2d(world, 960, 720, unreal.TextureRenderTargetFormat.RTF_RGBA8)
capture.set_editor_property("texture_target", target)
for position in (unreal.Vector(0, 0, 620), focus + unreal.Vector(0, 0, 250)):
    light = actors.spawn_actor_from_class(unreal.PointLight, position)
    light.point_light_component.set_editor_property("intensity", 40)
    light.point_light_component.set_editor_property("attenuation_radius", 2500)
sun = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 1500))
sun.set_actor_rotation(unreal.Rotator(pitch=-45, yaw=40), False)
sun.light_component.set_editor_property("intensity", 3)
tangent = unreal.Vector(-radial.y, radial.x, 0)
views = [
    ("interior", unreal.Vector(0, 0, 540), focus),
    ("interior_left", tangent * 70 + unreal.Vector(0, 0, 540), focus),
    ("interior_right", tangent * -70 + unreal.Vector(0, 0, 570), focus),
    ("exterior", radial * 1000 + unreal.Vector(0, 0, 750), focus),
    ("upper_exterior", radial * 750 + unreal.Vector(0, 0, 1150), radial * 250 + unreal.Vector(0, 0, 925)),
]
state = {"started": time.monotonic(), "stage": "damage", "view": 0, "rotation_samples": []}

def finish(error=None):
    unreal.unregister_slate_post_tick_callback(state["handle"])
    (output / "result.json").write_text(json.dumps({"passed": error is None, "error": error,
        "destroyed_cells": state.get("destroyed", 0),
        "rotation_frames": len(state["rotation_samples"]),
        "max_repeated_view_pixel_error": state.get("max_pixel_error", 0)}, indent=2))
    (unreal.log_error if error else unreal.log)("CONE_INTERIOR_RENDER " + str(error or "OK"))
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)

def tick(_delta):
    try:
        elapsed = time.monotonic() - state["started"]
        if elapsed > 100:
            raise RuntimeError("Rendering timed out")
        if state["stage"] == "damage" and elapsed > 6:
            cells = set()
            # Open the wall across the spiral attachment, using normal cell destruction.
            for z in list(range(350, 751, 100)) + [850, 950]:
                for delta in (-.35, -.18, 0, .18, .35):
                    r = 500 - z * .25 - 30
                    p = unreal.Vector(math.cos(angle + delta) * r, math.sin(angle + delta) * r, z)
                    cell = cone.world_location_to_cell(p)
                    if cell is not None:
                        cells.add((cell.x, cell.y))
            state["destroyed"] = sum(bool(cone.destroy_cell(unreal.IntPoint(x, y))) for x, y in cells)
            if state["destroyed"] == 0:
                raise RuntimeError("No wall cells were destroyed")
            state.update(stage="capture", changed=time.monotonic())
        elif state["stage"] == "capture" and time.monotonic() - state["changed"] > 6:
            capture.capture_scene()
            name = views[state["view"]][0]
            unreal.RenderingLibrary.export_render_target(world, target, str(output), name + ".png")
            state["view"] += 1
            if state["view"] == len(views):
                # Repeat a camera sweep over a static damaged wall. A view must not
                # intermittently acquire/discard surfaces depending on the preceding frame.
                rotation_target = unreal.RenderingLibrary.create_render_target2d(
                    world, 192, 144, unreal.TextureRenderTargetFormat.RTF_RGBA8)
                capture.set_editor_property("texture_target", rotation_target)
                camera.set_actor_location(unreal.Vector(0, 0, 540), False, False)
                state.update(stage="rotate", rotation_target=rotation_target, frame=0)
            else:
                _, position, target_position = views[state["view"]]
                camera.set_actor_location(position, False, False)
                camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(position, target_position), False)
                state["changed"] = time.monotonic()
        elif state["stage"] == "rotate":
            frame = state["frame"]
            yaw = math.degrees(angle) + 35 * math.sin(math.tau * (frame % 24) / 24)
            camera.set_actor_rotation(unreal.Rotator(pitch=0, yaw=yaw), False)
            capture.capture_scene()
            pixels = unreal.RenderingLibrary.read_render_target(world, state["rotation_target"], False)
            if isinstance(pixels, tuple):
                pixels = next(value for value in pixels if not isinstance(value, bool))
            sample = bytes(channel for pixel in pixels for channel in (pixel.r, pixel.g, pixel.b))
            state["rotation_samples"].append(sample)
            if frame >= 24:
                reference = state["rotation_samples"][frame - 24]
                error = sum(abs(a - b) for a, b in zip(reference, sample)) / len(sample)
                state["max_pixel_error"] = max(state.get("max_pixel_error", 0), error)
                if error > 1.0:
                    raise RuntimeError(f"Repeated camera angle changed unexpectedly: frame={frame}, error={error}")
            state["frame"] += 1
            if state["frame"] == 48:
                finish()
    except Exception as error:
        finish(str(error))

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state["handle"] = unreal.register_slate_post_tick_callback(tick)
