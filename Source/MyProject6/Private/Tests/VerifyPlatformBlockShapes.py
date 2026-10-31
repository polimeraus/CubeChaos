"""Save with -SetupPlatformBlockShapes; reload in a separate process to verify class and instance persistence."""
import json
import time
from pathlib import Path
import unreal

ROOT = '/Game/GameplayActors/Platforms/ParametricPlatform'
MAP = ROOT + '/Maps/L_ParametricPlatformBlockShapes'
CHILD = ROOT + '/Examples/BP_ParametricPlatform_RoundedU'
out = Path(unreal.Paths.project_saved_dir()) / 'ParametricPlatform' / 'BlockShapes'
out.mkdir(parents=True, exist_ok=True)
setup = '-setupplatformblockshapes' in unreal.SystemLibrary.get_command_line().lower()
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
bp = unreal.load_asset(ROOT + '/BP_ParametricPlatform')
assert bp
if setup:
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    assert unreal.EditorAssetLibrary.save_loaded_asset(bp, False)
defaults = unreal.get_default_object(bp.generated_class())
native = unreal.get_default_object(unreal.ParametricPlatform)
assert defaults.get_editor_property('corner_radius') == native.get_editor_property('corner_radius') == 0
assert defaults.get_editor_property('pad_path_settings').get_editor_property('surface_material')
assert len(defaults.get_editor_property('pad_path_settings').get_editor_property('pads')) == 5

if setup and not unreal.EditorAssetLibrary.does_asset_exist(CHILD):
    factory = unreal.BlueprintFactory()
    factory.set_editor_property('parent_class', bp.generated_class())
    child = unreal.AssetToolsHelpers.get_asset_tools().create_asset('BP_ParametricPlatform_RoundedU', ROOT+'/Examples', unreal.Blueprint, factory)
    cdo = unreal.get_default_object(child.generated_class())
    for key, value in {'outer_shape':unreal.ChunkedDecorativeFloorShape.U_SHAPE, 'shape_size':unreal.Vector2D(1200,1500),
                       'corner_radius':90., 'corner_segments':12, 'floor_pattern':unreal.ChunkedFloorPattern.CIRCLES_AND_PATHS}.items():
        cdo.set_editor_property(key, value)
    settings = cdo.get_editor_property('block_shape')
    settings.set_editor_property('rows', 4)
    cdo.set_editor_property('block_shape',settings)
    paint = cdo.get_editor_property('pad_path_settings')
    paint.set_editor_property('pattern_color',unreal.LinearColor(.1,.85,1,1))
    cdo.set_editor_property('pad_path_settings',paint)
    unreal.BlueprintEditorLibrary.compile_blueprint(child)
    assert unreal.EditorAssetLibrary.save_loaded_asset(child,False)

records = []
if setup and not unreal.EditorAssetLibrary.does_asset_exist(MAP):
    world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    world.get_world_settings().set_editor_property('default_game_mode',unreal.GameModeBase)
    colors = [(0.7,.018,.28),(.05,.52,.7),(1,.21,.012),(.36,.29,.045),(.52,.28,.55),(.025,.31,.28)]
    core = [('L_SHAPE',{}),('T_SHAPE',{'rows':2}),('ZIGZAG',{'rows':2}),('U_SHAPE',{}),('CROSS',{}),
            ('CUSTOM_BLOCKS',{'blocks':[(0,1),(1,0),(2,0)],'diagonal_bridge_width':120.})]
    extra = [('SQUARE',{}),('GRID',{}),('L_SHAPE',{'mirror':True}),('ZIGZAG',{'rows':2,'mirror':True}),
             ('U_SHAPE',{'quarter_turns':1}),('CUSTOM_BLOCKS',{'blocks':[(1,0),(2,0),(0,1),(1,1),(1,2),(2,2)]})]
    for row in range(4):
        for col, (shape, block_values) in enumerate(extra if row == 2 else core):
            mode = unreal.PlatformMode.CELLULAR if row == 3 else unreal.PlatformMode.SOLID
            location = unreal.Vector((col-2.5)*1400,(row-1.5)*1500,0)
            p = actors.spawn_actor_from_class(bp.generated_class(),location)
            label = f'{row+1}_{col+1}_{shape}'
            p.set_actor_label(label)
            for key,value in {'platform_mode':mode,'outer_shape':getattr(unreal.ChunkedDecorativeFloorShape,shape),
                              'shape_size':unreal.Vector2D(1000,360 if row == 2 and col == 1 else 1000),
                              'corner_radius':0. if row == 0 else 130.,'corner_segments':12,'thickness':100.,
                              'tile_size':unreal.Vector2D(100,100),'border_style':unreal.PlatformBorder.NONE,
                              'floor_pattern':unreal.ChunkedFloorPattern.SOLID,'use_destruction_ripple':False}.items():
                p.set_editor_property(key,value)
            p.set_editor_property('palette_tints',[unreal.LinearColor(*colors[col],1),unreal.LinearColor(1,.7,.1,1),unreal.LinearColor(.1,.8,1,1)])
            block = p.get_editor_property('block_shape')
            for key,value in block_values.items():
                if key == 'blocks': value = [unreal.IntPoint(x,y) for x,y in value]
                block.set_editor_property(key,value)
            p.set_editor_property('block_shape',block)
            p.rebuild_platform()
            assert not p.get_editor_property('shape_warning'), (label,p.get_editor_property('shape_warning'))
            records.append({'label':label,'shape':shape,'mode':'CELLULAR' if row == 3 else 'SOLID','radius':0 if row == 0 else 130,
                            'blocks':block_values, 'location':[location.x,location.y,location.z]})
    for rot,intensity in [(unreal.Rotator(pitch=-65,yaw=-30),5),(unreal.Rotator(pitch=-35,yaw=150),2)]:
        light = actors.spawn_actor_from_class(unreal.DirectionalLight,unreal.Vector(0,0,5000),rot)
        light.light_component.set_editor_property('intensity',intensity)
    assert unreal.EditorLoadingAndSavingUtils.save_map(world,MAP)
    (out/'Expected.json').write_text(json.dumps(records,indent=2))
    unreal.log('PLATFORM_BLOCK_SHAPES_SAVED')
