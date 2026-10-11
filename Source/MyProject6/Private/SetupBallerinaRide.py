"""Import original Blender meshes, create materials/Blueprints and an isolated demo map.
Run with UnrealEditor-Cmd PROJECT -run=pythonscript -script=THIS_FILE -unattended -nullrhi.
No existing gameplay map is loaded or saved.
"""
from pathlib import Path
import json, unreal

ROOT=Path(unreal.Paths.project_dir())
BASE='/Game/Fairground/BallerinaRide'
OUT=ROOT/'Saved/BallerinaRide';OUT.mkdir(parents=True,exist_ok=True)
assets=unreal.AssetToolsHelpers.get_asset_tools()
edit=unreal.MaterialEditingLibrary

def material(name, pattern=False, bulb=False):
    path=BASE+'/Materials/'+name
    if unreal.EditorAssetLibrary.does_asset_exist(path):return unreal.load_asset(path)
    m=assets.create_asset(name,BASE+'/Materials',unreal.Material,unreal.MaterialFactoryNew())
    def node(cls):return edit.create_material_expression(m,cls)
    def scalar(name,value):
        n=node(unreal.MaterialExpressionScalarParameter);n.set_editor_property('parameter_name',name);n.set_editor_property('default_value',value);return n
    tint=node(unreal.MaterialExpressionVectorParameter);tint.set_editor_property('parameter_name','Tint');tint.set_editor_property('default_value',unreal.LinearColor(1,.76,.08,1))
    color=tint;pin='RGB'
    if pattern:
        uv=node(unreal.MaterialExpressionTextureCoordinate)
        strength=scalar('PatternStrength',1)
        shader=node(unreal.MaterialExpressionCustom)
        inputs=[]
        for n in ['UV','Tint','Strength']:
            inp=unreal.CustomInput();inp.set_editor_property('input_name',n);inputs.append(inp)
        shader.set_editor_property('inputs',inputs)
        shader.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT3)
        shader.set_editor_property('code','''
float2 q=float2(UV.x*3.0,UV.y*11.0);
q.x+=fmod(floor(q.y),2.0)*0.5;
float2 cell=floor(q);float2 p=frac(q)-0.5;
float d=length(p);float dotMask=1.0-smoothstep(0.19,0.23,d);
float a=atan2(p.y,p.x)+1.5707963;
float starRadius=lerp(0.105,0.255,0.5+0.5*cos(a*5.0));
float starMask=1.0-smoothstep(starRadius-0.025,starRadius+0.025,d);
float useStar=fmod(cell.x+cell.y,3.0)<0.5;
float mask=lerp(dotMask,starMask,useStar);
float3 motif=lerp(float3(0.018,0.14,0.58),float3(0.8,0.025,0.055),useStar);
float seam=smoothstep(0.015,0.027,UV.x)*smoothstep(0.015,0.027,1-UV.x);
float3 base=lerp(float3(0.88,0.53,0.065),Tint,seam);
return lerp(base,motif,mask*saturate(Strength));''')
        for n,source,p in [('UV',uv,''),('Tint',tint,'RGB'),('Strength',strength,'')]:
            assert edit.connect_material_expressions(source,p,shader,n)
        color=shader;pin=''
    assert edit.connect_material_property(color,pin,unreal.MaterialProperty.MP_BASE_COLOR)
    for name,value,prop in [('Metallic',.22,unreal.MaterialProperty.MP_METALLIC),('Roughness',.36,unreal.MaterialProperty.MP_ROUGHNESS)]:
        assert edit.connect_material_property(scalar(name,value),'',prop)
    if bulb:
        multiply=node(unreal.MaterialExpressionMultiply)
        edit.connect_material_expressions(tint,'RGB',multiply,'A')
        edit.connect_material_expressions(scalar('Emission',5),'',multiply,'B')
        edit.connect_material_property(multiply,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    edit.layout_material_expressions(m);edit.recompile_material(m)
    unreal.EditorAssetLibrary.save_loaded_asset(m,False)
    return m

surface=material('M_RideSurface')
pattern=material('M_RideSkirt',pattern=True)
bulb=material('M_RideBulb',bulb=True)
palette={
 'CrimsonVelvet':((.48,.012,.033),.12,.33),'Porcelain':((.86,.59,.37),.05,.36),
 'MidnightHair':((.016,.010,.022),.18,.26),'Brass':((.83,.46,.085),.72,.25),
 'Ivory':((.91,.88,.72),.08,.26),'Lashes':((.008,.005,.011),.1,.35),
 'JadeEyes':((.018,.21,.16),.25,.22),'Rouge':((.56,.017,.035),.12,.3),
 'SeatPaint':((.016,.32,.34),.45,.3),'SeatCushion':((.53,.19,.028),0,.6),
 'Chrome':((.55,.65,.71),.88,.22),'TreadPlate':((.09,.13,.16),.7,.5)}
materials={}
for name,(color,metal,rough) in palette.items():
    path=BASE+'/Materials/MI_'+name
    mi=unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else assets.create_asset('MI_'+name,BASE+'/Materials',unreal.MaterialInstanceConstant,unreal.MaterialInstanceConstantFactoryNew())
    edit.set_material_instance_parent(mi,surface)
    edit.set_material_instance_vector_parameter_value(mi,'Tint',unreal.LinearColor(*color,1))
    edit.set_material_instance_scalar_parameter_value(mi,'Metallic',metal)
    edit.set_material_instance_scalar_parameter_value(mi,'Roughness',rough)
    unreal.EditorAssetLibrary.save_loaded_asset(mi,False);materials[name]=mi

meshes={}
for name in ['SM_BallerinaFigure','SM_BallerinaSeat']:
    task=unreal.AssetImportTask()
    task.set_editor_property('filename',str(ROOT/'ContentSource/BallerinaRide'/(name+'.fbx')))
    task.set_editor_property('destination_path',BASE+'/Meshes')
    task.set_editor_property('destination_name',name)
    task.set_editor_property('automated',True);task.set_editor_property('replace_existing',True);task.set_editor_property('save',True)
    options=unreal.FbxImportUI()
    options.set_editor_property('import_mesh',True);options.set_editor_property('import_as_skeletal',False)
    options.set_editor_property('import_materials',False);options.set_editor_property('import_textures',False)
    options.set_editor_property('mesh_type_to_import',unreal.FBXImportType.FBXIT_STATIC_MESH)
    options.set_editor_property('automated_import_should_detect_type',False)
    data=options.get_editor_property('static_mesh_import_data')
    data.set_editor_property('combine_meshes',True);data.set_editor_property('auto_generate_collision',True)
    data.set_editor_property('normal_import_method',unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS)
    data.set_editor_property('convert_scene',True);data.set_editor_property('convert_scene_unit',True)
    task.set_editor_property('options',options)
    assets.import_asset_tasks([task])
    mesh=unreal.load_asset(BASE+'/Meshes/'+name);assert mesh
    slots=mesh.get_editor_property('static_materials')
    for index,slot in enumerate(slots):
        slotname=str(slot.get_editor_property('material_slot_name'))
        assert slotname in materials,(name,slotname)
        slot.set_editor_property('material_interface',materials[slotname])
        # Unreal struct array iteration yields values. Write each modified value back.
        slots[index]=slot
    mesh.set_editor_property('static_materials',slots)
    for index,slot in enumerate(slots):
        assert mesh.get_material(index)==materials[str(slot.get_editor_property('material_slot_name'))]
    assert unreal.EditorAssetLibrary.save_loaded_asset(mesh,False);meshes[name]=mesh

# Set native CDO asset references for this first import process as well as persisted Blueprints.
unreal.get_default_object(unreal.BallerinaRideFigure).mesh.set_static_mesh(meshes['SM_BallerinaFigure'])
unreal.get_default_object(unreal.BallerinaRideSeat).mesh.set_static_mesh(meshes['SM_BallerinaSeat'])

def blueprint(name,parent):
    path=BASE+'/Blueprints/'+name
    if unreal.EditorAssetLibrary.does_asset_exist(path):return unreal.load_asset(path)
    f=unreal.BlueprintFactory();f.set_editor_property('parent_class',parent)
    return assets.create_asset(name,BASE+'/Blueprints',unreal.Blueprint,f)

figure=blueprint('BP_BallerinaFigure',unreal.BallerinaRideFigure)
seat=blueprint('BP_BallerinaSeat',unreal.BallerinaRideSeat)
for bp,key in [(figure,'SM_BallerinaFigure'),(seat,'SM_BallerinaSeat')]:
    component=unreal.get_default_object(bp.generated_class()).mesh
    component.set_static_mesh(meshes[key])
    component.set_editor_property('override_materials',[])
    unreal.BlueprintEditorLibrary.compile_blueprint(bp);unreal.EditorAssetLibrary.save_loaded_asset(bp,False)
ride=blueprint('BP_ParametricBallerinaRide',unreal.ParametricBallerinaRide)
defaults=unreal.get_default_object(ride.generated_class())
defaults.set_editor_property('slice_materials',[pattern])
defaults.set_editor_property('bulb_material',bulb)
defaults.set_editor_property('figure_actor_class',figure.generated_class())
defaults.set_editor_property('seat_actor_class',seat.generated_class())
unreal.BlueprintEditorLibrary.compile_blueprint(ride);unreal.EditorAssetLibrary.save_loaded_asset(ride,False)

world=unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
world.get_world_settings().set_editor_property('default_game_mode',unreal.GameModeBase)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actor=actors.spawn_actor_from_class(ride.generated_class(),unreal.Vector())
actor.set_actor_label('Ballerina - Parametric Ride')
actor.rebuild_ride()
assert actor.get_generated_seat_count()==24
assert actor.get_generated_bulb_count()==528
stage=actors.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(0,0,-28))
stage.set_actor_label('Demo stage')
stage.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cylinder'))
stage.set_actor_scale3d(unreal.Vector(23,23,.5))
stage.static_mesh_component.set_material(0,materials['TreadPlate'])
for rotation,intensity in [(unreal.Rotator(-45,145,0),5),(unreal.Rotator(-25,-35,0),2.0),(unreal.Rotator(-60,45,0),1.0)]:
    light=actors.spawn_actor_from_class(unreal.DirectionalLight,unreal.Vector(0,0,1500),rotation)
    light.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    light.light_component.set_editor_property('cast_shadows',intensity==5)
    light.light_component.set_intensity(intensity)
