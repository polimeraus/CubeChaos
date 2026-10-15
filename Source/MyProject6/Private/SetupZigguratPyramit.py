"""Independent reference-matched actor, palette and preview; leaves the earlier ziggurat intact."""
import json
from pathlib import Path
import unreal

folder='/Game/Structures/Destructible/ZigguratPyramit'
assets=unreal.AssetToolsHelpers.get_asset_tools()
edit=unreal.MaterialEditingLibrary
out=Path(unreal.Paths.project_saved_dir())/'ZigguratPyramit'
out.mkdir(parents=True,exist_ok=True)
def load(path):
    return unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None

master=load(folder+'/Materials/M_PyramitSandstone')
refresh='-RefreshPyramitPresentation' in unreal.SystemLibrary.get_command_line()
if not master:
    master=assets.create_asset('M_PyramitSandstone',folder+'/Materials',unreal.Material,unreal.MaterialFactoryNew())
    tint=edit.create_material_expression(master,unreal.MaterialExpressionVectorParameter,-650,0)
    tint.set_editor_property('parameter_name','SandstoneColor')
    tint.set_editor_property('default_value',unreal.LinearColor(.64,.4,.2,1))
    rough=edit.create_material_expression(master,unreal.MaterialExpressionScalarParameter,-200,350)
    rough.set_editor_property('parameter_name','Roughness')
    rough.set_editor_property('default_value',.92)
    position=edit.create_material_expression(master,unreal.MaterialExpressionWorldPosition,-850,150)
    noise=edit.create_material_expression(master,unreal.MaterialExpressionNoise,-650,150)
    for k,v in {'scale':.12,'levels':3,'quality':1,'output_min':.91,'output_max':1.}.items():noise.set_editor_property(k,v)
    multiply=edit.create_material_expression(master,unreal.MaterialExpressionMultiply,-200,0)
    edit.connect_material_expressions(position,'',noise,'Position')
    edit.connect_material_expressions(tint,'',multiply,'A')
    edit.connect_material_expressions(noise,'',multiply,'B')
    edit.connect_material_property(multiply,'',unreal.MaterialProperty.MP_BASE_COLOR)
    edit.connect_material_property(rough,'',unreal.MaterialProperty.MP_ROUGHNESS)
    edit.layout_material_expressions(master)
    assert not edit.recompile_material(master)
    assert unreal.EditorAssetLibrary.save_loaded_asset(master,False)
elif refresh:
    for expression in edit.get_material_expressions(master):
        if isinstance(expression,unreal.MaterialExpressionNoise):
            for key,value in {'scale':.12,'output_min':.91,'output_max':1.}.items():
                expression.set_editor_property(key,value)
    assert not edit.recompile_material(master)
    assert unreal.EditorAssetLibrary.save_loaded_asset(master,False)

palette={}
for name,color in {'Stone':(.64,.40,.20),'Cornice':(.66,.42,.21),'Terrace':(.70,.47,.25),
                   'Lintel':(.62,.38,.19),'Foundation':(.60,.37,.18),'Crown':(.67,.42,.22),
                   'Stage':(.12,.115,.10)}.items():
    material=load(folder+'/Materials/MI_Pyramit'+name)
    if not material:
        material=assets.create_asset('MI_Pyramit'+name,folder+'/Materials',unreal.MaterialInstanceConstant,unreal.MaterialInstanceConstantFactoryNew())
        edit.set_material_instance_parent(material,master)
        edit.set_material_instance_vector_parameter_value(material,'SandstoneColor',unreal.LinearColor(*color,1))
        assert unreal.EditorAssetLibrary.save_loaded_asset(material,False)
    if name!='Stage':palette[name.lower()+'_material']=material

bp=load(folder+'/BP_ChunkedDestructibleZigguratPyramit')
if not bp:
    factory=unreal.BlueprintFactory()
    factory.set_editor_property('parent_class',unreal.ChunkedDestructibleZigguratPyramit)
    bp=assets.create_asset('BP_ChunkedDestructibleZigguratPyramit',folder,unreal.Blueprint,factory)
    defaults=unreal.get_default_object(bp.generated_class())
    for key,value in palette.items():defaults.set_editor_property(key,value)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    assert unreal.EditorAssetLibrary.save_loaded_asset(bp,False)

path=folder+'/Maps/Lvl_ZigguratPyramitPreview'
if not unreal.EditorAssetLibrary.does_asset_exist(path):
    world=unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    temple=actors.spawn_actor_from_class(bp.generated_class(),unreal.Vector())
    temple.set_actor_label('ChunkedDestructibleZigguratPyramit')
    temple.set_folder_path('Ziggurat Pyramit')
    assert temple.generated_piece_count>100 and not temple.generation_warning
    for label,rotation,intensity in [('Key',unreal.Rotator(pitch=-38,yaw=65,roll=0),5.),
                                      ('Fill',unreal.Rotator(pitch=-15,yaw=-35,roll=0),1.6)]:
        light=actors.spawn_actor_from_class(unreal.DirectionalLight,unreal.Vector(0,0,3000),rotation)
        light.set_actor_label('Preview_'+label)
        light.set_folder_path('Preview')
        light.light_component.set_editor_property('intensity',intensity)
        light.light_component.set_editor_property('mobility',unreal.ComponentMobility.MOVABLE)
        if label=='Fill':light.light_component.set_editor_property('cast_shadows',False)
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
    ground.static_mesh_component.set_material(0,load(folder+'/Materials/MI_PyramitStage'))
    ground.set_actor_scale3d(unreal.Vector(70,70,.5))
    start=actors.spawn_actor_from_class(unreal.PlayerStart,unreal.Vector(0,-2000,125),unreal.Rotator(pitch=0,yaw=90,roll=0))
    start.set_folder_path('Preview')
    location=unreal.Vector(-3200,-5600,2550)
    unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(
        location,unreal.MathLibrary.find_look_at_rotation(location,unreal.Vector(0,0,1000)))
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
            actor.set_actor_rotation(unreal.Rotator(pitch=-15,yaw=-35,roll=0),False)
    location=unreal.Vector(-3200,-5600,2550)
    unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(
        location,unreal.MathLibrary.find_look_at_rotation(location,unreal.Vector(0,0,1000)))
    assert unreal.EditorLoadingAndSavingUtils.save_map(world,path)
unreal.log('ZIGGURAT_PYRAMIT_SETUP_OK '+bp.get_path_name())
