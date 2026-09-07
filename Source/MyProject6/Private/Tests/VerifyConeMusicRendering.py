"""Verify colored traveling highlights on the real cone mesh in disposable PIE."""
import json
import time
from pathlib import Path
import unreal

output = Path(unreal.Paths.project_saved_dir()) / "Analysis" / "ConeMusicRendering"
output.mkdir(parents=True, exist_ok=True)
settings = unreal.get_default_object(unreal.load_class(None, "/Script/MyProject6.MusicReactionSettings"))
original_track = settings.get_editor_property("DefaultTrack")
settings.set_editor_property("DefaultTrack", None)
unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
cone = actors.spawn_actor_from_class(unreal.ChunkedDestructibleConeStructure, unreal.Vector())
cone.set_editor_property("cone_animation_type", unreal.ConeStructureAnimationType.NONE)
cone.set_editor_property("generate_platforms", True)
cone.set_editor_property("platform_style", unreal.ConeStructurePlatformStyle.RISING_SPIRAL)
cone.configure_cone_structure(12, 5, 0, .6, unreal.Vector(80, 80, 70), unreal.Vector(), False)
cone.set_music_animation_type(unreal.MusicAnimationType.TRAVELING_COLUMNS)
cone.set_editor_property("strong_onset_cell_count", 32)
cone.set_editor_property("music_reaction_duration", 2.0)
cone.set_editor_property("music_reaction_color", unreal.LinearColor(0, 0, 1, 1))
cone.set_editor_property("strong_music_glow_intensity", 4.0)
cone.set_editor_property("weak_music_glow_intensity", 4.0)
camera = actors.spawn_actor_from_class(unreal.SceneCapture2D, unreal.Vector(0, -1800, 620))
camera.set_actor_rotation(unreal.Rotator(pitch=-6, yaw=90, roll=0), False)
light = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 1000))
light.set_actor_rotation(unreal.Rotator(pitch=-45, yaw=45, roll=0), False)
state = {"stage": "world", "started": time.monotonic(), "done": False}
results = {}


def finish(error=None):
    state["done"] = True
    unreal.unregister_slate_post_tick_callback(state["handle"])
    settings.set_editor_property("DefaultTrack", original_track)
    results.update(passed=error is None, error=error)
    (output / "result.json").write_text(json.dumps(results, indent=2))
    if error:
        unreal.log_error("CONE_MUSIC_RENDER_FAILED " + error)
    else:
        unreal.log("CONE_MUSIC_RENDER_OK " + json.dumps(results))
    unreal.EditorLevelLibrary.editor_end_play()
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)


def snapshot(name):
    surfaces = [component for component in state["cone"].get_components_by_class(unreal.DynamicMeshComponent)
                if component.component_has_tag("MusicSurfaceSquares")]
    for surface in surfaces:
        unreal.log("CONE_SQUARE_SURFACE " + str(surface.get_dynamic_mesh().get_triangle_count())
                   + " visible=" + str(surface.is_visible()) + " transform=" + str(surface.get_world_transform())
                   + " material=" + str(surface.get_material(0)))
    for component in state["cone"].get_components_by_class(unreal.DynamicMeshComponent):
        if component.get_num_materials() > 2:
            material = component.get_material(2)
            unreal.log("CONE_MUSIC_ACTIVE_MATERIAL " + str(material.get_editor_property("parent")) + " Color=" + str(material.get_vector_parameter_value("Color")))
            break
    state["capture"].capture_scene()
    pixels = unreal.RenderingLibrary.read_render_target(state["world"], state["target"], False)
    if isinstance(pixels, tuple):
        pixels = next(value for value in pixels if not isinstance(value, bool))
    blue = [i for i, color in enumerate(pixels) if color.b > 40 and color.b > color.r + 30 and color.b > color.g + 30]
    unreal.RenderingLibrary.export_render_target(state["world"], state["target"], str(output), name + ".png")
    if len(blue) < 50:
        raise RuntimeError("No colored full-piece highlights in " + name)
    return {"blue_pixels": len(blue), "centroid_y": sum(i // 384 for i in blue) / len(blue)}


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
            cones = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.ChunkedDestructibleConeStructure)
            cameras = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.SceneCapture2D)
            if not cones or not cameras:
                return
            capture = cameras[0].get_component_by_class(unreal.SceneCaptureComponent2D)
            capture.set_editor_property("projection_type", unreal.CameraProjectionMode.ORTHOGRAPHIC)
            capture.set_editor_property("ortho_width", 1400.0)
            capture.set_editor_property("capture_every_frame", False)
            capture.set_editor_property("capture_on_movement", False)
            capture.set_editor_property("capture_source", unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
            target = unreal.RenderingLibrary.create_render_target2d(world, 384, 384, unreal.TextureRenderTargetFormat.RTF_RGBA8)
            capture.set_editor_property("texture_target", target)
            cones[0].set_react_to_music(True)
            cones[0].trigger_music_reaction(1.0, True)
            if any(component.component_has_tag("MusicEqualizer") for component in cones[0].get_components_by_class(unreal.InstancedStaticMeshComponent)):
                raise RuntimeError("Cone still allocated small overlay cubes")
            state.update(world=world, cone=cones[0], capture=capture, target=target, stage="warmup", since=now)
        elif state["stage"] == "warmup" and now - state["since"] > 8:
            state["cone"].trigger_music_reaction(1.0, True)
            state.update(stage="capture", since=now)
        elif state["stage"] == "capture":
            name = "lower" if "lower" not in results else "upper"
            if now - state["since"] < (.35 if name == "lower" else 1.05) / state["cone"].get_traveling_columns_speed():
                return
            results[name] = snapshot(name)
            if name == "upper":
                if results["lower"]["centroid_y"] < results["upper"]["centroid_y"] + 20:
                    raise RuntimeError("Traveling highlights did not advance upward")
                finish()
    except Exception as error:
        finish(str(error))


unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state["handle"] = unreal.register_slate_post_tick_callback(tick)
unreal.EditorLevelLibrary.editor_play_simulate()
