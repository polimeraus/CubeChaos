"""Create the parametric destructible Babylon Tower, sandstone palette and isolated preview level."""
import json
from pathlib import Path
import unreal

folder='/Game/Structures/BabylonTower'
assets=unreal.AssetToolsHelpers.get_asset_tools()
edit=unreal.MaterialEditingLibrary
out=Path(unreal.Paths.project_saved_dir())/'BabylonTower'
out.mkdir(parents=True,exist_ok=True)
def load(path):
    return unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None

master=load(folder+'/M_BabylonSandstone')
refresh='-RefreshBabylonPresentation' in unreal.SystemLibrary.get_command_line()
if not master:
    master=assets.create_asset('M_BabylonSandstone',folder,unreal.Material,unreal.MaterialFactoryNew())
    tint=edit.create_material_expression(master,unreal.MaterialExpressionVectorParameter,-650,0)
    tint.set_editor_property('parameter_name','SandstoneColor')
    tint.set_editor_property('default_value',unreal.LinearColor(.64,.4,.2,1))
    rough=edit.create_material_expression(master,unreal.MaterialExpressionScalarParameter,-200,350)
    rough.set_editor_property('parameter_name','Roughness')
    rough.set_editor_property('default_value',.92)
    position=edit.create_material_expression(master,unreal.MaterialExpressionWorldPosition,-850,150)
    normal=edit.create_material_expression(master,unreal.MaterialExpressionVertexNormalWS,-850,300)
    shader=edit.create_material_expression(master,unreal.MaterialExpressionCustom,-200,0)
    pins=[]
    for name in ('Position','Normal','Tint'):
        pin=unreal.CustomInput();pin.set_editor_property('input_name',name);pins.append(pin)
    shader.set_editor_property('inputs',pins)
    shader.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    shader.set_editor_property('code',r"""
float3 p=Position;
float3 n=abs(Normal);
float2 uv=n.z>.65?p.xy:float2(n.y>n.x?p.x:p.y,p.z);
float row=floor(uv.y/23.0);
float2 grid=float2(uv.x/48.0+frac(row*.5)*.9,uv.y/23.0);
float2 cell=floor(grid), f=frac(grid);
float hash=frac(sin(dot(cell,float2(127.1,311.7)))*43758.5453);
float chips=sin(uv.x*.29+sin(uv.y*.18))*sin(uv.y*.39)*.012;
float joint=min(min(f.x,1-f.x),min(f.y,1-f.y));
float mortar=smoothstep(.012,.032+chips,joint);
float grain=sin(p.x*1.3+p.z*.91)*sin(p.y*.83-p.z*1.7)*.025;
float mottling=sin(p.x*.021+p.z*.035)*sin(p.y*.038-p.z*.028)*.055;
return Tint*(.84+hash*.2+grain+mottling)*lerp(.73,1.0,mortar);
""")
    edit.connect_material_expressions(position,'',shader,'Position')
    edit.connect_material_expressions(normal,'',shader,'Normal')
    edit.connect_material_expressions(tint,'RGB',shader,'Tint')
    edit.connect_material_property(shader,'',unreal.MaterialProperty.MP_BASE_COLOR)
    edit.connect_material_property(rough,'',unreal.MaterialProperty.MP_ROUGHNESS)
    edit.layout_material_expressions(master)
    assert not edit.recompile_material(master)
    assert unreal.EditorAssetLibrary.save_loaded_asset(master,False)

palette={}
for name,color in {'Stone':(.64,.40,.20),'Cornice':(.66,.42,.21),'Terrace':(.70,.47,.25),
                   'Portal':(.62,.38,.19),'Foundation':(.60,.37,.18),'Recess':(.49,.29,.13),
                   'Stage':(.29,.24,.17)}.items():
    material=load(folder+'/MI_Babylon'+name)
    if not material:
        material=assets.create_asset('MI_Babylon'+name,folder,unreal.MaterialInstanceConstant,unreal.MaterialInstanceConstantFactoryNew())
        edit.set_material_instance_parent(material,master)
        edit.set_material_instance_vector_parameter_value(material,'SandstoneColor',unreal.LinearColor(*color,1))
        assert unreal.EditorAssetLibrary.save_loaded_asset(material,False)
    if name!='Stage':palette[name.lower()+'_material']=material

bp=load(folder+'/BP_ChunkedDestructibleBabylonTower')
if not bp:
    factory=unreal.BlueprintFactory()
    factory.set_editor_property('parent_class',unreal.ChunkedDestructibleBabylonTower)
    bp=assets.create_asset('BP_ChunkedDestructibleBabylonTower',folder,unreal.Blueprint,factory)
    defaults=unreal.get_default_object(bp.generated_class())
    for key,value in palette.items():defaults.set_editor_property(key,value)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    assert unreal.EditorAssetLibrary.save_loaded_asset(bp,False)

