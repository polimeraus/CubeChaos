"""Save with -SetupParametricShape; reload in a fresh process to verify class defaults and placed instances."""
import json
import time
from pathlib import Path
import unreal

ROOT='/Game/GameplayActors/Platforms/ParametricPlatform'
MAIN=ROOT+'/BP_ParametricShape'
MAP=ROOT+'/Maps/L_ParametricShapeExamples'
OUT=Path(unreal.Paths.project_saved_dir())/'ParametricPlatform/ParametricShape'
OUT.mkdir(parents=True,exist_ok=True)
SETUP='-setupparametricshape' in unreal.SystemLibrary.get_command_line().lower()
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
SPECS=[
    ('Y_ThreeArms',{}),
    ('TwinGates',{'arm_count':2,'spread_degrees':110.,'rotation_degrees':90.,'center_radius':1050.,
                  'default_arm':{'length':2300.,'end_radius':850.,'neck_width':400.,'bend':.36}}),
    ('FiveArms',{'arm_count':5,'mirror_bends':False,'default_arm':{'bend':.36,'neck_width':320.,'end_radius':540.}}),
    ('Octopus',{'arm_count':8,'mirror_bends':False,'center_radius':950.,
                'default_arm':{'length':2300.,'end_radius':480.,'neck_width':300.,'end_width':750.,'bend':.32}}),
    ('Asymmetric',{'arm_count':4,'rotation_degrees':0.,'center_radius':650.,'arm_overrides':[
        {'arm_index':0,'angle_offset':-12.,'shape':{'length':2500.,'end_radius':900.,'bend':-.25,'neck_width':280.}},
        {'arm_index':1,'angle_offset':15.,'shape':{'length':1300.,'end_radius':480.,'bend':.4,'s_curve':.2}},
        {'arm_index':3,'enabled':False}]}),
    ('SCurve',{'arm_count':3,'spread_degrees':180.,'rotation_degrees':90.,'center_radius':700.,
               'default_arm':{'length':2500.,'neck_width':300.,'bend':.10,'s_curve':.60}}),
]
ARM_KEYS=('length','end_radius','root_width','neck_width','end_width','neck_position','bend','s_curve','end_offset')
SHAPE_KEYS=('center_radius','arm_count','rotation_degrees','spread_degrees','mirror_bends','junction_blend','uniform_scale','outline_resolution','curve_samples')
def apply(obj,values):
    for key,value in values.items():
        if key=='arm_overrides':
            items=[]
            for spec in value:
                item=unreal.PlatformParametricArmOverride()
                apply(item,spec);items.append(item)
            obj.set_editor_property(key,items)
        elif isinstance(value,dict):
            child=obj.get_editor_property(key);apply(child,value);obj.set_editor_property(key,child)
        else: obj.set_editor_property(key,value)
def settings_json(s):
    result={key:s.get_editor_property(key) for key in SHAPE_KEYS}
    result['default_arm']={key:s.get_editor_property('default_arm').get_editor_property(key) for key in ARM_KEYS}
    result['arm_overrides']=[]
    for item in s.get_editor_property('arm_overrides'):
        record={key:item.get_editor_property(key) for key in ('arm_index','enabled','angle_offset','override_shape')}
        record['shape']={key:item.get_editor_property('shape').get_editor_property(key) for key in ARM_KEYS}
        result['arm_overrides'].append(record)
    return result
def bp_path(index,name):
    return MAIN if index==0 else ROOT+'/Examples/ParametricShape/BP_ParametricShape_'+name
