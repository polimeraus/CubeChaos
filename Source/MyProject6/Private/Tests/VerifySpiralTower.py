"""Fresh saved/native/BP defaults, real renders, destruction and runtime spawn. No asset writes."""
import json, math, time
from pathlib import Path
import unreal

folder='/Game/Structures/Destructible/SpiralTower'
out=Path(unreal.Paths.project_saved_dir())/'SpiralTower/ClearReference/Preview'
out.mkdir(parents=True,exist_ok=True)
manifest=json.loads((out.parent/'SavedInstances.json').read_text())
features=Path(unreal.Paths.project_saved_dir())/'TowerFeatures/SavedInstances.json'
if features.exists():
    data=json.loads(features.read_text())
    manifest=dict(keys=data['keys']['Spiral'],blueprints=[r['path'] for r in data['blueprints'] if r['kind']=='Spiral'],
                  instances=[r for r in data['instances'] if r['kind']=='Spiral'])
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
native_defaults=unreal.get_default_object(unreal.ChunkedDestructibleSpiralTower)
def check_reference(a):
    for key in set(manifest['keys'])|{'clockwise'}:
        actual,expected=a.get_editor_property(key),native_defaults.get_editor_property(key)
        if key=='traversal':actual,expected=actual.export_text(),expected.export_text()
        assert actual==expected,(a.get_name(),key)
def pose(a):
    p,r,s=a.get_actor_location(),a.get_actor_rotation(),a.get_actor_scale3d()
    return [p.x,p.y,p.z,r.pitch,r.yaw,r.roll,s.x,s.y,s.z]
for path in manifest['blueprints']:
    check_reference(unreal.get_default_object(unreal.load_asset(path).generated_class()))
for path in sorted(set(row['map'] for row in manifest['instances'])):
    assert unreal.EditorLoadingAndSavingUtils.load_map(path)
    for row in [r for r in manifest['instances'] if r['map']==path]:
        a=next(a for a in actors.get_all_level_actors() if a.get_actor_label()==row['label'] and a.get_class().get_path_name()==row['cls'])
        check_reference(a)
        assert pose(a)==row['transform'] and str(a.get_folder_path())==row['folder']
        assert a.generated_piece_count==row['pieces'] and a.generated_height==row['height'] and not a.generation_warning
world=unreal.EditorLoadingAndSavingUtils.load_map(folder+'/Maps/Lvl_SpiralTowerPreview')
assert world
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
bp=unreal.load_asset(folder+'/BP_ChunkedDestructibleSpiralTower')
cdo=unreal.get_default_object(bp.generated_class())
placed=[a for a in actors.get_all_level_actors() if isinstance(a,unreal.ChunkedDestructibleSpiralTower)]
assert len(placed)==1
tower=placed[0]
def triangles(actor):
    return sum(c.get_dynamic_mesh().get_triangle_count() for c in actor.get_components_by_class(unreal.DynamicMeshComponent))
fresh=actors.spawn_actor_from_class(bp.generated_class(),unreal.Vector(8000,0,0))
native=actors.spawn_actor_from_class(unreal.ChunkedDestructibleSpiralTower,unreal.Vector(-8000,0,0))
assert triangles(fresh)==triangles(tower)==triangles(native)==tower.generated_piece_count*24
keys=('base_radius','top_radius','turns','rise_per_turn','start_angle','clockwise',
      'walkway_width','steps_per_turn','slab_thickness','wall_height','wall_thickness','wall_inset',
      'foundation','foundation_height','foundation_margin','fit_stairs_between_walls','summit_wall_extension','top_rise_scale','reverse_summit_crescent','piece_size','piece_gap',
      'stone_material','stair_material','default_cell_health','use_destruction_ripple')
for name in keys:
    assert tower.get_editor_property(name)==fresh.get_editor_property(name)==cdo.get_editor_property(name),name
    assert native.get_editor_property(name)==cdo.get_editor_property(name),('native',name)