path=folder+'/Lvl_BabylonTowerPreview'
if not unreal.EditorAssetLibrary.does_asset_exist(path):
    world=unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    temple=actors.spawn_actor_from_class(bp.generated_class(),unreal.Vector())
    temple.set_actor_label('ChunkedDestructibleBabylonTower')
    temple.set_folder_path('Babylon Tower')
    assert temple.generated_piece_count>100 and not temple.generation_warning
    for label,rotation,intensity in [('Key',unreal.Rotator(pitch=-38,yaw=65,roll=0),5.),
                                      ('Fill',unreal.Rotator(pitch=-30,yaw=155,roll=0),2.)]:
        light=actors.spawn_actor_from_class(unreal.DirectionalLight,unreal.Vector(0,0,3000),rotation)
        light.set_actor_label('Preview_'+label)
        light.set_folder_path('Preview')
        light.light_component.set_editor_property('intensity',intensity)
        light.light_component.set_editor_property('mobility',unreal.ComponentMobility.MOVABLE)
        if label=='Fill':light.light_component.set_editor_property('cast_shadows',False)
        else:light.light_component.set_editor_property('atmosphere_sun_light',True)
    atmosphere=actors.spawn_actor_from_class(unreal.SkyAtmosphere,unreal.Vector())
    atmosphere.set_folder_path('Preview')
    sky=actors.spawn_actor_from_class(unreal.SkyLight,unreal.Vector(0,0,2500))
    sky.set_folder_path('Preview')
    c=sky.get_component_by_class(unreal.SkyLightComponent)
    c.set_editor_property('mobility',unreal.ComponentMobility.MOVABLE)
    c.set_editor_property('source_type',unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
    c.set_editor_property('cubemap',unreal.load_asset('/Engine/MapTemplates/Sky/DaylightAmbientCubemap'))
    c.set_editor_property('intensity',.85)
    c.set_editor_property('cast_shadows',False)
    ground=actors.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(0,0,-25))
    ground.set_actor_label('Preview_Ground')
    ground.set_folder_path('Preview')
    ground.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
    ground.static_mesh_component.set_material(0,load(folder+'/MI_BabylonStage'))
    ground.set_actor_scale3d(unreal.Vector(1000,1000,.5))
    start=actors.spawn_actor_from_class(unreal.PlayerStart,unreal.Vector(0,-2600,125),unreal.Rotator(pitch=0,yaw=90,roll=0))
    start.set_folder_path('Preview')
    location=unreal.Vector(4200,-9300,2800)
    unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(
        location,unreal.MathLibrary.find_look_at_rotation(location,unreal.Vector(0,0,1850)))
    actors.set_selected_level_actors([temple])
    assert unreal.EditorLoadingAndSavingUtils.save_map(world,path)
    (out/'Setup.json').write_text(json.dumps({'blueprint':bp.get_path_name(),'map':path,
        'pieces':temple.generated_piece_count,'openings':temple.generated_opening_count,
        'height_cm':temple.generated_height},indent=2))

# A small amount of studio ambient light keeps the two cornice bands legible
# even in projects where global illumination is disabled.
world=unreal.EditorLoadingAndSavingUtils.load_map(path)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
ambient=next((a for a in actors.get_all_level_actors() if a.get_actor_label()=='Preview_Ambient'),None)
changed=refresh or not ambient
if not ambient:
    ambient=actors.spawn_actor_from_class(unreal.PostProcessVolume,unreal.Vector())
    ambient.set_actor_label('Preview_Ambient')
    ambient.set_folder_path('Preview')
    ambient.set_editor_property('unbound',True)