sky=actors.spawn_actor_from_class(unreal.SkyLight,unreal.Vector(0,0,1000))
sky.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
sky.light_component.set_editor_property('source_type',unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
sky.light_component.set_cubemap(unreal.load_asset('/Engine/MapTemplates/Sky/DaylightAmbientCubemap'))
sky.light_component.set_intensity(4)
fill=actors.spawn_actor_from_class(unreal.PointLight,unreal.Vector(1100,-700,1150))
fill.set_actor_label('Demo soft fill')
fill.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
fill.light_component.set_editor_property('intensity_units',unreal.LightUnits.CANDELAS)
fill.light_component.set_editor_property('attenuation_radius',5000)
fill.light_component.set_editor_property('cast_shadows',False)
fill.light_component.set_intensity(1000)
camera=actors.spawn_actor_from_class(unreal.CameraActor,unreal.Vector(1780,-2050,1480))
camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(camera.get_actor_location(),unreal.Vector(0,0,310)),False)
camera.camera_component.set_editor_property('field_of_view',45)
actors.set_selected_level_actors([actor])
unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(camera.get_actor_location(),camera.get_actor_rotation())
assert unreal.EditorLoadingAndSavingUtils.save_map(world,BASE+'/Maps/L_BallerinaRide_Demo')
unreal.EditorAssetLibrary.save_directory(BASE,False,True)
report={'blueprint':BASE+'/Blueprints/BP_ParametricBallerinaRide','map':BASE+'/Maps/L_BallerinaRide_Demo','seats':actor.get_generated_seat_count(),'bulbs':actor.get_generated_bulb_count(),'figure_bounds':str(meshes['SM_BallerinaFigure'].get_bounds()),'seat_bounds':str(meshes['SM_BallerinaSeat'].get_bounds())}
(OUT/'Setup.json').write_text(json.dumps(report,indent=2))
unreal.log('BALLERINA_SETUP_OK '+json.dumps(report))
