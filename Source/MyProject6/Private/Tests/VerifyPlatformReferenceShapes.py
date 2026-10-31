"""Create reference shape examples with -SetupReferenceShapes, then verify in a fresh process.

Reference coverage (images numbered in upload order, including duplicate image 7):
1-5: existing Rectangle/L/J/U/C/S/Z, plus DiagonalLink, OffsetLink, OffsetFork,
     OffsetArch, SteppedArch, UnequalU and SteppedBranch. 6-7: FingerCorner, ForkNotch, NotchedBar.
8: SteppedBlock/PiercedSteppedBlock. 9: HalfRing (two opening sizes).
10: PiercedTrapezoid/NotchedTrapezoid. 11: CombArch/PiercedCapsule.
12: RoundU/PiercedCapsule. 13: InterlockTab/InterlockSocket.
14: WaistedPier/Ring/HalfRing/HalfDisc. 15-16: VNotchHalfDisc,
PiercedDiamond/NotchedWedge (rotated/mirrored pairs).
Repeated colors, back layers and cropped scene plinths are not additional pieces.
"""
import json
import time
from pathlib import Path
import unreal

ROOT = '/Game/GameplayActors/Platforms/ParametricPlatform'
FOLDER = ROOT + '/Examples/ReferenceShapes'
MAP = ROOT + '/Maps/L_ParametricPlatformReferenceShapes'
OUT = Path(unreal.Paths.project_saved_dir()) / 'ParametricPlatform/ReferenceShapes'
OUT.mkdir(parents=True, exist_ok=True)
SETUP = '-setupreferenceshapes' in unreal.SystemLibrary.get_command_line().lower()
SPECS = [
    ('DiagonalLink','DIAGONAL_LINK',700,700), ('OffsetLink','OFFSET_LINK',1000,670),
    ('OffsetFork','OFFSET_FORK',850,850), ('OffsetArch','OFFSET_ARCH',900,900),
    ('SteppedArch','STEPPED_ARCH',1000,750), ('SteppedBlock','STEPPED_BLOCK',850,850),
    ('PiercedSteppedBlock','PIERCED_STEPPED_BLOCK',850,850), ('NotchedBar','NOTCHED_BAR',950,475),
    ('FingerCorner','FINGER_CORNER',600,980), ('ForkNotch','FORK_NOTCH',950,850),
    ('PiercedTrapezoid','PIERCED_TRAPEZOID',850,850), ('NotchedTrapezoid','NOTCHED_TRAPEZOID',850,850),
    ('PiercedCapsule','PIERCED_CAPSULE',470,940), ('RoundU','ROUND_U',900,900),
    ('CombArch','COMB_ARCH',1000,750), ('InterlockTab','INTERLOCK_TAB',650,980),
    ('InterlockSocket','INTERLOCK_SOCKET',490,980), ('WaistedPier','WAISTED_PIER',500,1000),
    ('Ring','RING',850,850), ('HalfRing','HALF_RING',1000,500),
    ('HalfDisc','HALF_DISC',1000,500), ('VNotchHalfDisc','V_NOTCH_HALF_DISC',1000,500),
    ('PiercedDiamond','PIERCED_DIAMOND',900,900), ('NotchedWedge','NOTCHED_WEDGE',700,900),
    ('UnequalU','UNEQUAL_U',900,900), ('SteppedBranch','STEPPED_BRANCH',900,900),
]
HOLED = {'PiercedSteppedBlock','PiercedTrapezoid','PiercedCapsule','InterlockTab','InterlockSocket','Ring','PiercedDiamond'}
COLORS = [(1,.40,.015),(.1,.55,.75),(.9,.25,.4),(.28,.65,.5),(.55,.4,.8),(.8,.72,.4)]
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
base = unreal.load_asset(ROOT + '/BP_ParametricPlatform')
assert base
if SETUP:
    unreal.BlueprintEditorLibrary.compile_blueprint(base)
    assert unreal.EditorAssetLibrary.save_loaded_asset(base,False)
    for index,(name,enum,width,height) in enumerate(SPECS):
        path=FOLDER+'/BP_Platform_'+name
        bp=unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
        if bp is None:
            factory=unreal.BlueprintFactory()
            factory.set_editor_property('parent_class',base.generated_class())
            bp=unreal.AssetToolsHelpers.get_asset_tools().create_asset('BP_Platform_'+name,FOLDER,unreal.Blueprint,factory)
        cdo=unreal.get_default_object(bp.generated_class())
        for key,value in {'outer_shape':getattr(unreal.ChunkedDecorativeFloorShape,enum),
            'shape_size':unreal.Vector2D(width,height),'thickness':100.,'tile_size':unreal.Vector2D(120,120),
            'platform_mode':unreal.PlatformMode.SOLID,'border_style':unreal.PlatformBorder.NONE,
            'floor_pattern':unreal.ChunkedFloorPattern.SOLID,'corner_radius':0.}.items():
            cdo.set_editor_property(key,value)
        cdo.set_editor_property('reference_shape',unreal.PlatformReferenceShape())
        cdo.set_editor_property('palette_tints',[unreal.LinearColor(*COLORS[index%6],1)])
        unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        assert unreal.EditorAssetLibrary.save_loaded_asset(bp,False)
    if not unreal.EditorAssetLibrary.does_asset_exist(MAP):
        world=unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
        world.get_world_settings().set_editor_property('default_game_mode',unreal.GameModeBase)
        for index,(name,enum,width,height) in enumerate(SPECS):
            bp=unreal.load_asset(FOLDER+'/BP_Platform_'+name)
            p=actors.spawn_actor_from_class(bp.generated_class(),unreal.Vector((index%6-2.5)*1300,(2-index//6)*1300,0))
            p.set_actor_label(f'{index+1:02d}_{name}')
            p.set_folder_path('Reference Shapes')
        for rotation,intensity in [(unreal.Rotator(pitch=-65,yaw=-30),5),(unreal.Rotator(pitch=-35,yaw=150),2)]:
            light=actors.spawn_actor_from_class(unreal.DirectionalLight,unreal.Vector(0,0,5000),rotation)
            light.light_component.set_editor_property('intensity',intensity)
        assert unreal.EditorLoadingAndSavingUtils.save_map(world,MAP)
    else:
        world=unreal.EditorLoadingAndSavingUtils.load_map(MAP)
        assert world
else:
    world=unreal.EditorLoadingAndSavingUtils.load_map(MAP)
    assert world

placed={p.get_actor_label():p for p in actors.get_all_level_actors() if isinstance(p,unreal.ParametricPlatform)}
assert len(placed)==len(SPECS)
records=[]
for index,(name,enum,width,height) in enumerate(SPECS):
    bp=unreal.load_asset(FOLDER+'/BP_Platform_'+name)
    defaults=unreal.get_default_object(bp.generated_class())
    p=placed[f'{index+1:02d}_{name}']
    expected_shape=getattr(unreal.ChunkedDecorativeFloorShape,enum)
    assert defaults.get_editor_property('outer_shape')==p.get_editor_property('outer_shape')==expected_shape
    assert defaults.get_editor_property('shape_size')==p.get_editor_property('shape_size')==unreal.Vector2D(width,height)
    assert not p.get_editor_property('shape_warning'),(name,p.get_editor_property('shape_warning'))
    assert p.get_platform_hole_count()==(1 if name in HOLED else 0),name
    count=sum(c.get_dynamic_mesh().get_triangle_count() for c in p.get_components_by_class(unreal.DynamicMeshComponent))
    assert count>0,name
    fresh=actors.spawn_actor_from_class(bp.generated_class(),unreal.Vector(0,12000,0))
    assert fresh.get_editor_property('outer_shape')==expected_shape
    assert fresh.get_platform_hole_count()==p.get_platform_hole_count()
    assert fresh.get_platform_mesh_count()==1 and not fresh.get_editor_property('shape_warning')
    actors.destroy_actor(fresh)
    records.append({'name':name,'enum':enum,'size':[width,height],'triangles':count,
        'outline':[[v.x,v.y] for v in p.get_platform_outline()],
        'holes':[[[v.x,v.y] for v in p.get_platform_hole(h)] for h in range(p.get_platform_hole_count())]})
(OUT/('Setup.json' if SETUP else 'Persistence.json')).write_text(json.dumps(records,indent=2))
unreal.log(f'REFERENCE_SHAPES_PERSISTENCE_OK {len(SPECS)} class defaults, placed actors and fresh spawns')

if not SETUP and '-nullrhi' not in unreal.SystemLibrary.get_command_line().lower():
    position=unreal.Vector(0,-2800,15800)
    camera=actors.spawn_actor_from_class(unreal.SceneCapture2D,position,unreal.MathLibrary.find_look_at_rotation(position,unreal.Vector()))
    capture=camera.get_component_by_class(unreal.SceneCaptureComponent2D)
    for key,value in {'capture_every_frame':False,'capture_on_movement':False,'always_persist_rendering_state':True,
        'fov_angle':36,'capture_source':unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR}.items():
        capture.set_editor_property(key,value)
    post=capture.get_editor_property('post_process_settings')
    for key,value in {'override_auto_exposure_method':True,'auto_exposure_method':unreal.AutoExposureMethod.AEM_MANUAL,
        'override_auto_exposure_apply_physical_camera_exposure':True,'auto_exposure_apply_physical_camera_exposure':False,
        'override_auto_exposure_bias':True,'auto_exposure_bias':0}.items(): post.set_editor_property(key,value)
    capture.set_editor_property('post_process_settings',post)
    target=unreal.RenderingLibrary.create_render_target2d(world,2400,2100,unreal.TextureRenderTargetFormat.RTF_RGBA8)
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
            unreal.RenderingLibrary.export_render_target(world,target,str(OUT),'ReferenceShapes.png')
            unreal.log('REFERENCE_SHAPES_RENDERED');stop()
        except Exception:
            stop();raise
    unreal.EditorPythonScripting.set_keep_python_script_alive(True)
    state['handle']=unreal.register_slate_post_tick_callback(tick)
