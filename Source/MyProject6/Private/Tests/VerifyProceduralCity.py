"""Fresh-process saved-asset checks and optional rendered layout documentation.
Use -NullRHI for validation only; pass -CityRender with a real RHI for screenshots.
Never saves or changes the example maps on disk.
"""
import json
import time
from pathlib import Path
import unreal

ROOT='/Game/Procedural/CityGenerator'
out=Path(unreal.Paths.project_saved_dir())/'ProceduralCity'
preview=Path(unreal.Paths.project_dir())/'Docs/ProceduralCity/Images'
preview.mkdir(parents=True,exist_ok=True)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
bp=unreal.load_asset(ROOT+'/BP_ProceduralCityGenerator')
assert bp
cdo=unreal.get_default_object(bp.generated_class())
assert len(cdo.building_catalog)==5 and cdo.generate_on_begin_play and cdo.new_seed_on_begin_play
expected=json.loads((out/'Setup.json').read_text())['maps']
results=[]
for entry in expected:
    world=unreal.EditorLoadingAndSavingUtils.load_map(entry['map'])
    assert world
    city=next(a for a in actors.get_all_level_actors() if isinstance(a,unreal.ProceduralCityGenerator))
    assert city.generated_plan.signature==entry['signature']
    owned=list(city.get_generated_actors())
    assert len(owned)==entry['actors']==city.generated_actor_count
    assert all(a.get_owner()==city and a.actor_has_tag('ProceduralCity.Generated') for a in owned)
    links=[a for a in owned if isinstance(a,unreal.ParametricCityLink)]
    assert links and all(a.get_dynamic_mesh_component().get_dynamic_mesh().get_triangle_count()>0 and not a.generation_warning for a in links)
    assert all(a.get_dynamic_mesh_component().get_material(0) for a in links)
    buildings=[a for a in owned if any(a.get_class()==e.actor_class for e in city.building_catalog)]
    assert len(buildings)==entry['buildings']
    assert city.validate_current_settings(),city.last_error
    assert city.rebuild_same_seed(),city.last_error
    assert city.generated_plan.signature==entry['signature']
    assert len(city.get_generated_actors())==len(owned)
    tagged=[a for a in actors.get_all_level_actors() if a.get_owner()==city and a.actor_has_tag('ProceduralCity.Generated')]
    assert len(tagged)==len(owned),'Orphan/duplicate generated actors after reload'
    results.append(dict(entry,fresh_reload=True,same_seed_matches=True,no_duplicates=True))

unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
fresh=actors.spawn_actor_from_class(bp.generated_class(),unreal.Vector())
for key in ('layout','district_count','min_district_radius','max_district_radius','district_gap','position_jitter',
            'round_platform_chance','base_elevation','elevation_step','elevation_levels','road_width',
            'extra_connection_chance','curvature','max_ramp_slope','stair_chance','max_step_rise','min_step_tread',
            'max_stair_height','guard_rails','min_buildings_per_district','max_buildings_per_district',
            'building_clearance','platform_edge_setback','building_access_width','hub_landmark',
            'max_total_buildings','max_estimated_building_cells','placement_attempts'):
    assert fresh.rules.get_editor_property(key)==cdo.rules.get_editor_property(key),key
for a,b in zip(fresh.building_catalog,cdo.building_catalog):
    for key in ('id','actor_class','weight','footprint_radius','pivot_offset','min_scale','max_scale','front_yaw','estimated_cells','allow_as_landmark'):
        assert a.get_editor_property(key)==b.get_editor_property(key),key
assert fresh.generate_with_seed(expected[0]['seed']),fresh.last_error
assert fresh.generated_plan.signature==expected[0]['signature']
assert fresh.generated_actor_count==expected[0]['actors']
report={'fresh_process_persistence':True,'new_blueprint_instance_inherits_catalog_and_rules':True,'maps':results}
(out/'Persistence.json').write_text(json.dumps(report,indent=2))
unreal.log('PROCEDURAL_CITY_PERSISTENCE_OK')

