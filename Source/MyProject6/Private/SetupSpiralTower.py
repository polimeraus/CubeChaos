"""Create the reusable hollow spiral tower Blueprint, masonry materials and a playable preview map."""
import json
from pathlib import Path
import unreal

folder='/Game/Structures/Destructible/SpiralTower'
out=Path(unreal.Paths.project_saved_dir())/'SpiralTower'
out.mkdir(parents=True,exist_ok=True)
assets=unreal.AssetToolsHelpers.get_asset_tools()
edit=unreal.MaterialEditingLibrary


def load(path):
    return unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None


master=load(folder+'/Materials/M_SpiralTowerMasonry')
if not master:
    master=assets.create_asset('M_SpiralTowerMasonry',folder+'/Materials',unreal.Material,unreal.MaterialFactoryNew())
    position=edit.create_material_expression(master,unreal.MaterialExpressionWorldPosition)
    normal=edit.create_material_expression(master,unreal.MaterialExpressionVertexNormalWS)
    tint=edit.create_material_expression(master,unreal.MaterialExpressionVectorParameter)
    tint.set_editor_property('parameter_name','StoneColor')
    tint.set_editor_property('default_value',unreal.LinearColor(.43,.40,.34,1))
    joints=edit.create_material_expression(master,unreal.MaterialExpressionScalarParameter)
    joints.set_editor_property('parameter_name','JointStrength');joints.set_editor_property('default_value',.22)
    custom=edit.create_material_expression(master,unreal.MaterialExpressionCustom)
    pins=[]
    for name in ('Position','Normal','Tint','Joints'):
        pin=unreal.CustomInput();pin.set_editor_property('input_name',name);pins.append(pin)
    custom.set_editor_property('inputs',pins)
    custom.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    custom.set_editor_property('code','''
float row=floor(Position.z/32.0);
float3 n=abs(normalize(Normal));
float horizontal=(n.x>n.y?Position.y:Position.x)+fmod(row,2.0)*49.0;
float2 brick=float2(horizontal/98.0,Position.z/32.0);
float2 edge=min(frac(brick),1.0-frac(brick))*float2(98.0,32.0);
float joint=1.0-smoothstep(0.4,1.15,min(edge.x,edge.y));
joint*=1.0-smoothstep(0.65,0.95,n.z);
float grain=frac(sin(dot(floor(Position*.8),float3(12.9898,78.233,37.719)))*43758.5453);
float variation=frac(sin(dot(floor(brick),float2(27.71,91.37)))*13758.5453);
return Tint*(.94+.045*grain+.04*variation)*(1.0-joint*Joints);
''')
    for node,pin,name in [(position,'','Position'),(normal,'','Normal'),(tint,'RGB','Tint'),(joints,'','Joints')]:
        assert edit.connect_material_expressions(node,pin,custom,name)
    rough=edit.create_material_expression(master,unreal.MaterialExpressionConstant);rough.set_editor_property('r',.94)
    assert edit.connect_material_property(custom,'',unreal.MaterialProperty.MP_BASE_COLOR)
    assert edit.connect_material_property(rough,'',unreal.MaterialProperty.MP_ROUGHNESS)
    edit.layout_material_expressions(master);edit.recompile_material(master)
    assert unreal.EditorAssetLibrary.save_loaded_asset(master,False)

palette={}
for name,color,joint in [('Stone',(.43,.40,.34),.22),('Stairs',(.66,.62,.53),.12),('Stage',(.35,.32,.26),0)]:
    material=load(folder+'/Materials/MI_SpiralTower'+name)
    if not material:
        material=assets.create_asset('MI_SpiralTower'+name,folder+'/Materials',unreal.MaterialInstanceConstant,unreal.MaterialInstanceConstantFactoryNew())
        edit.set_material_instance_parent(material,master)
        edit.set_material_instance_vector_parameter_value(material,'StoneColor',unreal.LinearColor(*color,1))
        edit.set_material_instance_scalar_parameter_value(material,'JointStrength',joint)
        assert unreal.EditorAssetLibrary.save_loaded_asset(material,False)
    palette[name]=material

bp=load(folder+'/BP_ChunkedDestructibleSpiralTower')
if not bp:
    factory=unreal.BlueprintFactory();factory.set_editor_property('parent_class',unreal.ChunkedDestructibleSpiralTower)
    bp=assets.create_asset('BP_ChunkedDestructibleSpiralTower',folder,unreal.Blueprint,factory)
    defaults=unreal.get_default_object(bp.generated_class())
    defaults.set_editor_property('stone_material',palette['Stone'])
    defaults.set_editor_property('stair_material',palette['Stairs'])
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    assert unreal.EditorAssetLibrary.save_loaded_asset(bp,False)

