"""Capture the actual equalizer rising/falling in disposable PIE; no level/settings are saved."""
import json
import time
from pathlib import Path
import unreal

output = Path(unreal.Paths.project_saved_dir()) / "Analysis" / "EqualizerRendering"
output.mkdir(parents=True, exist_ok=True)
settings = unreal.get_default_object(unreal.load_class(None, "/Script/MyProject6.MusicReactionSettings"))
original_track = settings.get_editor_property("DefaultTrack")
settings.set_editor_property("DefaultTrack", None)
unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
floor = actors.spawn_actor_from_class(unreal.ChunkedDestructibleFloor, unreal.Vector())
floor.configure_floor(unreal.Vector2D(800, 700), unreal.Vector(80, 80, 40), unreal.Vector2D(0, 0), False)
floor.set_music_animation_type(unreal.MusicAnimationType.EQUALIZER)
floor.set_editor_property("music_equalizer_column_count", 8)
floor.set_editor_property("music_equalizer_row_count", 16)
floor.set_editor_property("music_reaction_duration", 3.0)
floor.set_editor_property("music_reaction_color", unreal.LinearColor(0, 0, 1, 1))
floor.set_editor_property("strong_music_glow_intensity", 4.0)
floor.set_editor_property("weak_music_glow_intensity", 4.0)
camera = actors.spawn_actor_from_class(unreal.SceneCapture2D, unreal.Vector(0, 0, 1200))
camera.set_actor_rotation(unreal.Rotator(pitch=-90, yaw=0, roll=0), False)
state = {"stage": "world", "started": time.monotonic(), "done": False}
results = {}


def finish(error=None):
    state["done"] = True
    unreal.unregister_slate_post_tick_callback(state["handle"])
    settings.set_editor_property("DefaultTrack", original_track)
    results.update(passed=error is None, error=error)
    (output / "result.json").write_text(json.dumps(results, indent=2))
    if error:
        unreal.log_error("EQUALIZER_RENDER_FAILED " + error)
    else:
        unreal.log("EQUALIZER_RENDER_OK " + json.dumps(results))
    unreal.EditorLevelLibrary.editor_end_play()
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)


def snapshot(name):
    state["capture"].capture_scene()
    pixels = unreal.RenderingLibrary.read_render_target(state["world"], state["target"], False)
    if isinstance(pixels, tuple):
        pixels = next(value for value in pixels if not isinstance(value, bool))
    blue = [i for i, color in enumerate(pixels) if color.b > 40 and color.b > color.r + 25]
    unreal.RenderingLibrary.export_render_target(state["world"], state["target"], str(output), name + ".png")
    if len(blue) < 30:
        raise RuntimeError("No visible equalizer segments in " + name)
    return {"lit_pixels": len(blue), "base_y": max(i // 256 for i in blue),
            "top_y": min(i // 256 for i in blue)}


def tick(_delta):
    if state["done"]:
        return
    try:
        now = time.monotonic()
        if now - state["started"] > 90:
            raise RuntimeError("Timed out during " + state["stage"])
        if state["stage"] == "world":
            world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
            if world is None:
                return
            runtime_floors = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.ChunkedDestructibleFloor)
            runtime_cameras = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.SceneCapture2D)
            if not runtime_floors or not runtime_cameras:
                return
            capture = runtime_cameras[0].get_component_by_class(unreal.SceneCaptureComponent2D)
            capture.set_editor_property("projection_type", unreal.CameraProjectionMode.ORTHOGRAPHIC)
            capture.set_editor_property("ortho_width", 1000.0)
            capture.set_editor_property("capture_every_frame", False)
            capture.set_editor_property("capture_on_movement", False)
            capture.set_editor_property("capture_source", unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
            target = unreal.RenderingLibrary.create_render_target2d(world, 256, 256, unreal.TextureRenderTargetFormat.RTF_RGBA8)
            capture.set_editor_property("texture_target", target)
            runtime_floors[0].set_react_to_music(True)
            # Warm up the material and scene before measuring a fresh pulse.
            runtime_floors[0].trigger_music_reaction(1.0, True)
            state.update(world=world, floor=runtime_floors[0], capture=capture, target=target,
                         stage="warmup", since=now)
        elif state["stage"] == "warmup" and now - state["since"] > 8:
            state["floor"].trigger_music_reaction(1.0, True)
            state.update(stage="capture", since=now)
        elif state["stage"] == "capture":
            elapsed = now - state["since"]
            name = "rising" if "rising" not in results else "peak" if "peak" not in results else "falling"
            threshold = {"rising": 0.16, "peak": 0.52, "falling": 2.8}[name]
            if elapsed < threshold:
                return
            results[name] = snapshot(name)
            if name == "falling":
                if results["peak"]["lit_pixels"] <= results["rising"]["lit_pixels"] * 1.2:
                    raise RuntimeError("Columns did not grow toward the peak")
                if results["falling"]["lit_pixels"] >= results["peak"]["lit_pixels"] * .7:
                    raise RuntimeError("Columns did not shorten after the peak")
                bases = [results[stage]["base_y"] for stage in ("rising", "peak", "falling")]
                if max(bases) - min(bases) > 3:
                    raise RuntimeError("The bottom of the columns moved: " + str(bases))
                finish()
    except Exception as error:
        finish(str(error))


unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state["handle"] = unreal.register_slate_post_tick_callback(tick)
unreal.EditorLevelLibrary.editor_play_simulate()
