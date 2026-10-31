"""Reload saved assets/map in a fresh editor and render the actual first-person rig."""
from pathlib import Path
import json, time, unreal

root=Path(unreal.Paths.project_dir()).resolve();out=root/'Saved/PurpleRailgun/Preview';out.mkdir(parents=True,exist_ok=True)
dark_preview='-PurpleDarkPreview' in unreal.SystemLibrary.get_command_line()
if dark_preview:
    out=root/'Saved/PurpleRailgun/PreviewDark';out.mkdir(parents=True,exist_ok=True)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem);level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
base='/Game/Weapons/PurpleRailgun'
mesh=unreal.load_asset(base+'/Meshes/SM_PurpleRailgun');assert mesh
assert mesh.get_bounds().origin.y>15, 'Barrel must face +Y in grip space'
assert mesh.find_socket('Muzzle') and mesh.find_socket('SupportHand')
animation=unreal.load_asset(base+'/Animations/A_PurpleRailgunHold');assert animation
player_class=unreal.load_class(None,'/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C')
cdo=unreal.get_default_object(player_class);visuals=cdo.get_editor_property('purple_railgun_visuals')
assert visuals.color.r>.7 and visuals.color.g<.05 and visuals.color.b>.9
assert cdo.railgun_visuals.color.b>.9 and cdo.railgun_visuals.color.r<.05
pickup_class=unreal.load_class(None,'/Game/GameplayActors/Pickups/PurpleRailgun/BP_PurpleRailgunPickup.BP_PurpleRailgunPickup_C')
pd=unreal.get_default_object(pickup_class)
assert pd.weapon_reward==unreal.PickupWeapon.PURPLE_RAILGUN and pd.icon==unreal.PickupMotif.PURPLE_RAILGUN
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
placed=[a for a in actors.get_all_level_actors() if a.get_class()==pickup_class]
assert len(placed)==1
pickup=placed[0];assert pickup.weapon_reward==pd.weapon_reward and pickup.icon==pd.icon
placement=json.loads((out.parent/'Placement.json').read_text())
by_label={a.get_actor_label():a for a in actors.get_all_level_actors()}
for old in placement['existing']:
    a=by_label[old['label']];p,r,s=a.get_actor_location(),a.get_actor_rotation(),a.get_actor_scale3d()
    actual=[p.x,p.y,p.z,r.pitch,r.yaw,r.roll,s.x,s.y,s.z]
    assert all(abs(x-y)<.01 for x,y in zip(actual,old['pose'])),old['label']
report={'saved_defaults_verified':True,'pickup':pickup.get_actor_label(),'location':str(pickup.get_actor_location()),
        'existing_pickups_preserved':len(placement['existing']),'mesh_bounds':str(mesh.get_bounds())}
editor_world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
anchor=pickup.get_actor_location()
floor_hit=unreal.SystemLibrary.line_trace_single(editor_world,anchor+unreal.Vector(0,0,100),anchor-unreal.Vector(0,0,500),
    unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,False,[pickup],unreal.DrawDebugTrace.NONE,True)
assert floor_hit and floor_hit.to_dict()['blocking_hit'],'New pickup must be above a walkable surface'
floor_data=floor_hit.to_dict();report['floor_point']=str(floor_data['impact_point'])
assert floor_data['impact_normal'].z>.7 and abs(floor_data['impact_point'].z-anchor.z)<100,'Pickup floor is too far or steep'
(out.parent/'Persistence.json').write_text(json.dumps(report,indent=2))
unreal.log('PURPLE_RAILGUN_PERSISTENCE_OK')
if '-PurplePersistenceOnly' in unreal.SystemLibrary.get_command_line():
    raise SystemExit