map_path=folder+'/Maps/Lvl_SpiralTowerPreview'
if not unreal.EditorAssetLibrary.does_asset_exist(map_path):
    world=unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    tower=actors.spawn_actor_from_class(bp.generated_class(),unreal.Vector())
    tower.set_actor_label('ChunkedDestructibleSpiralTower');tower.set_folder_path('Spiral Tower')
    assert not tower.generation_warning and tower.generated_piece_count>1000
    for label,rotation,intensity in [('Key',unreal.Rotator(pitch=-42,yaw=155,roll=0),5.),
                                      ('Fill',unreal.Rotator(pitch=-25,yaw=57,roll=0),1.3)]:
        light=actors.spawn_actor_from_class(unreal.DirectionalLight,unreal.Vector(0,0,5000),rotation)
        light.set_actor_label('Preview_'+label);light.set_folder_path('Preview')
        light.light_component.set_editor_property('mobility',unreal.ComponentMobility.MOVABLE)
        light.light_component.set_editor_property('intensity',intensity)
        light.light_component.set_editor_property('light_source_angle',2.0)
        if label=='Fill':light.light_component.set_editor_property('cast_shadows',False)
    sky=actors.spawn_actor_from_class(unreal.SkyLight,unreal.Vector(0,0,4000));sky.set_folder_path('Preview')
    component=sky.get_component_by_class(unreal.SkyLightComponent)
    component.set_editor_property('mobility',unreal.ComponentMobility.MOVABLE)
    component.set_editor_property('source_type',unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
    component.set_editor_property('cubemap',unreal.load_asset('/Engine/MapTemplates/Sky/DaylightAmbientCubemap'))
    component.set_editor_property('intensity',.7);component.set_editor_property('cast_shadows',False)
    ground=actors.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(0,0,-25))
    ground.set_actor_label('Preview_Ground');ground.set_folder_path('Preview')
    ground.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
    ground.static_mesh_component.set_material(0,palette['Stage']);ground.set_actor_scale3d(unreal.Vector(10000,10000,.5))
    backdrop=assets.create_asset('M_SpiralTowerPreviewSky',folder+'/Materials',unreal.Material,unreal.MaterialFactoryNew())
    backdrop.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
    backdrop.set_editor_property('two_sided',True);backdrop.set_editor_property('is_sky',True)
    color=edit.create_material_expression(backdrop,unreal.MaterialExpressionConstant3Vector)
    color.set_editor_property('constant',unreal.LinearColor(.37,.35,.30,1))
    edit.connect_material_property(color,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    edit.recompile_material(backdrop);assert unreal.EditorAssetLibrary.save_loaded_asset(backdrop,False)
    dome=actors.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector())
    dome.set_actor_label('Preview_Sky');dome.set_folder_path('Preview')
    dome.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Sphere'))
    dome.static_mesh_component.set_material(0,backdrop)
    dome.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    dome.static_mesh_component.set_editor_property('cast_shadow',False);dome.set_actor_scale3d(unreal.Vector(5000,5000,5000))
    post=actors.spawn_actor_from_class(unreal.PostProcessVolume,unreal.Vector())
    post.set_actor_label('Preview_Exposure');post.set_folder_path('Preview');post.set_editor_property('unbound',True)
    settings=post.get_editor_property('settings')
    for key,value in {'override_auto_exposure_method':True,'auto_exposure_method':unreal.AutoExposureMethod.AEM_MANUAL,
        'override_auto_exposure_apply_physical_camera_exposure':True,'auto_exposure_apply_physical_camera_exposure':False,
        'override_auto_exposure_bias':True,'auto_exposure_bias':0.,'override_vignette_intensity':True,'vignette_intensity':0.,
        'ambient_cubemap':unreal.load_asset('/Engine/MapTemplates/Sky/DaylightAmbientCubemap'),
        'override_ambient_cubemap_intensity':True,'ambient_cubemap_intensity':.45,
        'override_motion_blur_amount':True,'motion_blur_amount':0.}.items():settings.set_editor_property(key,value)
    post.set_editor_property('settings',settings)
    start=actors.spawn_actor_from_class(unreal.PlayerStart,unreal.Vector(-650,-2450,110),unreal.Rotator(pitch=12,yaw=75,roll=0))
    start.set_folder_path('Preview')
    camera=unreal.Vector(-4800,-7400,5200)
    unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(
        camera,unreal.MathLibrary.find_look_at_rotation(camera,unreal.Vector(0,0,1750)))
    actors.set_selected_level_actors([tower])
    assert unreal.EditorLoadingAndSavingUtils.save_map(world,map_path)
    report=dict(blueprint=bp.get_path_name(),map=map_path,pieces=tower.generated_piece_count,
                steps=tower.generated_step_count,height_cm=tower.generated_height,shaft_cm=tower.clear_shaft_diameter)
    (out/'setup.json').write_text(json.dumps(report,indent=2))
unreal.log('SPIRAL_TOWER_SETUP_OK '+bp.get_path_name())