if '-CityRender' in unreal.SystemLibrary.get_command_line():
    views=[('Radial','Overview'),('Radial','Top'),('Radial','Hub'),('Radial','Link'),('Grid','Overview'),('Grid','Top'),('Organic','Overview'),('Organic','Top')]
    state={'index':0,'next':time.monotonic()+1,'captures':0,'export':False,'loaded':None,'started':time.monotonic(),'busy':False}
    def stop(error=None):
        unreal.unregister_slate_post_tick_callback(state['handle'])
        (out/'Render.json').write_text(json.dumps({'passed':error is None,'error':error,'views':views},indent=2))
        if error:unreal.log_error('CITY_RENDER_FAILED '+error)
        else:unreal.log('PROCEDURAL_CITY_RENDER_OK')
        unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    def tick(dt):
        if state['busy'] or time.monotonic()<state['next']:return
        state['busy']=True
        try:
            assert time.monotonic()-state['started']<600,'Render timeout'
            if state['captures']:
                state['capture'].capture_scene();state['captures']-=1
                state.update(export=state['captures']==0,next=time.monotonic()+.5)
                return
            if state['export']:
                mode,name=views[state['index']]
                unreal.RenderingLibrary.export_render_target(state['world'],state['target'],str(preview),mode+'_'+name+'.png')
                state.update(index=state['index']+1,export=False)
                if state['index']==len(views):stop();return
            mode,name=views[state['index']]
            if state['loaded']!=mode:
                world=unreal.EditorLoadingAndSavingUtils.load_map(ROOT+'/Maps/Lvl_City_'+mode)
                city=next(a for a in actors.get_all_level_actors() if isinstance(a,unreal.ProceduralCityGenerator))
                camera=actors.spawn_actor_from_class(unreal.SceneCapture2D,unreal.Vector())
                capture=camera.get_component_by_class(unreal.SceneCaptureComponent2D)
                for key,value in {'capture_every_frame':False,'capture_on_movement':False,'always_persist_rendering_state':True,
                                  'fov_angle':48.,'capture_source':unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR}.items():
                    capture.set_editor_property(key,value)
                post=capture.get_editor_property('post_process_settings')
                for key,value in {'override_auto_exposure_method':True,'auto_exposure_method':unreal.AutoExposureMethod.AEM_MANUAL,
                                  'override_auto_exposure_apply_physical_camera_exposure':True,'auto_exposure_apply_physical_camera_exposure':False,
                                  'override_auto_exposure_bias':True,'auto_exposure_bias':1.5}.items():
                    post.set_editor_property(key,value)
                capture.set_editor_property('post_process_settings',post)
                target=unreal.RenderingLibrary.create_render_target2d(world,1800,1300,unreal.TextureRenderTargetFormat.RTF_RGBA8)
                capture.set_editor_property('texture_target',target)
                state.update(loaded=mode,world=world,city=city,camera=camera,capture=capture,target=target)
                unreal.SystemLibrary.execute_console_command(world,'r.AntiAliasingMethod 1')
            city=state['city'];camera=state['camera'];capture=state['capture']
            ds=list(city.generated_plan.districts)
            xmin=min(d.center.x-d.radius for d in ds);xmax=max(d.center.x+d.radius for d in ds)
            ymin=min(d.center.y-d.radius for d in ds);ymax=max(d.center.y+d.radius for d in ds)
            center=unreal.Vector((xmin+xmax)/2,(ymin+ymax)/2,350)
            extent=max(xmax-xmin,(ymax-ymin)*1800/1300)
            capture.set_editor_property('projection_type',unreal.CameraProjectionMode.PERSPECTIVE)
            location=center+unreal.Vector(extent*.60,-extent*.92,extent*1.26)
            look=center
            if name=='Top':
                capture.set_editor_property('projection_type',unreal.CameraProjectionMode.ORTHOGRAPHIC)
                capture.set_editor_property('ortho_width',extent*1.15)
                location=center+unreal.Vector(0,0,60000)
            elif name=='Hub':
                look=ds[0].center+unreal.Vector(0,0,350)
                location=look+unreal.Vector(4200,-6500,6200)
            elif name=='Link':
                links=list(city.generated_plan.connections)
                link=next((c for c in links if c.kind==unreal.CityLinkKind.STAIRS),links[0])
                pts=list(link.points);look=(pts[0]+pts[-1])*.5
                span=max(1600.,(pts[-1]-pts[0]).length())
                direction=pts[-1]-pts[0];normal=unreal.Vector(-direction.y,direction.x,0).normal()
                location=look+normal*span*.75+unreal.Vector(0,0,span*.65)
            camera.set_actor_location(location,False,False)
            camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(location,look),False)
            if name=='Top':camera.set_actor_rotation(unreal.Rotator(pitch=-90,yaw=-90),False)
            state.update(captures=6,next=time.monotonic()+8)
        except Exception as error:stop(str(error))
        finally:state['busy']=False
    unreal.EditorPythonScripting.set_keep_python_script_alive(True)
    state['handle']=unreal.register_slate_post_tick_callback(tick)