for component in tower.get_components_by_class(unreal.DynamicMeshComponent):
    assert component.get_num_materials()==3 and all(component.get_material(i) for i in range(3))
actors.destroy_actor(fresh)
actors.destroy_actor(native)
results={'fresh_process_persistence':True,'native_and_blueprint_defaults_match':True,
         'saved_instances_verified':len(manifest['instances']),
         'pieces':tower.generated_piece_count,'steps':tower.generated_step_count,
         'triangles':triangles(tower),'height_cm':tower.generated_height,'clear_shaft_cm':tower.clear_shaft_diameter}

camera=actors.spawn_actor_from_class(unreal.SceneCapture2D,unreal.Vector())
capture=camera.get_component_by_class(unreal.SceneCaptureComponent2D)
for key,value in {'capture_every_frame':False,'capture_on_movement':False,'always_persist_rendering_state':True,
                  'fov_angle':31.,'capture_source':unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR,
                  'projection_type':unreal.CameraProjectionMode.ORTHOGRAPHIC,'ortho_width':4200.}.items():
    capture.set_editor_property(key,value)
post=capture.get_editor_property('post_process_settings')
for key,value in {'override_auto_exposure_method':True,'auto_exposure_method':unreal.AutoExposureMethod.AEM_MANUAL,
                  'override_auto_exposure_apply_physical_camera_exposure':True,'auto_exposure_apply_physical_camera_exposure':False,
                  'override_auto_exposure_bias':True,'auto_exposure_bias':0.,
                  'override_ambient_occlusion_intensity':True,'ambient_occlusion_intensity':.8}.items():
    post.set_editor_property(key,value)
