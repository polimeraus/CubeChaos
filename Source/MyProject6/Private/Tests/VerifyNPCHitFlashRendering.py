"""Inspect the real damage-overlay material on all four NPC skins in an unsaved scene.

Timed recovery and repeated impacts are covered by NPCHitReactionTests.cpp.
"""
from pathlib import Path
import time
import unreal

output = Path(unreal.Paths.project_saved_dir()) / 'ImpactPolish/NPCPreview'
output.mkdir(parents=True, exist_ok=True)
world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
cases = [
    (unreal.ChasingNPC, '/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle'),
    (unreal.load_class(None, '/Game/NPC/RadicalMike/BP_RadicalMike_NPC.BP_RadicalMike_NPC_C'), '/Game/NPC/RadicalMike/Animations/Anim_ZMIKE_BusterIdle_R'),
    (unreal.load_class(None, '/Game/NPC/Whisper/BP_Whisper_Follower.BP_Whisper_Follower_C'), '/Game/NPC/Whisper/Animations/Anim_Whisper_Idle1'),
    (unreal.load_class(None, '/Game/NPC/BlackMonster/BP_BlackMonster_Follower.BP_BlackMonster_Follower_C'), '/Game/NPC/BlackMonster/Animations/Anim_BlackMonster_Idle')]
npcs = []
for index, (cls, animation_path) in enumerate(cases):
    npc = actors.spawn_actor_from_class(cls, unreal.Vector(0, (index - 1.5) * 220, 100))
    npc.mesh.set_update_animation_in_editor(True)
    npc.mesh.play_animation(unreal.load_asset(animation_path), True)
    npc.mesh.set_play_rate(0)
    npc.mesh.set_position(.3, False)
    npcs.append(npc)
for rotation, intensity in [(unreal.Rotator(-45, -30, 0), 40.0), (unreal.Rotator(-25, 150, 0), 24.0)]:
    light = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 500), rotation)
    light.light_component.set_editor_property('intensity', intensity)
fill = actors.spawn_actor_from_class(unreal.PointLight, unreal.Vector(450, 0, 300))
fill.light_component.set_editor_property('intensity', 1500.0)
fill.light_component.set_editor_property('attenuation_radius', 2000.0)
floor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, -5))
floor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
floor.set_actor_scale3d(unreal.Vector(20, 20, .1))
surface = floor.static_mesh_component.create_dynamic_material_instance(0, unreal.load_asset('/Game/LevelPrototyping/Materials/M_FlatCol'))
surface.set_vector_parameter_value('Base Color', unreal.LinearColor(.04, .04, .04, 1))
camera = actors.spawn_actor_from_class(unreal.SceneCapture2D, unreal.Vector(900, 0, 250))
camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(camera.get_actor_location(), unreal.Vector(0, 0, 125)), False)
capture = camera.get_component_by_class(unreal.SceneCaptureComponent2D)
capture.set_editor_property('projection_type', unreal.CameraProjectionMode.ORTHOGRAPHIC)
capture.set_editor_property('ortho_width', 1040.0)
capture.set_editor_property('capture_every_frame', False)
capture.set_editor_property('always_persist_rendering_state', True)
capture.set_editor_property('capture_source', unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
target = unreal.RenderingLibrary.create_render_target2d(world, 1600, 680, unreal.TextureRenderTargetFormat.RTF_RGBA8)
capture.set_editor_property('texture_target', target)
post = capture.get_editor_property('post_process_settings')
post.set_editor_property('override_auto_exposure_method', True)
post.set_editor_property('auto_exposure_method', unreal.AutoExposureMethod.AEM_MANUAL)
post.set_editor_property('override_auto_exposure_bias', True)
post.set_editor_property('auto_exposure_bias', 10.0)
capture.set_editor_property('post_process_settings', post)
stages = ['Normal', 'WhiteImpact', 'BlueImpact', 'Recovered']
state = {'index': 0, 'next': time.monotonic() + 12, 'pending': False, 'capture': False}

def tick(delta):
    if time.monotonic() < state['next']:
        return
    try:
        if state['capture']:
            capture.capture_scene()
            state['capture'] -= 1
            state.update(pending=state['capture'] == 0, next=time.monotonic() + .3)
            return
        if state['pending']:
            unreal.RenderingLibrary.export_render_target(world, target, str(output), stages[state['index']] + '.png')
            state['index'] += 1
            state['pending'] = False
            if state['index'] == len(stages):
                unreal.log('NPC_HIT_FLASH_RENDER_OK')
                unreal.unregister_slate_post_tick_callback(state['handle'])
                unreal.EditorPythonScripting.set_keep_python_script_alive(False)
                return
        name = stages[state['index']]
        for npc in npcs:
            reaction = npc.get_component_by_class(unreal.NPCHitReactionComponent)
            if name in ('WhiteImpact', 'BlueImpact'):
                reaction.set_editor_property('hit_flash_color', unreal.LinearColor(1, 1, 1, 1) if name == 'WhiteImpact' else unreal.LinearColor(.04, .3, 1, 1))
                reaction.play_damage_flash()
                assert npc.mesh.get_overlay_material() is not None
                overlay = npc.mesh.get_overlay_material()
                unreal.log(f'{name}: {npc.get_name()} overlay={overlay.get_path_name()} amount={overlay.get_scalar_parameter_value("HitFlashAmount")} distance={npc.mesh.get_overlay_material_max_draw_distance()}')
            else:
                npc.mesh.set_overlay_material(None)
        # Let render-state recreation/PSO precaching finish before the capture.
        state.update(capture=3, next=time.monotonic() + 1)
    except Exception:
        unreal.unregister_slate_post_tick_callback(state['handle'])
        unreal.EditorPythonScripting.set_keep_python_script_alive(False)
        raise

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state['handle'] = unreal.register_slate_post_tick_callback(tick)