if SETUP:
    base=unreal.load_asset(ROOT+'/BP_ParametricPlatform')
    assert base
    unreal.BlueprintEditorLibrary.compile_blueprint(base)
    assert unreal.EditorAssetLibrary.save_loaded_asset(base,False)
    for index,(name,values) in enumerate(SPECS):
        path=bp_path(index,name)
        bp=unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
        if bp is None:
            factory=unreal.BlueprintFactory()
            factory.set_editor_property('parent_class',base.generated_class() if index==0 else unreal.load_asset(MAIN).generated_class())
            folder,asset_name=path.rsplit('/',1)
            bp=unreal.AssetToolsHelpers.get_asset_tools().create_asset(asset_name,folder,unreal.Blueprint,factory)
        cdo=unreal.get_default_object(bp.generated_class())
        settings=unreal.PlatformParametricShape()
        apply(settings,values)
        cdo.set_editor_property('parametric_shape_settings',settings)
        for key,value in {'outer_shape':unreal.ChunkedDecorativeFloorShape.PARAMETRIC_SHAPE,
            'platform_mode':unreal.PlatformMode.SOLID,'thickness':130.,'tile_size':unreal.Vector2D(220,220),
            'floor_pattern':unreal.ChunkedFloorPattern.SOLID,'border_style':unreal.PlatformBorder.NONE}.items():
            cdo.set_editor_property(key,value)
        colors=[(.48,.22,.62),(.12,.43,.61),(.08,.55,.45),(.95,.4,.09),(.75,.25,.36),(.3,.42,.75)]
        cdo.set_editor_property('palette_tints',[unreal.LinearColor(*colors[index],1)])
        unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        assert unreal.EditorAssetLibrary.save_loaded_asset(bp,False)
    if not unreal.EditorAssetLibrary.does_asset_exist(MAP):
        world=unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
        world.get_world_settings().set_editor_property('default_game_mode',unreal.GameModeBase)
        for index,(name,_) in enumerate(SPECS):
            bp=unreal.load_asset(bp_path(index,name))
            p=actors.spawn_actor_from_class(bp.generated_class(),unreal.Vector((index%3-1)*10000,(.5-index//3)*11000,0))
            p.set_actor_label(f'{index+1:02d}_{name}')
            p.set_folder_path('Parametric Shape')
        for rotation,intensity in [(unreal.Rotator(pitch=-65,yaw=-30),5),(unreal.Rotator(pitch=-35,yaw=150),2)]:
            light=actors.spawn_actor_from_class(unreal.DirectionalLight,unreal.Vector(0,0,15000),rotation)
            light.light_component.set_editor_property('intensity',intensity)
        assert unreal.EditorLoadingAndSavingUtils.save_map(world,MAP)
    else:
        world=unreal.EditorLoadingAndSavingUtils.load_map(MAP)
else:
    world=unreal.EditorLoadingAndSavingUtils.load_map(MAP)
assert world
placed={p.get_actor_label():p for p in actors.get_all_level_actors() if isinstance(p,unreal.ParametricPlatform)}
assert len(placed)==len(SPECS)
expected=None if SETUP else json.loads((OUT/'Setup.json').read_text())
results=[]
for index,(name,_) in enumerate(SPECS):
    bp=unreal.load_asset(bp_path(index,name))
    defaults=unreal.get_default_object(bp.generated_class())
    p=placed[f'{index+1:02d}_{name}']
    settings=settings_json(defaults.get_editor_property('parametric_shape_settings'))
    assert settings_json(p.get_editor_property('parametric_shape_settings'))==settings,name
    assert not p.get_editor_property('shape_warning'),(name,p.get_editor_property('shape_warning'))
    outline=[[v.x,v.y] for v in p.get_platform_outline()]
    assert len(outline)>32
    triangles=sum(c.get_dynamic_mesh().get_triangle_count() for c in p.get_components_by_class(unreal.DynamicMeshComponent))
    assert triangles>0 and p.get_platform_mesh_count()==1
    if expected:
        assert expected[index]['settings']==settings,(name,'defaults changed after reload')
        assert expected[index]['outline']==outline,(name,'geometry changed after reload')
    fresh=actors.spawn_actor_from_class(bp.generated_class(),unreal.Vector(0,30000,0))
    assert settings_json(fresh.get_editor_property('parametric_shape_settings'))==settings
    assert [[v.x,v.y] for v in fresh.get_platform_outline()]==outline
    actors.destroy_actor(fresh)
    size=p.get_platform_size()
    results.append({'name':name,'settings':settings,'size':[size.x,size.y],'outline':outline,
                    'holes':[[[v.x,v.y] for v in p.get_platform_hole(h)] for h in range(p.get_platform_hole_count())],
                    'triangles':triangles})
(OUT/('Setup.json' if SETUP else 'Persistence.json')).write_text(json.dumps(results,indent=2))
unreal.log('PARAMETRIC_SHAPE_PERSISTENCE_OK six saved classes, placed instances and new spawns')
if not SETUP and '-nullrhi' not in unreal.SystemLibrary.get_command_line().lower():
    position=unreal.Vector(0,-13000,53000)
    camera=actors.spawn_actor_from_class(unreal.SceneCapture2D,position,unreal.MathLibrary.find_look_at_rotation(position,unreal.Vector()))
    capture=camera.get_component_by_class(unreal.SceneCaptureComponent2D)
    for key,value in {'capture_every_frame':False,'capture_on_movement':False,'always_persist_rendering_state':True,
        'fov_angle':37,'capture_source':unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR}.items(): capture.set_editor_property(key,value)
    post=capture.get_editor_property('post_process_settings')
    for key,value in {'override_auto_exposure_method':True,'auto_exposure_method':unreal.AutoExposureMethod.AEM_MANUAL,
        'override_auto_exposure_apply_physical_camera_exposure':True,'auto_exposure_apply_physical_camera_exposure':False,
        'override_auto_exposure_bias':True,'auto_exposure_bias':0}.items(): post.set_editor_property(key,value)
    capture.set_editor_property('post_process_settings',post)
    target=unreal.RenderingLibrary.create_render_target2d(world,2100,1700,unreal.TextureRenderTargetFormat.RTF_RGBA8)
    capture.set_editor_property('texture_target',target)
    state={'next':time.monotonic()+15,'captures':0}
    def stop():
        unreal.unregister_slate_post_tick_callback(state['handle'])
        unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    def tick(_):
        if time.monotonic()<state['next']: return
        try:
            if state['captures']<4:
                capture.capture_scene();state.update(captures=state['captures']+1,next=time.monotonic()+1);return
            unreal.RenderingLibrary.export_render_target(world,target,str(OUT),'ParametricShape.png')
            unreal.log('PARAMETRIC_SHAPE_RENDERED');stop()
        except Exception:
            stop();raise
    unreal.EditorPythonScripting.set_keep_python_script_alive(True)
    state['handle']=unreal.register_slate_post_tick_callback(tick)