capture.set_editor_property('post_process_settings',post)
target=unreal.RenderingLibrary.create_render_target2d(world,2140,2940,unreal.TextureRenderTargetFormat.RTF_RGBA8)
capture.set_editor_property('texture_target',target)
views=['Reference','Hollow','Inside','LowerSidewall','Damage']
state={'next':time.monotonic()+15,'index':0,'captures':0,'export':False,'runtime':False}
def stop():
    unreal.unregister_slate_post_tick_callback(state['handle'])
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
def tick(dt):
    if state.get('busy') or time.monotonic()<state['next']:return
    state['busy']=True
    try:
        if state['runtime']:
            game=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
            if not game:
                assert time.monotonic()<state['deadline'],'PIE timed out'
                return
            runtime=unreal.GameplayStatics.get_all_actors_of_class(game,unreal.ChunkedDestructibleSpiralTower)
            assert len(runtime)==1 and triangles(runtime[0])==results['triangles']
            original=runtime[0]
            unreal.SystemLibrary.execute_console_command(game,'EnableCheats')
            unreal.SystemLibrary.execute_console_command(game,'Summon '+bp.generated_class().get_path_name())
            runtime=unreal.GameplayStatics.get_all_actors_of_class(game,unreal.ChunkedDestructibleSpiralTower)
            assert len(runtime)==2
            spawned=next(a for a in runtime if a!=original)
            assert triangles(spawned)==results['triangles'] and not spawned.is_actor_tick_enabled()
            spawned.set_editor_property('turns',2.5)
            spawned.rebuild_tower()
            assert triangles(spawned)!=results['triangles'] and not spawned.generation_warning
            results.update(runtime_spawn=True,runtime_rebuild=True,idle_tick_disabled=True)
            (out/'Verification.json').write_text(json.dumps(results,indent=2))
            unreal.log('SPIRAL_TOWER_VERIFIED')
            unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
            stop()
            return
        if state['captures']:
            capture.capture_scene()
            state['captures']-=1
            state.update(export=state['captures']==0,next=time.monotonic()+.4)
            return
        if state['export']:
            unreal.RenderingLibrary.export_render_target(world,target,str(out),views[state['index']]+'.png')
            state['index']+=1
            state['export']=False
            if state['index']==len(views):
                (out/'RenderMetrics.json').write_text(json.dumps(results,indent=2))
                if '-SpiralRenderOnly' in unreal.SystemLibrary.get_command_line():
                    unreal.log('SPIRAL_TOWER_RENDER_OK')
                    stop();return
                unreal.EditorLoadingAndSavingUtils.load_map(folder+'/Maps/Lvl_SpiralTowerPreview')
                state.update(runtime=True,next=time.monotonic()+5,deadline=time.monotonic()+60)
                unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_play_simulate()
                return
        name=views[state['index']]
        if state.get('inspection_light'):
            actors.destroy_actor(state.pop('inspection_light'))
        location,look=unreal.Vector(0,-10000,8335),unreal.Vector(0,0,1735)
        capture.set_editor_property('projection_type',unreal.CameraProjectionMode.ORTHOGRAPHIC)
        capture.set_editor_property('ortho_width',4200.)
        capture.set_editor_property('fov_angle',31.)
        if name=='Hollow':
            location,look=unreal.Vector(-1600,-2500,6700),unreal.Vector(0,0,2500)
            capture.set_editor_property('ortho_width',4000.)
            capture.set_editor_property('fov_angle',53.)
        if name=='LowerSidewall':
            location,look=unreal.Vector(6500,6000,3000),unreal.Vector(700,500,650)
            capture.set_editor_property('ortho_width',2800.)
        if name=='Inside':
            location,look=unreal.Vector(0,0,600),unreal.Vector(700,0,2000)
            capture.set_editor_property('projection_type',unreal.CameraProjectionMode.PERSPECTIVE)
            capture.set_editor_property('fov_angle',100.)
            # A temporary inspection light makes the unlit room readable in this view only.
            light=actors.spawn_actor_from_class(unreal.PointLight,unreal.Vector(0,0,1800))
            component=light.get_component_by_class(unreal.PointLightComponent)
            for key,value in {'mobility':unreal.ComponentMobility.MOVABLE,'use_inverse_squared_falloff':False,
                              'intensity':3.,'attenuation_radius':7000.}.items():
                component.set_editor_property(key,value)
            state['inspection_light']=light
        if name=='Damage':
            tower.set_editor_property('flash_duration',0.)
            tower.set_editor_property('play_destroy_tick_sound',False)
            tower.set_editor_property('use_destruction_ripple',False)
            removed=0
            facing=math.degrees(math.atan2(location.y,location.x))
            front_turn=((facing-tower.start_angle)/(-360 if tower.clockwise else 360))%1
            for turn in (front_turn,front_turn+1,front_turn+2):
                angle=math.radians(tower.start_angle)+(-1 if tower.clockwise else 1)*2*math.pi*turn
                radius=tower.base_radius+(tower.top_radius-tower.base_radius)*turn/tower.turns-tower.walkway_width
                radius=(radius+tower.wall_thickness*.5)*(1-tower.wall_inset*(180/tower.wall_height)/radius)
                base=tower.foundation_height if tower.foundation else 0
                height=tower.rise_per_turn*(turn-(1-tower.top_rise_scale)*turn*turn/(2*tower.turns))
                point=unreal.Vector(math.cos(angle)*radius,math.sin(angle)*radius,base+tower.slab_thickness+height+180)
                removed+=tower.destroy_cells_in_radius(point,225,unreal.ChunkedFloorBlastShape.CIRCLE)
            assert removed>5 and triangles(tower)==results['triangles']-24*removed
            results['explosion_removed_pieces']=removed
        camera.set_actor_location(location,False,False)
        camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(location,look),False)
        state.update(captures=8,next=time.monotonic()+2)
    except Exception:
        stop()
        raise
    finally:
        state['busy']=False
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state['handle']=unreal.register_slate_post_tick_callback(tick)
