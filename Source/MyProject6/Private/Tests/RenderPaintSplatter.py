"""Offscreen views of the native Paint Splatter generator; does not save the scene."""
from pathlib import Path
import time,json,unreal
out=Path(unreal.Paths.project_saved_dir())/'PaintSplatter';out.mkdir(parents=True,exist_ok=True)
before='-PaintBefore' in unreal.SystemLibrary.get_command_line()
world=unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
house=actors.spawn_actor_from_class(unreal.SimpleHouseActor,unreal.Vector())
building=house.get_editor_property('building')
for key,value in {'base_width':540,'base_depth':200,'floor_count':1,'floor_height':480,'wall_thickness':20}.items():building.set_editor_property(key,value)
house.set_editor_property('building',building)
facade=house.get_editor_property('front_facade');facade.set_editor_property('enable_openings',True)
door=facade.get_editor_property('door');door.set_editor_property('has_door',False);facade.set_editor_property('door',door)
settings=unreal.SimpleHouseFloorWindowSettings()
for key,value in {'has_windows':True,'window_count':1,'window_width':120,'window_height':140,
    'window_border_style':unreal.SimpleHouseWindowBorderStyle.PAINT_SPLATTER,
    'paint_spread':55,'paint_splatter_amount':85,'paint_splatter_seed':1337}.items():settings.set_editor_property(key,value)
parent=unreal.load_asset('/Game/LevelPrototyping/Materials/M_FlatCol')
wall=house.front_wall_component.create_dynamic_material_instance(0,parent)
wall.set_vector_parameter_value('Base Color',unreal.LinearColor(.62,.59,.49,1))
wall.set_scalar_parameter_value('Roughness',.9)
paint=house.front_wall_component.create_dynamic_material_instance(0,parent)
paint.set_vector_parameter_value('Base Color',unreal.LinearColor(.8,.003,.002,1))
paint.set_scalar_parameter_value('Roughness',.48)
settings.set_editor_property('window_object_materials',[paint])
facade.set_editor_property('floor_windows',[settings]);house.set_editor_property('front_facade',facade)
house.set_editor_property('material',wall);house.rebuild_mesh()
# Expose the true hole against the black background, including in the oblique view.
for part in [unreal.SimpleHousePart.ROOF,unreal.SimpleHousePart.CEILING,unreal.SimpleHousePart.GROUND,
             unreal.SimpleHousePart.BACK_WALL,unreal.SimpleHousePart.LEFT_WALL,unreal.SimpleHousePart.RIGHT_WALL]:house.set_part_visibility(part,False)
for rot,intensity in [(unreal.Rotator(pitch=-20,yaw=80),4),(unreal.Rotator(pitch=-10,yaw=100),2)]:
    light=actors.spawn_actor_from_class(unreal.DirectionalLight,unreal.Vector(0,-500,650),rot)
    light.light_component.set_editor_property('intensity',intensity)
camera=actors.spawn_actor_from_class(unreal.SceneCapture2D,unreal.Vector(0,-870,240),unreal.Rotator(yaw=90))
capture=camera.get_component_by_class(unreal.SceneCaptureComponent2D)
for key,val in {'capture_every_frame':False,'capture_on_movement':False,'always_persist_rendering_state':True,
                'capture_source':unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR,'fov_angle':42}.items():capture.set_editor_property(key,val)
target=unreal.RenderingLibrary.create_render_target2d(world,1200,1080,unreal.TextureRenderTargetFormat.RTF_RGBA8)
capture.set_editor_property('texture_target',target)
post=capture.get_editor_property('post_process_settings')
for key,val in {'override_auto_exposure_method':True,'auto_exposure_method':unreal.AutoExposureMethod.AEM_MANUAL,
    'override_auto_exposure_apply_physical_camera_exposure':True,'auto_exposure_apply_physical_camera_exposure':False,
    'override_auto_exposure_bias':True,'auto_exposure_bias':0,'override_bloom_intensity':True,'bloom_intensity':0}.items():post.set_editor_property(key,val)
capture.set_editor_property('post_process_settings',post)
(out/'RenderScene.json').write_text(json.dumps({'house_bounds':str(house.get_actor_bounds(False)),
    'front_triangles':house.front_wall_component.get_dynamic_mesh().get_triangle_count(),
    'camera':str(camera.get_actor_transform()),'window_components':len(house.window_object_components),
    'visibility':str(house.part_visibility)},indent=2))
views=[('Before' if before else 'After',unreal.SimpleHouseWindowType.RECTANGULAR,1337)]
if not before:views += [('Oblique',unreal.SimpleHouseWindowType.RECTANGULAR,1337),('Arched',unreal.SimpleHouseWindowType.ARCHED,42),
                        ('Rounded',unreal.SimpleHouseWindowType.ROUNDED_CORNERS,871),('Circular',unreal.SimpleHouseWindowType.CIRCULAR,319)]
state={'index':0,'next':time.monotonic()+12,'captures':3}
def close():
    unreal.unregister_slate_post_tick_callback(state['handle']);unreal.EditorPythonScripting.set_keep_python_script_alive(False)
def tick(delta):
    if time.monotonic()<state['next']:return
    try:
        if state['captures']:
            capture.capture_scene();state['captures']-=1;state['next']=time.monotonic()+1;return
        name,shape,seed=views[state['index']]
        unreal.RenderingLibrary.export_render_target(world,target,str(out),name+'.png')
        state['index']+=1
        if state['index']==len(views):unreal.log('PAINT_RENDER_OK');close();return
        name,shape,seed=views[state['index']]
        pos=unreal.Vector(310,-840,310) if name=='Oblique' else unreal.Vector(0,-870,240)
        camera.set_actor_location_and_rotation(pos,unreal.MathLibrary.find_look_at_rotation(pos,unreal.Vector(0,-100,240)),False,False)
        settings.set_editor_property('window_type',shape);settings.set_editor_property('paint_splatter_seed',seed)
        facade.set_editor_property('floor_windows',[settings]);house.set_editor_property('front_facade',facade);house.rebuild_mesh()
        state['captures']=3;state['next']=time.monotonic()+1
    except Exception:close();raise
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state['handle']=unreal.register_slate_post_tick_callback(tick)