else:
    world = unreal.EditorLoadingAndSavingUtils.load_map(MAP)
    assert world
    records = json.loads((out/'Expected.json').read_text())

platforms = {p.get_actor_label():p for p in actors.get_all_level_actors() if isinstance(p,unreal.ParametricPlatform)}
assert len(platforms) == len(records) == 24
results = []
for record in records:
    p = platforms[record['label']]
    assert p.get_editor_property('outer_shape') == getattr(unreal.ChunkedDecorativeFloorShape,record['shape'])
    assert p.get_editor_property('platform_mode') == getattr(unreal.PlatformMode,record['mode'])
    assert p.get_editor_property('corner_radius') == record['radius']
    assert p.get_actor_location() == unreal.Vector(*record['location'])
    assert not p.get_editor_property('shape_warning'),p.get_editor_property('shape_warning')
    block = p.get_editor_property('block_shape')
    for key,value in record['blocks'].items():
        if key == 'blocks': value = [unreal.IntPoint(x,y) for x,y in value]
        assert block.get_editor_property(key) == value,(record['label'],key)
    count = sum(c.get_dynamic_mesh().get_triangle_count() for c in p.get_components_by_class(unreal.DynamicMeshComponent))
    assert count > 0,record['label']
    results.append({'label':record['label'],'triangles':count})

child = unreal.load_asset(CHILD)
assert child
child_defaults = unreal.get_default_object(child.generated_class())
assert child_defaults.get_editor_property('corner_radius') == 90
assert child_defaults.get_editor_property('outer_shape') == unreal.ChunkedDecorativeFloorShape.U_SHAPE
assert child_defaults.get_editor_property('block_shape').get_editor_property('rows') == 4
fresh = actors.spawn_actor_from_class(child.generated_class(),unreal.Vector(0,15000,0))
assert fresh.get_editor_property('corner_radius') == 90
assert fresh.get_editor_property('corner_segments') == 12
assert fresh.get_editor_property('floor_pattern') == unreal.ChunkedFloorPattern.CIRCLES_AND_PATHS
assert fresh.get_editor_property('pad_path_settings').get_editor_property('pattern_color') == unreal.LinearColor(.1,.85,1,1)
assert not fresh.get_editor_property('shape_warning') and not fresh.get_editor_property('pad_path_warning')
assert fresh.get_platform_mesh_count() == 1
actors.destroy_actor(fresh)
(out/('Setup.json' if setup else 'Persistence.json')).write_text(json.dumps({'class_defaults_verified':True,'examples':results},indent=2))
unreal.log('PLATFORM_BLOCK_SHAPES_PERSISTENCE_OK ' + json.dumps(results))

if not setup and '-nullrhi' not in unreal.SystemLibrary.get_command_line().lower():
    damaged = platforms['4_1_L_SHAPE']
    damaged.set_editor_property('flash_duration',0.)
    # Damage the lower arm, whose middle is inside the L even though its origin is in empty space.
    removed = damaged.destroy_cells_in_radius(damaged.get_actor_location()+unreal.Vector(0,-340,-30),120,unreal.ChunkedFloorBlastShape.SPHERE)
    assert removed > 0
    position = unreal.Vector(0,-4500,12000)
    camera = actors.spawn_actor_from_class(unreal.SceneCapture2D,position,unreal.MathLibrary.find_look_at_rotation(position,unreal.Vector()))
    capture = camera.get_component_by_class(unreal.SceneCaptureComponent2D)
    for key,value in {'capture_every_frame':False,'capture_on_movement':False,'always_persist_rendering_state':True,
                      'fov_angle':42,'capture_source':unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR}.items(): capture.set_editor_property(key,value)
    post = capture.get_editor_property('post_process_settings')
    for key,value in {'override_auto_exposure_method':True,'auto_exposure_method':unreal.AutoExposureMethod.AEM_MANUAL,
                      'override_auto_exposure_apply_physical_camera_exposure':True,'auto_exposure_apply_physical_camera_exposure':False,
                      'override_auto_exposure_bias':True,'auto_exposure_bias':0}.items(): post.set_editor_property(key,value)
    capture.set_editor_property('post_process_settings',post)
    target = unreal.RenderingLibrary.create_render_target2d(world,2100,1600,unreal.TextureRenderTargetFormat.RTF_RGBA8)
    capture.set_editor_property('texture_target',target)
    state = {'next':time.monotonic()+12,'captures':0}
    def stop():
        unreal.unregister_slate_post_tick_callback(state['handle'])
        unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    def tick(_):
        if time.monotonic() < state['next']: return
        try:
            if state['captures'] < 4:
                capture.capture_scene(); state.update(captures=state['captures']+1,next=time.monotonic()+1); return
            unreal.RenderingLibrary.export_render_target(world,target,str(out),'BlockShapes.png')
            unreal.log('PLATFORM_BLOCK_SHAPES_RENDERED'); stop()
        except Exception: stop(); raise
    unreal.EditorPythonScripting.set_keep_python_script_alive(True)
    state['handle'] = unreal.register_slate_post_tick_callback(tick)
