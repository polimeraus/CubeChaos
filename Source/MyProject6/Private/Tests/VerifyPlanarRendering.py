"""Disposable real-RHI capture: verifies moving light on an unchanged clipped surface."""
import json
import time
from pathlib import Path
import unreal

output = Path(unreal.Paths.project_saved_dir()) / "Analysis" / "PlanarRendering"
output.mkdir(parents=True, exist_ok=True)
world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
floor = actors.spawn_actor_from_class(unreal.ChunkedDestructibleTriangle, unreal.Vector())
floor.configure_triangle(800, 700, .2, unreal.Vector(80, 80, 40), unreal.Vector2D(2, 2), False)
floor.set_planar_animation_type(unreal.PlanarAnimationType.FLOWING_WAVE)
errors = unreal.MaterialEditingLibrary.recompile_material(unreal.load_asset("/Game/Destructible/Materials/M_PlanarAnimation"))
if errors:
    raise RuntimeError("Planar shader compile errors: " + str(errors))
floor.play_fixed_animation()
surfaces = [c for c in floor.get_components_by_class(unreal.DynamicMeshComponent)
            if c.component_has_tag("PlanarAnimationSurface")]
if not surfaces:
    raise RuntimeError("No planar surfaces")
material = surfaces[0].get_material(0)
capture_actor = actors.spawn_actor_from_class(unreal.SceneCapture2D, unreal.Vector(0, 0, 1200), unreal.Rotator(-90, 0, 0))
capture_actor.set_actor_rotation(unreal.Rotator(pitch=-90, yaw=0, roll=0), False)
capture = capture_actor.get_component_by_class(unreal.SceneCaptureComponent2D)
capture.set_editor_property("projection_type", unreal.CameraProjectionMode.ORTHOGRAPHIC)
capture.set_editor_property("ortho_width", 1000.0)
capture.set_editor_property("capture_every_frame", False)
capture.set_editor_property("capture_on_movement", False)
capture.set_editor_property("capture_source", unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
target = unreal.RenderingLibrary.create_render_target2d(world, 256, 256, unreal.TextureRenderTargetFormat.RTF_RGBA8)
capture.set_editor_property("texture_target", target)

def snapshot(name, alpha):
    # Editor selection/construction can regenerate a newly spawned actor before the first frame.
    floor.play_fixed_animation()
    current = [c for c in floor.get_components_by_class(unreal.DynamicMeshComponent)
               if c.component_has_tag("PlanarAnimationSurface")]
    material = current[0].get_material(0)
    unreal.log("PLANAR_CAPTURE_STATE " + str({"floor": str(floor.get_actor_location()),
        "bounds": str(floor.get_actor_bounds(False)), "camera": str(capture_actor.get_actor_location()),
        "rotation": str(capture_actor.get_actor_rotation()), "visible": current[0].is_visible(),
        "material": material.get_path_name(), "surfaces": len(current)}))
    material.set_vector_parameter_value("WaveColor", unreal.LinearColor(0, 0, 1, 1))
    material.set_vector_parameter_value("Fixed", unreal.LinearColor(1, 1, .18, 5))
    material.set_vector_parameter_value("Waves0", unreal.LinearColor(alpha, -1, -1, -1))
    capture.capture_scene()
    pixels = unreal.RenderingLibrary.read_render_target(world, target, False)
    # Python unwraps the output-array from the bool-returning Blueprint function.
    if isinstance(pixels, tuple):
        pixels = next(value for value in pixels if not isinstance(value, bool))
    blue = [i for i, color in enumerate(pixels) if color.b > 40 and color.b > color.r + 25]
    unreal.RenderingLibrary.export_render_target(world, target, str(output), name + ".png")
    if len(blue) < 40:
        return None
    return {"blue_pixels": len(blue), "centroid_x": sum(i % 256 for i in blue) / len(blue),
            "centroid_y": sum(i // 256 for i in blue) / len(blue)}

results = {}
state = {"since": time.monotonic(), "last": 0, "done": False}

def finish(error=None):
    state["done"] = True
    unreal.unregister_slate_post_tick_callback(state["handle"])
    floor.stop_fixed_animation()
    results["passed"] = error is None
    results["error"] = error
    (output / "result.json").write_text(json.dumps(results, indent=2))
    if error:
        unreal.log_error("PLANAR_RENDER_VERIFY_FAILED " + error)
    else:
        unreal.log("PLANAR_RENDER_VERIFY_OK " + json.dumps(results))
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)


def tick(_delta):
    if state["done"]:
        return
    now = time.monotonic()
    if now - state["since"] < 3 or now - state["last"] < 2:
        return
    state["last"] = now
    try:
        if now - state["since"] > 90:
            raise RuntimeError("No animated pixels after waiting for shader and scene updates")
        name, alpha = ("early", .25) if "early" not in results else ("late", .7)
        value = snapshot(name, alpha)
        if value is None:
            return
        results[name] = value
        if "late" in results:
            movement = sum(abs(results["early"][key] - results["late"][key]) for key in ("centroid_x", "centroid_y"))
            if movement < 20:
                raise RuntimeError(f"The light did not move across the surface: {results}")
            finish()
    except Exception as error:
        finish(str(error))

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state["handle"] = unreal.register_slate_post_tick_callback(tick)
