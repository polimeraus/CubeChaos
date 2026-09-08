"""Render side neon without any lights in a disposable editor world. No map/settings are saved."""
import json
import time
from pathlib import Path
import unreal

output = Path(unreal.Paths.project_saved_dir()) / "Analysis" / "SideNeonRendering"
output.mkdir(parents=True, exist_ok=True)
world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
floor = actors.spawn_actor_from_class(unreal.ChunkedFloor, unreal.Vector())
shape = unreal.ChunkedDecorativeFloorShape
mode = unreal.FloorSideNeonMode
scope = unreal.FloorSideNeonScope
floor.configure_chunked_floor(shape.GRID, unreal.Vector2D(800, 800),
                              unreal.Vector(100, 100, 100), unreal.Vector2D(), False)
floor.set_editor_property("flash_duration", 0.0)
floor.set_editor_property("play_destroy_tick_sound", False)
body = unreal.MaterialLibrary.create_dynamic_material_instance(
    world, unreal.load_asset("/Game/Destructible/Materials/M_MusicPieceGlow"))
body.set_vector_parameter_value("Color", unreal.LinearColor(.045, .045, .045, 1))
floor.set_material_palette([body])
position = unreal.Vector(1100, -1500, 1000)
camera = actors.spawn_actor_from_class(unreal.SceneCapture2D, position)
camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(position, unreal.Vector(0, 0, -50)), False)
capture = camera.get_component_by_class(unreal.SceneCaptureComponent2D)
capture.set_editor_property("projection_type", unreal.CameraProjectionMode.ORTHOGRAPHIC)
capture.set_editor_property("ortho_width", 1250.0)
capture.set_editor_property("capture_every_frame", False)
capture.set_editor_property("capture_on_movement", False)
capture.set_editor_property("always_persist_rendering_state", True)
capture.set_editor_property("capture_source", unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
target = unreal.RenderingLibrary.create_render_target2d(world, 512, 512, unreal.TextureRenderTargetFormat.RTF_RGBA8)
capture.set_editor_property("texture_target", target)
post = capture.get_editor_property("post_process_settings")
post.set_editor_property("override_bloom_intensity", True)
post.set_editor_property("bloom_intensity", .5)
capture.set_editor_property("post_process_settings", post)

settings = floor.get_side_neon_settings()
settings.set_editor_property("color", unreal.LinearColor(0, .4, 1, 1))
settings.set_editor_property("intensity", 6.0)
settings.set_editor_property("stripe_width", 12.0)
cases = ["off", "grid_stripe", "grid_full", "grid_magenta", "triangle", "star", "hole_outer", "hole_all", "restored"]
results = {}
state = {"started": time.monotonic(), "since": time.monotonic(), "case": 0, "prepared": False, "done": False}


def prepare(name):
    settings.set_editor_property("mode", mode.OFF if name == "off" else mode.STRIPE)
    if name == "grid_full":
        settings.set_editor_property("mode", mode.FULL_SIDE)
    if name == "grid_magenta":
        settings.set_editor_property("color", unreal.LinearColor(1, 0, .4, 1))
    if name == "triangle":
        floor.set_floor_shape(shape.TRIANGLE, unreal.Vector2D(800, 800), False)
    if name == "star":
        floor.set_floor_shape(shape.STAR, unreal.Vector2D(800, 800), False)
    if name == "hole_outer":
        floor.set_floor_shape(shape.GRID, unreal.Vector2D(800, 800), False)
        for x in range(2, 6):
            for y in range(2, 6):
                floor.destroy_cell(unreal.IntPoint(x, y))
    if name == "hole_all":
        settings.set_editor_property("scope", scope.ALL_EXPOSED_SIDES)
    if name == "restored":
        floor.restore_all_cells()
    floor.set_side_neon_settings(settings)


def snapshot(name):
    chunks = [c for c in floor.get_components_by_class(unreal.DynamicMeshComponent)
              if c.component_has_tag("ChunkedDestructibleFloorChunk")]
    unreal.log("SIDE_NEON_CAPTURE " + name + " settings=" + str(floor.get_side_neon_settings())
               + " location=" + str(floor.get_actor_location()) + " camera=" + str(camera.get_actor_location())
               + " materials=" + str([[str(c.get_material(i)) for i in range(c.get_num_materials())] for c in chunks]))
    capture.capture_scene()
    pixels = unreal.RenderingLibrary.read_render_target(world, target, False)
    if isinstance(pixels, tuple):
        pixels = next(item for item in pixels if not isinstance(item, bool))
    colored = [i for i, p in enumerate(pixels) if max(p.r, p.g, p.b) > 80
               and max(p.r, p.g, p.b) - min(p.r, p.g, p.b) > 35]
    unreal.RenderingLibrary.export_render_target(world, target, str(output), name + ".png")
    if name != "off" and len(colored) < 80:
        # A lazily loaded material can still be compiling its first RHI shader map.
        return None
    components = [c for c in floor.get_components_by_class(unreal.DynamicMeshComponent)
                  if c.component_has_tag("ChunkedDestructibleFloorChunk")]
    return {"colored_pixels": len(colored),
            "triangles": sum(c.get_dynamic_mesh().get_triangle_count() for c in components),
            "chunks": len(components), "ticking": floor.is_actor_tick_enabled()}


def finish(error=None):
    state["done"] = True
    unreal.unregister_slate_post_tick_callback(state["handle"])
    results.update(passed=error is None, error=error)
    (output / "result.json").write_text(json.dumps(results, indent=2))
    if error:
        unreal.log_error("SIDE_NEON_RENDER_FAILED " + error)
    else:
        unreal.log("SIDE_NEON_RENDER_OK " + json.dumps(results))
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)


def tick(_delta):
    if state["done"]:
        return
    try:
        now = time.monotonic()
        if now - state["started"] > 120:
            raise RuntimeError("Side neon render timeout")
        if now - state["started"] < 8:
            return
        name = cases[state["case"]]
        if not state["prepared"]:
            prepare(name)
            state.update(prepared=True, since=now)
            return
        if now - state["since"] < 2:
            return
        value = snapshot(name)
        if value is None:
            state["since"] = now
            return
        results[name] = value
        state.update(case=state["case"] + 1, prepared=False)
        if state["case"] == len(cases):
            if results["grid_full"]["colored_pixels"] <= results["grid_stripe"]["colored_pixels"] * 1.5:
                raise RuntimeError("Full-side mode did not fill the side")
            if results["hole_all"]["colored_pixels"] <= results["hole_outer"]["colored_pixels"] + 50:
                raise RuntimeError("Damage hole did not gain neon in all-exposed mode")
            if any(item["ticking"] for item in results.values()):
                raise RuntimeError("Neon activated actor tick")
            if results["off"]["triangles"] != results["grid_stripe"]["triangles"]:
                raise RuntimeError("Neon added geometry")
            finish()
    except Exception as error:
        finish(str(error))


unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state["handle"] = unreal.register_slate_post_tick_callback(tick)
