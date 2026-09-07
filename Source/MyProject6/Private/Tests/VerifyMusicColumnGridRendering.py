"""Verify all three moving-column modes in disposable PIE. No level or settings are saved."""
import json
import time
from pathlib import Path
import unreal

output = Path(unreal.Paths.project_saved_dir()) / "Analysis" / "MusicColumnGridRendering"
output.mkdir(parents=True, exist_ok=True)
settings = unreal.get_default_object(unreal.load_class(None, "/Script/MyProject6.MusicReactionSettings"))
original_track = settings.get_editor_property("DefaultTrack")
settings.set_editor_property("DefaultTrack", None)
unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
grid = actors.spawn_actor_from_class(unreal.MusicReactiveColumnGrid, unreal.Vector())
grid.configure_grid(30, 1022, 650, 1)
grid.configure_heights(15, 100, 300)
grid.set_wave_speed(500)
grid.set_editor_property("wave_width", 300)
grid.set_editor_property("equalizer_response_duration", 1.4)
camera_position = unreal.Vector(700, -1000, 1350)
camera = actors.spawn_actor_from_class(unreal.SceneCapture2D, camera_position)
camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(camera_position, unreal.Vector(0, 0, 100)), False)
light = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 1000))
light.set_actor_rotation(unreal.Rotator(pitch=-55, yaw=-35, roll=0), False)
light.get_component_by_class(unreal.DirectionalLightComponent).set_editor_property("intensity", 4.0)
mode = unreal.ColumnGridReactionType
direction = unreal.ColumnGridWaveDirection
cases = [
    ("equalizer", mode.EQUALIZER, direction.OUTWARD),
    ("circular_outward", mode.CIRCULAR_WAVE, direction.OUTWARD),
    ("circular_inward", mode.CIRCULAR_WAVE, direction.INWARD),
    ("rectangular_outward", mode.RECTANGULAR_WAVE, direction.OUTWARD),
    ("rectangular_inward", mode.RECTANGULAR_WAVE, direction.INWARD),
]
state = {"stage": "world", "started": time.monotonic(), "done": False, "case": 0}
results = {}


def finish(error=None):
    state["done"] = True
    unreal.unregister_slate_post_tick_callback(state["handle"])
    settings.set_editor_property("DefaultTrack", original_track)
    results.update(passed=error is None, error=error)
    (output / "result.json").write_text(json.dumps(results, indent=2))
    if error:
        unreal.log_error("COLUMN_GRID_RENDER_FAILED " + error)
    else:
        unreal.log("COLUMN_GRID_RENDER_OK " + json.dumps(results))
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)


def capture(name):
    state["capture"].capture_scene()
    unreal.RenderingLibrary.export_render_target(state["world"], state["target"], str(output), name + ".png")


def begin_case(now):
    name, reaction, travel = cases[state["case"]]
    state["grid"].set_reaction_type(reaction)
    state["grid"].set_wave_direction(travel)
    # Trigger on a fresh game frame after capture/export work has finished. GPU
    # readback must not make the event start with an already stale world timestamp.
    state.update(stage="trigger", snapshots=0)
    results[name] = []


def tick(_delta):
    if state["done"]:
        return
    try:
        now = time.monotonic()
        if now - state["started"] > 120:
            raise RuntimeError("Timed out in " + state["stage"])
        if state["stage"] == "world":
            world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
            if world is None:
                return
            grids = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.MusicReactiveColumnGrid)
            cameras = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.SceneCapture2D)
            if not grids or not cameras:
                return
            component = cameras[0].get_component_by_class(unreal.SceneCaptureComponent2D)
            component.set_editor_property("projection_type", unreal.CameraProjectionMode.ORTHOGRAPHIC)
            component.set_editor_property("ortho_width", 1450.0)
            component.set_editor_property("capture_every_frame", False)
            component.set_editor_property("capture_on_movement", False)
            component.set_editor_property("capture_source", unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
            target = unreal.RenderingLibrary.create_render_target2d(world, 640, 480, unreal.TextureRenderTargetFormat.RTF_RGBA8)
            component.set_editor_property("texture_target", target)
            state.update(world=world, grid=grids[0], capture=component, target=target, stage="warmup", since=now)
        elif state["stage"] == "warmup" and now - state["since"] > 8:
            capture("rest")
            begin_case(time.monotonic())
        elif state["stage"] == "trigger":
            state["grid"].trigger_music_reaction(1.0)
            state.update(stage="capture", since=now)
        elif state["stage"] == "capture":
            name = cases[state["case"]][0]
            delay = .2 if state["snapshots"] == 0 else .8
            if now - state["since"] < delay:
                return
            count = state["grid"].get_column_count()
            heights = [state["grid"].get_column_height(x, y) for y in range(count.y) for x in range(count.x)]
            unreal.log("COLUMN_GRID_SAMPLE " + name + " elapsed=" + str(now - state["since"])
                       + " count=" + str(count) + " heights=" + str((min(heights), max(heights)))
                       + " ticking=" + str(state["grid"].is_actor_tick_enabled()))
            if max(heights) - min(heights) < 5:
                raise RuntimeError("No moving column heights in " + name)
            capture(name + ("_early" if state["snapshots"] == 0 else "_late"))
            results[name].append({"min_height": min(heights), "max_height": max(heights)})
            state["snapshots"] += 1
            if state["snapshots"] == 2:
                state["stage"] = "settle"
        elif state["stage"] == "settle" and now - state["since"] > 2.1:
            if state["grid"].is_actor_tick_enabled():
                raise RuntimeError("Grid did not become idle after the wave")
            state["case"] += 1
            if state["case"] == len(cases):
                finish()
            else:
                begin_case(now)
    except Exception as error:
        finish(str(error))


unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state["handle"] = unreal.register_slate_post_tick_callback(tick)
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_play_simulate()
