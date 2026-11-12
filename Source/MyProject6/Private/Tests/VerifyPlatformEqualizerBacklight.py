"""Fresh-process shader comparison on the real patterned platform; no map changes are saved."""
import json
import time
from pathlib import Path
import unreal

out = Path(unreal.Paths.project_saved_dir()) / 'ParametricPlatform' / 'EqualizerBacklight'
out.mkdir(parents=True, exist_ok=True)
world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
cls = unreal.load_asset('/Game/GameplayActors/Platforms/ParametricPlatform/Examples/BP_ParametricPlatform_Lobby').generated_class()
assert abs(unreal.get_default_object(cls).get_editor_property('equalizer_transparency')-.8)<.00001
floor = actors.spawn_actor_from_class(cls, unreal.Vector())
for key, value in {'shape_size':unreal.Vector2D(1800,1400), 'thickness':120.,
                   'corner_radius':180., 'react_to_music':False, 'platform_mode':unreal.PlatformMode.SOLID}.items():
    floor.set_editor_property(key,value)
floor.rebuild_platform()
stock = unreal.load_asset('/Game/Materials/Structures/M_PlanarAnimation')
backlight = unreal.load_asset('/Game/Materials/Structures/M_PlatformEqualizerBacklight')
assert stock and backlight
assert backlight.get_editor_property('blend_mode') == unreal.BlendMode.BLEND_ADDITIVE
# An explicitly saved old stock reference must also pick up the upgraded platform renderer.
floor.set_editor_property('planar_animation_material', stock)
floor.set_planar_animation_type(unreal.PlanarAnimationType.FLOWING_WAVE)
floor.play_fixed_animation()

for rotation, intensity in [(unreal.Rotator(pitch=-55,yaw=-30),5), (unreal.Rotator(pitch=-25,yaw=140),2)]:
    light = actors.spawn_actor_from_class(unreal.DirectionalLight,unreal.Vector(0,0,3000),rotation)
    light.light_component.set_editor_property('intensity',intensity)
location = unreal.Vector(1600,-2200,3000)
camera = actors.spawn_actor_from_class(unreal.SceneCapture2D,location,unreal.MathLibrary.find_look_at_rotation(location,unreal.Vector()))
capture = camera.get_component_by_class(unreal.SceneCaptureComponent2D)
for key,value in {'capture_every_frame':False,'capture_on_movement':False,'always_persist_rendering_state':True,
                  'fov_angle':39.,'capture_source':unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR}.items():
    capture.set_editor_property(key,value)
post = capture.get_editor_property('post_process_settings')
for key,value in {'override_auto_exposure_method':True,'auto_exposure_method':unreal.AutoExposureMethod.AEM_MANUAL,
                  'override_auto_exposure_apply_physical_camera_exposure':True,'auto_exposure_apply_physical_camera_exposure':False,
                  'override_auto_exposure_bias':True,'auto_exposure_bias':0}.items():
    post.set_editor_property(key,value)
capture.set_editor_property('post_process_settings',post)
target = unreal.RenderingLibrary.create_render_target2d(world,1200,900,unreal.TextureRenderTargetFormat.RTF_RGBA8)
capture.set_editor_property('texture_target',target)
white = unreal.load_asset('/Engine/EngineResources/WhiteSquareTexture')
assert white
state = {'next':time.monotonic()+15,'phase':0,'captures':0,'configured':False}
results = {}
cases = [('BaseMaterial', 0.8), ('PreviousEqualizer', 0.0),
         ('Transparency0', 0.0), ('Transparency50', 0.5),
         ('BacklitEqualizer', 0.8), ('Transparency100', 1.0)]


def configure(phase):
    floor.stop_fixed_animation()
    floor.set_editor_property('equalizer_transparency', cases[phase][1])
    floor.play_fixed_animation()
    surfaces = [c for c in floor.get_components_by_class(unreal.DynamicMeshComponent) if c.component_has_tag('PlanarAnimationSurface')]
    assert surfaces
    if phase == 0:
        assert surfaces[0].get_material(0).get_editor_property('parent') == backlight, 'Old saved stock reference was not upgraded'
        results['legacy_reference_uses_backlight'] = True
    for surface in surfaces:
        surface.set_visibility(phase != 0)
        material = surface.get_material(0)
        assert abs(material.get_scalar_parameter_value('EqualizerTransparency')-cases[phase][1])<.00001, 'Actor transparency was not sent to the shader'
        if phase == 1:
            material = surface.create_dynamic_material_instance(0, stock)
        material.set_vector_parameter_value('Footprint',unreal.LinearColor(1800,1400,.5,.5))
        material.set_vector_parameter_value('Fixed',unreal.LinearColor(0,0,0,0))
        material.set_vector_parameter_value('WaveColor',unreal.LinearColor(0,0,0,0))
        material.set_vector_parameter_value('Equalizer',unreal.LinearColor(12,10,2,1))
        material.set_vector_parameter_value('MusicColor',unreal.LinearColor(42,42,42,1))
        material.set_scalar_parameter_value('MusicEnabled',1)
        material.set_texture_parameter_value('MusicMask',white)


def finish(error=None):
    unreal.unregister_slate_post_tick_callback(state['handle'])
    results.update(passed=error is None,error=error)
    (out/'Result.json').write_text(json.dumps(results,indent=2))
    (unreal.log_error if error else unreal.log)('PLATFORM_BACKLIGHT_RENDER_' + ('FAILED '+str(error) if error else 'OK'))
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)


def tick(_):
    if time.monotonic()<state['next']:
        return
    try:
        if not state['configured']:
            configure(state['phase'])
            state['configured']=True
        capture.capture_scene()
        state['captures']+=1
        state['next']=time.monotonic()+1
        if state['captures']<4:
            return
        name = cases[state['phase']][0]
        unreal.RenderingLibrary.export_render_target(world,target,str(out),name+'.png')
        if state['phase']==len(cases)-1:
            finish()
        else:
            state.update(phase=state['phase']+1,captures=0,configured=False)
    except Exception as error:
        finish(str(error))


unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state['handle']=unreal.register_slate_post_tick_callback(tick)
