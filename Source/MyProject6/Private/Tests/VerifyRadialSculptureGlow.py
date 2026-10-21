"""Fresh-process persistence and rendered phase/material checks; visual changes are unsaved."""
import json
import time
from pathlib import Path
import unreal

folder = '/Game/Structures/Destructible/RadialSculpture'
out = Path(unreal.Paths.project_saved_dir()).resolve() / 'RadialSculpture/Glow'
out.mkdir(parents=True, exist_ok=True)
saved = json.loads((out.parent / 'GlowUpgrade.json').read_text())
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
bp = unreal.load_asset(saved['blueprint'])
defaults = unreal.get_default_object(bp.generated_class())
assert defaults.glow_animation == unreal.RadialSculptureGlowAnimation.SEQUENTIAL_FINS
assert defaults.glow_preview_time == -1.
for path, records in saved['maps'].items():
    assert unreal.EditorLoadingAndSavingUtils.load_map(path)
    found = [a for a in actors.get_all_level_actors() if isinstance(a, unreal.DestructibleRadialSculpture)]
    assert len(found) == len(records)
    for a in found:
        expected = next(r for r in records if r['name'] == a.get_name())
        assert a.get_class().get_path_name() == expected['owning_class']
        assert a.get_actor_label() == expected['label'] and str(a.get_folder_path()) == expected['folder']
        t = a.get_actor_transform()
        assert [t.translation.x,t.translation.y,t.translation.z] == expected['location']
        assert [t.rotation.x,t.rotation.y,t.rotation.z,t.rotation.w] == expected['rotation']
        assert [t.scale3d.x,t.scale3d.y,t.scale3d.z] == expected['scale']
        for key, value in expected['shape'].items():
            assert a.get_editor_property(key) == value, key
        assert a.sculpture_material.get_path_name() == expected['material']
        assert a.glow_animation == defaults.glow_animation
        assert a.generated_piece_count == a.remaining_piece_count > 1000 and not a.generation_warning
        chunks = a.get_components_by_class(unreal.DynamicMeshComponent)
        assert chunks and all(c.get_material(0) == a.sculpture_material for c in chunks)
        assert all(isinstance(c.get_material(1), unreal.MaterialInstanceDynamic) for c in chunks)

world = unreal.EditorLoadingAndSavingUtils.load_map(folder + '/Maps/Lvl_RadialSculpturePreview')
a = next(a for a in actors.get_all_level_actors() if isinstance(a, unreal.DestructibleRadialSculpture))
fresh = actors.spawn_actor_from_class(bp.generated_class(), unreal.Vector(5000,0,0))
for key in ['fin_height','sphere_radius','sculpture_material','glow_animation','glow_duration',
            'glow_loop_delay','glow_leg_width','glow_band_height','sculpture_glow_color','sculpture_glow_intensity']:
    assert fresh.get_editor_property(key) == defaults.get_editor_property(key), key
assert fresh.generated_piece_count == a.generated_piece_count and not fresh.is_actor_tick_enabled()
actors.destroy_actor(fresh)
material = unreal.load_asset(folder + '/Materials/M_RadialSculptureGlow')
expressions = unreal.MaterialEditingLibrary.get_material_expressions(material)
shader = next(e for e in expressions if isinstance(e, unreal.MaterialExpressionCustom))
assert shader.get_editor_property('code') == (Path(__file__).parent.parent / 'RadialSculptureGlow.ush').read_text()

camera = actors.spawn_actor_from_class(unreal.SceneCapture2D, unreal.Vector(1100,-1450,1100))
camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(camera.get_actor_location(), unreal.Vector(0,0,330)), False)
capture = camera.get_component_by_class(unreal.SceneCaptureComponent2D)
for key, value in dict(capture_every_frame=False,capture_on_movement=False,always_persist_rendering_state=True,
                       fov_angle=48.,capture_source=unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR).items():
    capture.set_editor_property(key,value)
target = unreal.RenderingLibrary.create_render_target2d(world,1000,900,unreal.TextureRenderTargetFormat.RTF_RGBA8)
capture.set_editor_property('texture_target',target)
original_material = a.sculpture_material
top = a.get_sphere_local_center().z + a.sphere_radius
frames = [
    ('Off',unreal.RadialSculptureGlowAnimation.NONE,0.),
    ('Leg13',unreal.RadialSculptureGlowAnimation.SEQUENTIAL_FINS,4.*14./17.),
    ('Leg14',unreal.RadialSculptureGlowAnimation.SEQUENTIAL_FINS,4.*15./17.),
    ('UpBase',unreal.RadialSculptureGlowAnimation.BOTTOM_TO_TOP,4.*75./(top+100.)),
    ('UpMiddle',unreal.RadialSculptureGlowAnimation.BOTTOM_TO_TOP,4.*350./(top+100.)),
    ('UpSphere',unreal.RadialSculptureGlowAnimation.BOTTOM_TO_TOP,4.*700./(top+100.)),
    ('CustomMaterial',unreal.RadialSculptureGlowAnimation.BOTTOM_TO_TOP,4.*350./(top+100.)),
    ('TallerLargerSphere',unreal.RadialSculptureGlowAnimation.NONE,0.),
]
state = dict(stage=-1,next=time.monotonic()+12,captures=0,export=False)
def stop():
    unreal.unregister_slate_post_tick_callback(state['handle'])
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
def tick(delta):
    if time.monotonic()<state['next']: return
    try:
        if state['captures']:
            capture.capture_scene()
            state['captures']-=1
            state.update(export=state['captures']==0,next=time.monotonic()+.5)
            return
        if state['export']:
            unreal.RenderingLibrary.export_render_target(world,target,str(out),frames[state['stage']][0]+'.png')
            state['export']=False
        state['stage']+=1
        if state['stage']==len(frames):
            result=dict(fresh_process_persistence=True,placement_and_existing_shape_preserved=True,
                        blueprint_spawn_defaults=True,saved_shader_matches_source=True,
                        rendered_frames=[f[0] for f in frames])
            (out/'Verification.json').write_text(json.dumps(result,indent=2))
            unreal.log('RADIAL_GLOW_VERIFY_OK '+json.dumps(result))
            stop()
            return
        name,mode,preview=frames[state['stage']]
        a.set_editor_property('glow_animation',mode)
        a.set_editor_property('glow_preview_time',preview)
        a.refresh_sculpture_glow()
        if name=='CustomMaterial':
            a.set_editor_property('sculpture_material',unreal.load_asset(folder+'/Materials/M_SculptureStage'))
        if name=='TallerLargerSphere':
            a.set_editor_property('sculpture_material',original_material)
            a.set_editor_property('fin_height',780.)
            a.set_editor_property('sphere_radius',160.)
            a.rebuild_sculpture()
            camera.set_actor_location_and_rotation(unreal.Vector(1400,-1800,1350),
                unreal.MathLibrary.find_look_at_rotation(unreal.Vector(1400,-1800,1350),unreal.Vector(0,0,500)),False,False)
        state.update(captures=3,next=time.monotonic()+.5)
    except Exception:
        stop()
        raise
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state['handle']=unreal.register_slate_post_tick_callback(tick)