if changed:
    post=ambient.get_editor_property('settings')
    for key,value in {'ambient_cubemap':unreal.load_asset('/Engine/MapTemplates/Sky/DaylightAmbientCubemap'),
                      'override_ambient_cubemap_intensity':True,'ambient_cubemap_intensity':.6,
                      'override_ambient_cubemap_tint':True,'ambient_cubemap_tint':unreal.LinearColor(1.,.94,.85,1.),
                      'override_auto_exposure_method':True,'auto_exposure_method':unreal.AutoExposureMethod.AEM_MANUAL,
                      'override_auto_exposure_apply_physical_camera_exposure':True,'auto_exposure_apply_physical_camera_exposure':False,
                      'override_auto_exposure_bias':True,'auto_exposure_bias':.2}.items():
        post.set_editor_property(key,value)
    ambient.set_editor_property('settings',post)
    bounce=next((a for a in actors.get_all_level_actors() if a.get_actor_label()=='Preview_Bounce'),None)
    if not bounce:
        bounce=actors.spawn_actor_from_class(unreal.DirectionalLight,unreal.Vector(0,0,100))
        bounce.set_actor_label('Preview_Bounce')
        bounce.set_folder_path('Preview')
    # Approximate reflected light from sand without changing project-wide GI settings.
    bounce.set_actor_rotation(unreal.Rotator(pitch=25,yaw=45,roll=0),False)
    bounce.light_component.set_editor_property('mobility',unreal.ComponentMobility.MOVABLE)
    bounce.light_component.set_editor_property('cast_shadows',False)
    bounce.light_component.set_editor_property('intensity',2.)
    for actor in actors.get_all_level_actors():
        if actor.get_actor_label()=='Preview_Key':
            actor.set_actor_rotation(unreal.Rotator(pitch=-38,yaw=65,roll=0),False)
        elif actor.get_actor_label()=='Preview_Fill':
            actor.set_actor_rotation(unreal.Rotator(pitch=-30,yaw=155,roll=0),False)
            actor.light_component.set_editor_property('intensity',2.)
        elif isinstance(actor,unreal.ChunkedDestructibleBabylonTower):
            actor.set_folder_path('Babylon Tower')
            actor.rebuild_tower()
    location=unreal.Vector(4200,-9300,2800)
    unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(
        location,unreal.MathLibrary.find_look_at_rotation(location,unreal.Vector(0,0,1850)))
    assert unreal.EditorLoadingAndSavingUtils.save_map(world,path)

# Quiet sand stage and a self-contained sky keep the asset readable even when
# the project's atmospheric rendering is disabled. These belong to the preview only.
stage=load(folder+'/M_BabylonPreviewSand')
if not stage:
    stage=assets.create_asset('M_BabylonPreviewSand',folder,unreal.Material,unreal.MaterialFactoryNew())
    color=edit.create_material_expression(stage,unreal.MaterialExpressionConstant3Vector)
    color.set_editor_property('constant',unreal.LinearColor(.30,.25,.18,1))
    rough=edit.create_material_expression(stage,unreal.MaterialExpressionConstant)
    rough.set_editor_property('r',.95)
    edit.connect_material_property(color,'',unreal.MaterialProperty.MP_BASE_COLOR)
    edit.connect_material_property(rough,'',unreal.MaterialProperty.MP_ROUGHNESS)
    edit.recompile_material(stage)
    assert unreal.EditorAssetLibrary.save_loaded_asset(stage,False)
sky_material=load(folder+'/M_BabylonPreviewSky')
if not sky_material:
    sky_material=assets.create_asset('M_BabylonPreviewSky',folder,unreal.Material,unreal.MaterialFactoryNew())
    sky_material.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
    sky_material.set_editor_property('two_sided',True)
    sky_material.set_editor_property('is_sky',True)
    position=edit.create_material_expression(sky_material,unreal.MaterialExpressionWorldPosition)
    shader=edit.create_material_expression(sky_material,unreal.MaterialExpressionCustom)
    pin=unreal.CustomInput();pin.set_editor_property('input_name','Position')
    shader.set_editor_property('inputs',[pin])
    shader.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    shader.set_editor_property('code','return lerp(float3(.38,.51,.60),float3(.055,.23,.46),saturate(normalize(Position).z*2.5));')
    edit.connect_material_expressions(position,'',shader,'Position')
    edit.connect_material_property(shader,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    edit.recompile_material(sky_material)
    assert unreal.EditorAssetLibrary.save_loaded_asset(sky_material,False)
for actor in actors.get_all_level_actors():
    if actor.get_actor_label()=='Preview_Ground':
        actor.static_mesh_component.set_material(0,stage)
        actor.set_actor_scale3d(unreal.Vector(10000,10000,.5))
dome=next((a for a in actors.get_all_level_actors() if a.get_actor_label()=='Preview_SkyDome'),None)
if not dome:
    dome=actors.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector())
    dome.set_actor_label('Preview_SkyDome')
    dome.set_folder_path('Preview')
    dome.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Sphere'))
    dome.static_mesh_component.set_material(0,sky_material)
    dome.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    dome.static_mesh_component.set_editor_property('cast_shadow',False)
    dome.set_actor_scale3d(unreal.Vector(5000,5000,5000))
assert unreal.EditorLoadingAndSavingUtils.save_map(world,path)
unreal.log('BABYLON_TOWER_SETUP_OK '+bp.get_path_name())
