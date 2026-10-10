"""Inspect complete trapezoid panels and their damage openings in an unsaved scene."""
from pathlib import Path
import math
import time
import unreal

output = Path(unreal.Paths.project_saved_dir()) / 'QuadCone/Preview'
output.mkdir(parents=True, exist_ok=True)
world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
cone = actors.spawn_actor_from_class(unreal.QuadPanelConeStructure, unreal.Vector())
for name, value in {
    'generate_platforms': True, 'platform_style': unreal.ConeStructurePlatformStyle.RISING_SPIRAL,
    'platform_spiral_turns': 2., 'spiral_segments_per_turn': 48,
    'spiral_end_height_blocks': 17, 'lowest_platform_height_blocks': 0,
    'platform_overhang_blocks': 3., 'wall_panel_segments': 32, 'panel_joint_width': 1.,
    'generate_entrance': False, 'scatter_destroyed_pieces': False,
    'play_destroy_tick_sound': False, 'flash_duration': 0.,
    'cone_animation_type': unreal.ConeStructureAnimationType.NONE,
}.items():
    cone.set_editor_property(name, value)
cone.set_react_to_music(False)
cone.configure_cone_structure(18, 7, 1.5, .6, unreal.Vector(100, 100, 100), unreal.Vector(), False)
cone.set_structure_materials(unreal.load_asset('/Engine/MapTemplates/Materials/BasicAsset01'),
                             unreal.load_asset('/Engine/MapTemplates/Materials/BasicAsset03'))
light = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 2500), unreal.Rotator(-35, -40, 0))
light.light_component.set_editor_property('intensity', 3.)
fill = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 2500), unreal.Rotator(-25, 150, 0))
fill.light_component.set_editor_property('intensity', 1.5)
camera = actors.spawn_actor_from_class(unreal.SceneCapture2D, unreal.Vector(2600, 0, 1300))
capture = camera.get_component_by_class(unreal.SceneCaptureComponent2D)
capture.set_editor_property('capture_every_frame', False)
capture.set_editor_property('capture_on_movement', False)
capture.set_editor_property('always_persist_rendering_state', True)
capture.set_editor_property('capture_source', unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
capture.set_editor_property('fov_angle', 60.)
post = capture.get_editor_property('post_process_settings')
for name, value in {
    'override_auto_exposure_method': True, 'auto_exposure_method': unreal.AutoExposureMethod.AEM_MANUAL,
    'override_auto_exposure_apply_physical_camera_exposure': True, 'auto_exposure_apply_physical_camera_exposure': False,
    'override_auto_exposure_bias': True, 'auto_exposure_bias': 0.,
}.items(): post.set_editor_property(name, value)
capture.set_editor_property('post_process_settings', post)
target = unreal.RenderingLibrary.create_render_target2d(world, 1200, 1000, unreal.TextureRenderTargetFormat.RTF_RGBA8)
capture.set_editor_property('texture_target', target)
views = [('whole', unreal.Vector(2500, -2200, 1600), unreal.Vector(0,0,900)),
         ('panels', unreal.Vector(1500,0,1050), unreal.Vector(0,0,850)),
         ('damaged', unreal.Vector(1500,0,1050), unreal.Vector(0,0,850))]
state = {'index': 0, 'next': time.monotonic() + 12, 'captures': 0, 'export': False}

def tick(delta):
    if time.monotonic() < state['next']: return
    try:
        if state['captures']:
            capture.capture_scene()
            state['captures'] -= 1
            state.update(export=state['captures'] == 0, next=time.monotonic() + .3)
            return
        if state['export']:
            unreal.RenderingLibrary.export_render_target(world, target, str(output), views[state['index']][0] + '.png')
            state['index'] += 1
            state['export'] = False
            if state['index'] == len(views):
                unreal.log('QUAD_CONE_RENDER_OK')
                unreal.unregister_slate_post_tick_callback(state['handle'])
                unreal.EditorPythonScripting.set_keep_python_script_alive(False)
                return
        name, location, look = views[state['index']]
        if name == 'damaged':
            for row, column in [(6,0), (7,0), (7,31), (8,1)]:
                z = (row + .5) * 100
                radius = (7 + (1.5 - 7) * z / 1800) * 100 - 30
                angle = (column + .5) * math.tau / 32
                cell = cone.world_location_to_cell(unreal.Vector(math.cos(angle)*radius, math.sin(angle)*radius, z))
                assert cell is not None and cone.destroy_cell(cell), (row, column)
        camera.set_actor_location(location, False, False)
        camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(location, look), False)
        state.update(captures=3, next=time.monotonic() + .5)
    except Exception:
        unreal.unregister_slate_post_tick_callback(state['handle'])
        unreal.EditorPythonScripting.set_keep_python_script_alive(False)
        raise

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state['handle'] = unreal.register_slate_post_tick_callback(tick)