world=unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
player=actors.spawn_actor_from_class(player_class,unreal.Vector(0,0,100))
camera=player.get_component_by_class(unreal.CameraComponent)
camera.detach_from_component(unreal.DetachmentRule.KEEP_WORLD,unreal.DetachmentRule.KEEP_WORLD,unreal.DetachmentRule.KEEP_WORLD)
camera.set_world_location_and_rotation(unreal.Vector(0,0,170),unreal.Rotator(),False,False)
gun=actors.spawn_actor_from_class(unreal.RailgunWeapon,unreal.Vector());gun.equip(player);gun.activate_purple_railgun()
gun.arms.set_editor_property('only_owner_see',False);gun.weapon_mesh.set_editor_property('only_owner_see',False)
gun.arms.set_update_animation_in_editor(True)
hand_points={}
for name in ['hand_r','index_03_r','middle_01_r','middle_03_r','hand_l','index_03_l','middle_01_l','middle_03_l']:
    p=unreal.MathLibrary.inverse_transform_location(gun.weapon_mesh.get_world_transform(),gun.arms.get_socket_location(name))
    hand_points[name]=[p.x,p.y,p.z]
(out.parent/'HandContacts.json').write_text(json.dumps(hand_points,indent=2))
for m in player.get_components_by_class(unreal.SkeletalMeshComponent):m.set_visibility(False)
for rotation,intensity in [(unreal.Rotator(-45,-30,0),1.25 if dark_preview else 56.),(unreal.Rotator(-25,150,0),.45 if dark_preview else 24.)]:
    light=actors.spawn_actor_from_class(unreal.DirectionalLight,unreal.Vector(0,0,500),rotation);light.light_component.set_editor_property('intensity',intensity)
