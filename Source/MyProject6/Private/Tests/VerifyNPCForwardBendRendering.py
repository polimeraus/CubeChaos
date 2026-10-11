"""Render before/impact/recovery poses exported by ForwardBendAllSkeletons.

Run that automation with -NPCBendPoseExport first, then this script offscreen (D3D11).
Only temporary actors are used; the user's map and character assets are not saved.
"""
from pathlib import Path
import csv
import time
import unreal

output = Path(unreal.Paths.project_saved_dir()) / 'NPCForwardBend/Preview'
output.mkdir(parents=True, exist_ok=True)
poses = output.parent / 'Poses'
world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

sun = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 600), unreal.Rotator(-45, -30, 0))
sun.light_component.set_editor_property('intensity', 4.0)
sky = actors.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 500))
sky.light_component.set_editor_property('source_type', unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
sky.light_component.set_editor_property('cubemap', unreal.load_asset('/Engine/MapTemplates/Sky/DaylightAmbientCubemap'))
sky.light_component.set_editor_property('intensity', 3.0)
fill = actors.spawn_actor_from_class(unreal.PointLight, unreal.Vector(450, -350, 300))
fill.light_component.set_editor_property('intensity_units', unreal.LightUnits.CANDELAS)
fill.light_component.set_editor_property('intensity', 1000.0)
fill.light_component.set_editor_property('attenuation_radius', 5000.0)
fill.light_component.set_editor_property('cast_shadows', False)
floor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, -6))
floor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
floor.set_actor_scale3d(unreal.Vector(20, 20, .1))

camera = actors.spawn_actor_from_class(unreal.SceneCapture2D, unreal.Vector(600, -900, 310))
camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(camera.get_actor_location(), unreal.Vector(0, 0, 115)), False)
capture = camera.get_component_by_class(unreal.SceneCaptureComponent2D)
capture.set_editor_property('projection_type', unreal.CameraProjectionMode.ORTHOGRAPHIC)
capture.set_editor_property('ortho_width', 780.0)
capture.set_editor_property('capture_every_frame', False)
capture.set_editor_property('always_persist_rendering_state', True)
capture.set_editor_property('capture_source', unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
target = unreal.RenderingLibrary.create_render_target2d(world, 1400, 650, unreal.TextureRenderTargetFormat.RTF_RGBA8)
capture.set_editor_property('texture_target', target)
post = capture.get_editor_property('post_process_settings')
post.set_editor_property('override_auto_exposure_method', True)
post.set_editor_property('auto_exposure_method', unreal.AutoExposureMethod.AEM_MANUAL)
post.set_editor_property('override_auto_exposure_apply_physical_camera_exposure', True)
post.set_editor_property('auto_exposure_apply_physical_camera_exposure', False)
post.set_editor_property('override_auto_exposure_bias', True)
post.set_editor_property('auto_exposure_bias', 0.0)
capture.set_editor_property('post_process_settings', post)

cases = sorted(poses.glob('*_Before.csv'))
assert len(cases) == 7
spawned = []

def show_case(path):
    for actor in spawned:
        actors.destroy_actor(actor)
    spawned.clear()
    for index, stage in enumerate(['Before', 'Impact', 'Recovered']):
        lines = path.with_name(path.name.replace('_Before.csv', '_' + stage + '.csv')).read_text(encoding='utf-8-sig').splitlines()
        default = unreal.get_default_object(unreal.load_class(None, lines[0]))
        source = default.get_component_by_class(unreal.SkeletalMeshComponent)
        # Line up the three snapshots along the camera's screen-right direction.
        location = unreal.Vector((index - 1) * 200, (index - 1) * 133.333, 0)
        actor = actors.spawn_actor_from_class(unreal.Actor, location)
        subsystem = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
        handles = subsystem.k2_gather_subobject_data_for_instance(actor)
        handle, reason = subsystem.add_new_subobject(unreal.AddNewSubobjectParams(
            parent_handle=handles[0], new_class=unreal.PoseableMeshComponent))
        component = actor.get_component_by_class(unreal.PoseableMeshComponent)
        assert component, str(reason)
        component.set_skinned_asset_and_update(source.skeletal_mesh_asset)
        component.set_relative_rotation(source.get_editor_property('relative_rotation'), False, False)
        component.set_relative_scale3d(source.get_editor_property('relative_scale3d'))
        for slot in range(source.get_num_materials()):
            component.set_material(slot, source.get_material(slot))
        for row in csv.reader(lines[1:]):
            values = [float(v) for v in row[1:]]
            transform = unreal.Transform(location=unreal.Vector(*values[:3]),
                rotation=unreal.Rotator(pitch=values[3], yaw=values[4], roll=values[5]),
                scale=unreal.Vector(*values[6:9]))
            component.set_bone_transform_by_name(row[0], transform, unreal.BoneSpaces.COMPONENT_SPACE)
        unreal.NPCHitBendAnimInstance.refresh_preview_pose(component)
        spawned.append(actor)

state = {'index': 0, 'next': time.monotonic() + 15, 'phase': 'create', 'captures': 0}

def finish():
    unreal.unregister_slate_post_tick_callback(state['handle'])
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    unreal.SystemLibrary.execute_console_command(world, 'QUIT_EDITOR')

def tick(delta):
    if time.monotonic() < state['next']:
        return
    try:
        if state['phase'] == 'create':
            show_case(cases[state['index']])
            state.update(phase='capture', captures=4, next=time.monotonic() + 3)
        elif state['phase'] == 'capture':
            capture.capture_scene()
            state['captures'] -= 1
            state['next'] = time.monotonic() + .4
            if state['captures'] == 0:
                state['phase'] = 'save'
        else:
            name = cases[state['index']].stem.removesuffix('_Before')
            unreal.RenderingLibrary.export_render_target(world, target, str(output), name + '.png')
            state['index'] += 1
            if state['index'] == len(cases):
                unreal.log('NPC_FORWARD_BEND_RENDER_OK')
                finish()
            else:
                state.update(phase='create', next=time.monotonic() + .2)
    except Exception:
        finish()
        raise

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state['handle'] = unreal.register_slate_post_tick_callback(tick)
