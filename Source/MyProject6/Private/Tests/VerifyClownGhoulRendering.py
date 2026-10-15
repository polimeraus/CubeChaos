"""Render actual Ghoul test poses and Clown clips in an unsaved preview world."""
from pathlib import Path
import csv
import time
import traceback
import unreal

OUT = Path(unreal.Paths.project_saved_dir()) / 'ClownGhoul/Preview'
OUT.mkdir(parents=True, exist_ok=True)
world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
sun = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 600), unreal.Rotator(pitch=-45, yaw=-30, roll=0))
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
floor.set_actor_scale3d(unreal.Vector(24, 24, .1))
camera = actors.spawn_actor_from_class(unreal.SceneCapture2D, unreal.Vector(600, -900, 370))
camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(camera.get_actor_location(), unreal.Vector(0, 0, 80)), False)
capture = camera.get_component_by_class(unreal.SceneCaptureComponent2D)
capture.set_editor_property('projection_type', unreal.CameraProjectionMode.ORTHOGRAPHIC)
capture.set_editor_property('ortho_width', 960.0)
capture.set_editor_property('capture_every_frame', False)
capture.set_editor_property('always_persist_rendering_state', True)
capture.set_editor_property('capture_source', unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
target = unreal.RenderingLibrary.create_render_target2d(world, 1600, 700, unreal.TextureRenderTargetFormat.RTF_RGBA8)
capture.set_editor_property('texture_target', target)
post = capture.get_editor_property('post_process_settings')
for key, value in {'override_auto_exposure_method': True, 'auto_exposure_method': unreal.AutoExposureMethod.AEM_MANUAL,
                   'override_auto_exposure_apply_physical_camera_exposure': True, 'auto_exposure_apply_physical_camera_exposure': False,
                   'override_auto_exposure_bias': True, 'auto_exposure_bias': 0.0}.items():
    post.set_editor_property(key, value)
capture.set_editor_property('post_process_settings', post)
spawned = []


def show_case(index):
    for actor in spawned:
        actors.destroy_actor(actor)
    spawned.clear()
    cls = unreal.ChasingNPC if index == 0 else unreal.load_class(None,
        '/Game/NPC/ClownMonster/BP_ClownMonster_NPC.BP_ClownMonster_NPC_C')
    source = unreal.get_default_object(cls).mesh
    for column, stage in enumerate(['Before', 'Impact', 'Dead']):
        actor = actors.spawn_actor_from_class(unreal.Actor, unreal.Vector((column - 1) * 255, (column - 1) * 170, 0))
        subsystem = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
        handles = subsystem.k2_gather_subobject_data_for_instance(actor)
        _, reason = subsystem.add_new_subobject(unreal.AddNewSubobjectParams(
            parent_handle=handles[0], new_class=unreal.PoseableMeshComponent))
        mesh = actor.get_component_by_class(unreal.PoseableMeshComponent)
        assert mesh, str(reason)
        mesh.set_skinned_asset_and_update(source.skeletal_mesh_asset)
        mesh.set_relative_rotation(source.get_editor_property('relative_rotation'), False, False)
        for slot in range(source.get_num_materials()):
            mesh.set_material(slot, source.get_material(slot))
        if index == 0:
            path = OUT.parent / 'Poses' / ('Ghoul_' + stage + '.csv')
            lines = path.read_text(encoding='utf-8-sig').splitlines()
            transforms = []
            for row in csv.reader(lines[1:]):
                v = [float(x) for x in row[1:]]
                transforms.append((row[0], unreal.Transform(location=unreal.Vector(*v[:3]),
                    rotation=unreal.Rotator(pitch=v[3], yaw=v[4], roll=v[5]), scale=unreal.Vector(*v[6:9]))))
        else:
            name, seconds = [('anim_idle_1', .3), ('anim_attack_1', 1.0), ('anim_death', 2.2)][column]
            animation = unreal.load_asset('/Game/NPC/ClownMonster/demo/animation/' + name)
            pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(animation, seconds, unreal.AnimPoseEvaluationOptions())
            transforms = [(bone, unreal.AnimPoseExtensions.get_bone_pose(pose, bone, unreal.AnimPoseSpaces.WORLD))
                          for bone in unreal.AnimPoseExtensions.get_bone_names(pose)]
        for bone, transform in transforms:
            mesh.set_bone_transform_by_name(bone, transform, unreal.BoneSpaces.COMPONENT_SPACE)
        unreal.NPCHitBendAnimInstance.refresh_preview_pose(mesh)
        spawned.append(actor)


state = {'index': 0, 'phase': 'create', 'next': time.monotonic() + 8}


def finish():
    unreal.unregister_slate_post_tick_callback(state['handle'])
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    unreal.SystemLibrary.execute_console_command(world, 'QUIT_EDITOR')


def tick(delta):
    if time.monotonic() < state['next']:
        return
    try:
        if state['phase'] == 'create':
            show_case(state['index'])
            state.update(phase='capture', captures=4, next=time.monotonic()+3)
        elif state['phase'] == 'capture':
            capture.capture_scene()
            state['captures'] -= 1
            state['next'] = time.monotonic()+.4
            if state['captures'] == 0:
                state['phase'] = 'save'
        else:
            unreal.RenderingLibrary.export_render_target(world, target, str(OUT), ['Ghoul', 'Clown'][state['index']] + '.png')
            state['index'] += 1
            if state['index'] == 2:
                unreal.log('CLOWN_GHOUL_RENDER_OK')
                finish()
            else:
                state.update(phase='create', next=time.monotonic()+.2)
    except Exception:
        unreal.log_error(traceback.format_exc())
        finish()


unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state['handle'] = unreal.register_slate_post_tick_callback(tick)