sky=actors.spawn_actor_from_class(unreal.SkyLight,unreal.Vector(0,0,500))
sky.light_component.set_editor_property('source_type',unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
sky.light_component.set_editor_property('cubemap',unreal.load_asset('/Engine/MapTemplates/Sky/DaylightAmbientCubemap'))
sky.light_component.set_editor_property('intensity',.45 if dark_preview else 1.5)
cube=unreal.load_asset('/Engine/BasicShapes/Cube')
for position,scale in [(unreal.Vector(900,0,-5),unreal.Vector(35,30,.1)),(unreal.Vector(1700,0,250),unreal.Vector(.3,20,5))]:
    block=actors.spawn_actor_from_class(unreal.StaticMeshActor,position);block.static_mesh_component.set_static_mesh(cube);block.set_actor_scale3d(scale)
    mat=block.static_mesh_component.create_dynamic_material_instance(0,unreal.load_asset('/Game/LevelPrototyping/Materials/M_FlatCol'))
    mat.set_vector_parameter_value('Base Color',unreal.LinearColor(.045,.05,.06,1))
capture_actor=actors.spawn_actor_from_class(unreal.SceneCapture2D,camera.get_world_location(),camera.get_world_rotation())
capture=capture_actor.get_component_by_class(unreal.SceneCaptureComponent2D)
for k,v in dict(capture_every_frame=False,always_persist_rendering_state=True,capture_source=unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR,
    fov_angle=90.,enable_first_person_field_of_view=True,first_person_field_of_view=70.,enable_first_person_scale=True,first_person_scale=.6).items():capture.set_editor_property(k,v)
target=unreal.RenderingLibrary.create_render_target2d(world,1280,720,unreal.TextureRenderTargetFormat.RTF_RGBA8);capture.texture_target=target
post=capture.get_editor_property('post_process_settings')
for k,v in dict(override_auto_exposure_method=True,auto_exposure_method=unreal.AutoExposureMethod.AEM_MANUAL,
    override_auto_exposure_bias=True,auto_exposure_bias=10.).items():post.set_editor_property(k,v)
capture.set_editor_property('post_process_settings',post)
if dark_preview:
    post.set_editor_property('override_auto_exposure_apply_physical_camera_exposure',True)
    post.set_editor_property('auto_exposure_apply_physical_camera_exposure',False)
    post.set_editor_property('auto_exposure_bias',0.)
    capture.set_editor_property('post_process_settings',post)
state={'stage':0,'time':time.monotonic()}
beam=actors.spawn_actor_from_class(unreal.RailgunBeam,unreal.Vector());beam.visuals=visuals
beam.set_actor_hidden_in_game(True)
preview_pickup=actors.spawn_actor_from_class(pickup_class,unreal.Vector(160,0,80))
preview_pickup.set_editor_property('auto_collect',False);preview_pickup.set_editor_property('preview_motion_in_editor',False)
preview_pickup.set_actor_rotation(unreal.Rotator(0,25,0),False);preview_pickup.set_actor_hidden_in_game(True)
def capture_now():
    capture.capture_scene();state['time']=time.monotonic()
def save(name):unreal.RenderingLibrary.export_render_target(world,target,str(out),name+'.png')
def close():
    unreal.unregister_slate_post_tick_callback(state['handle']);level.editor_request_end_play();unreal.EditorPythonScripting.set_keep_python_script_alive(False)
def tick(delta):
    global world,player,camera,gun,beam,capture,capture_actor,target,preview_pickup
    if state['stage']==-1:
        game=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
        if not game:return
        world=game
        players=unreal.GameplayStatics.get_all_actors_of_class(world,player_class)
        player=next(p for p in players if p.get_actor_label()==state['player_label'])
        player.set_actor_tick_enabled(False);player.character_movement.disable_movement()
        camera=player.get_component_by_class(unreal.CameraComponent)
        for g in unreal.GameplayStatics.get_all_actors_of_class(world,unreal.RailgunWeapon):g.set_actor_hidden_in_game(True)
        gun=player.railgun;gun.set_actor_hidden_in_game(False);gun.activate_purple_railgun();gun.set_actor_tick_enabled(False)
        gun.arms.set_editor_property('only_owner_see',False);gun.weapon_mesh.set_editor_property('only_owner_see',False)
        beam=unreal.GameplayStatics.get_all_actors_of_class(world,unreal.RailgunBeam)[0];beam.set_actor_tick_enabled(False)
        preview_pickup=unreal.GameplayStatics.get_all_actors_of_class(world,pickup_class)[0]
        preview_pickup.set_actor_tick_enabled(False)
        capture_actor=unreal.GameplayStatics.get_all_actors_of_class(world,unreal.SceneCapture2D)[0]
        capture=capture_actor.get_component_by_class(unreal.SceneCaptureComponent2D)
        target=unreal.RenderingLibrary.create_render_target2d(world,1280,720,unreal.TextureRenderTargetFormat.RTF_RGBA8);capture.texture_target=target
        state.update(stage=0,time=time.monotonic());return
    if time.monotonic()-state['time']<(15 if state['stage']==0 else 2):return
    try:
        stage=state['stage']
        if stage==0:capture_now()
        elif stage==1:
            save('FirstPerson');beam.set_actor_hidden_in_game(False)
            beam.initialize_beam(gun.get_visual_muzzle_location(),unreal.Vector(1680,0,170),.65)
            beam.set_life_span(0)
            beam.set_impact(unreal.Vector(1680,0,170));beam.face_camera(camera.get_world_location());beam.set_preview_age(.08);capture_now()
        elif stage==2:capture_now()
        elif stage==3:
            save('Firing');beam.set_actor_hidden_in_game(True)
            capture.set_editor_property('enable_first_person_field_of_view',False);capture.set_editor_property('enable_first_person_scale',False)
            capture.set_editor_property('fov_angle',55.)
            capture_actor.set_actor_location_and_rotation(unreal.Vector(80,-110,175),unreal.MathLibrary.find_look_at_rotation(unreal.Vector(80,-110,175),unreal.Vector(80,14,148)),False,False);capture_now()
        elif stage==4:
            save('Grip');gun.set_actor_hidden_in_game(True)
            preview_pickup.set_actor_hidden_in_game(False)
            capture_actor.set_actor_location_and_rotation(unreal.Vector(0,-170,180),unreal.MathLibrary.find_look_at_rotation(unreal.Vector(0,-170,180),unreal.Vector(160,0,110)),False,False);capture_now()
        elif stage==5:capture_now()
        else:
            save('Pickup');unreal.log('PURPLE_RAILGUN_RENDER_OK');close();return
        state['stage']+=1;state['time']=time.monotonic()
    except Exception:
        close();raise
state.update(stage=-1,player_label=player.get_actor_label())
unreal.EditorPythonScripting.set_keep_python_script_alive(True);state['handle']=unreal.register_slate_post_tick_callback(tick)
level.editor_play_simulate()
