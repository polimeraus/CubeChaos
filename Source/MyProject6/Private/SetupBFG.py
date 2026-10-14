"""Save reconstructed BFG art/audio and assign one existing level pickup to BFG."""
from pathlib import Path
import json, math, random, struct, wave
import unreal

root=Path(unreal.Paths.project_dir())
out=root/'Saved/BFG';out.mkdir(parents=True,exist_ok=True)
base='/Game/Weapons/Railgun'  # Keep the weapons' assets together.
assets=unreal.AssetToolsHelpers.get_asset_tools()
edit=unreal.MaterialEditingLibrary
def load_or_create(name,cls,factory,folder=base):
    path=folder+'/'+name
    return unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else assets.create_asset(name,folder,cls,factory)

plasma=load_or_create('M_BFGPlasma',unreal.Material,unreal.MaterialFactoryNew())
plasma.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
plasma.set_editor_property('blend_mode',unreal.BlendMode.BLEND_ADDITIVE)
plasma.set_editor_property('two_sided',True)
nodes=list(edit.get_material_expressions(plasma))
def node(cls):
    for n in nodes:
        if isinstance(n,cls):return n
    n=edit.create_material_expression(plasma,cls);nodes.append(n);return n
shader=node(unreal.MaterialExpressionCustom)
shader.set_editor_property('code',Path(__file__).with_name('BFGPlasma.ush').read_text())
shader.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT3)
age=node(unreal.MaterialExpressionScalarParameter)
age.set_editor_property('parameter_name','Age');age.set_editor_property('default_value',0)
color=node(unreal.MaterialExpressionVertexColor)
sources={'UV':(node(unreal.MaterialExpressionTextureCoordinate),''),'VertexAlpha':(color,'A'),'VertexMode':(color,'R'),'Age':(age,'')}
pins=[]
for name in sources:
    p=unreal.CustomInput();p.set_editor_property('input_name',name);pins.append(p)
shader.set_editor_property('inputs',pins)
for name,(n,pin) in sources.items():assert edit.connect_material_expressions(n,pin,shader,name)
assert edit.connect_material_property(shader,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
opacity=node(unreal.MaterialExpressionConstant);opacity.set_editor_property('r',1)
edit.connect_material_property(opacity,'',unreal.MaterialProperty.MP_OPACITY)
edit.layout_material_expressions(plasma);edit.recompile_material(plasma)
assert unreal.EditorAssetLibrary.save_loaded_asset(plasma,False)

surface=unreal.load_asset(base+'/M_RailMetal');assert surface
palette={'BFGSteel':((.27,.30,.25),.8,.38,0),'BFGBronze':((.35,.16,.048),.7,.4,0),
         'BFGDark':((.022,.029,.024),.45,.5,0),'BFGEnergy':((.03,1,.12),.25,.28,2.4),
         'BFGRubber':((.012,.016,.013),.05,.68,0)}
materials={}
for name,(color,metal,rough,emission) in palette.items():
    mi=load_or_create('MI_'+name,unreal.MaterialInstanceConstant,unreal.MaterialInstanceConstantFactoryNew())
    edit.set_material_instance_parent(mi,surface)
    edit.set_material_instance_vector_parameter_value(mi,'Tint',unreal.LinearColor(*color,1))
    for key,val in [('Metallic',metal),('Roughness',rough),('Emission',emission)]:edit.set_material_instance_scalar_parameter_value(mi,key,val)
    assert unreal.EditorAssetLibrary.save_loaded_asset(mi,False)
    materials[name]=mi

task=unreal.AssetImportTask()
for key,val in {'filename':str(root/'ContentSource/Railgun/SM_BFG10K.fbx'),'destination_path':base,
                'destination_name':'SM_BFG10K','automated':True,'replace_existing':True,'save':True}.items():task.set_editor_property(key,val)
options=unreal.FbxImportUI()
for key,val in {'import_mesh':True,'import_as_skeletal':False,'import_materials':False,'import_textures':False,
                'mesh_type_to_import':unreal.FBXImportType.FBXIT_STATIC_MESH,'automated_import_should_detect_type':False}.items():options.set_editor_property(key,val)
data=options.get_editor_property('static_mesh_import_data')
for key,val in {'combine_meshes':True,'auto_generate_collision':False,'convert_scene':True,'convert_scene_unit':True,
                'normal_import_method':unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS}.items():data.set_editor_property(key,val)
task.set_editor_property('options',options);assets.import_asset_tasks([task])
mesh=unreal.load_asset(base+'/SM_BFG10K');assert mesh
slots=mesh.get_editor_property('static_materials')
for i,slot in enumerate(slots):
    name=str(slot.get_editor_property('material_slot_name'));assert name in materials,name
    slot.set_editor_property('material_interface',materials[name]);slots[i]=slot
mesh.set_editor_property('static_materials',slots)
assert unreal.EditorAssetLibrary.save_loaded_asset(mesh,False)

# Original synthesized plasma discharge and impact, saved with their editable sources.
rng=random.Random(103)
for name,duration,impact in [('S_BFGFire',.23,False),('S_BFGImpact',.48,True)]:
    path=root/'ContentSource/Railgun'/f'{name}.wav'
    samples=[];phase=0;low_noise=0
    for i in range(int(44100*duration)):
        t=i/44100
        freq=(70+200*math.exp(-t*12)) if impact else (160+1200*math.exp(-t*30))
        phase+=math.tau*freq/44100
        noise=rng.uniform(-1,1);low_noise=.86*low_noise+.14*noise
        envelope=min(1,t/.003)*math.exp(-t*(9 if impact else 20))
        sample=(.45*math.sin(phase)+.20*math.sin(phase*1.49)+(.9*low_noise if impact else .2*noise))*envelope
        samples.append(struct.pack('<h',int(max(-1,min(1,sample))*.8*32767)))
    with wave.open(str(path),'wb') as f:f.setnchannels(1);f.setsampwidth(2);f.setframerate(44100);f.writeframes(b''.join(samples))
    task=unreal.AssetImportTask()
    for key,val in {'filename':str(path),'destination_path':base,'destination_name':name,'automated':True,'replace_existing':True,'save':True}.items():task.set_editor_property(key,val)
    assets.import_asset_tasks([task]);assert unreal.load_asset(base+'/'+name)

factory=unreal.BlueprintFactory();factory.set_editor_property('parent_class',unreal.ParametricPickup)
bp=load_or_create('BP_BFGPickup',unreal.Blueprint,factory,'/Game/Gameplay/Pickups')
cdo=unreal.get_default_object(bp.generated_class())
settings={'weapon_reward':unreal.PickupWeapon.BFG,'icon':unreal.PickupMotif.RADIATION_BOMB,
          'icon_color':unreal.LinearColor(.03,1,.12,1),'body_color':unreal.LinearColor(.02,.055,.025,1)}
for key,val in settings.items():cdo.set_editor_property(key,val)
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
assert unreal.EditorAssetLibrary.save_loaded_asset(bp,False)

level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
pickups=[a for a in actors.get_all_level_actors() if isinstance(a,unreal.ParametricPickup)]
selected=next(a for a in pickups if a.get_actor_label()=='BP_ParametricPickup4')
transform=str(selected.get_actor_transform())
saved_transform=selected.get_actor_transform()
for key,val in settings.items():selected.set_editor_property(key,val)
selected.refresh_pickup()
assert selected.get_actor_transform()==saved_transform,'Pickup transform must be preserved'
assert level.save_current_level()
assert unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True,True)
report={'railgun_damage':unreal.get_default_object(unreal.RailgunWeapon).damage,
        'bfg_pickup':selected.get_actor_label(),'transform':transform,
        'pickups':[{'label':a.get_actor_label(),'reward':str(a.weapon_reward),'location':str(a.get_actor_location())} for a in pickups],
        'assets':base,'model':'reconstructed geometry','audio':'original synthesized effects'}
(out/'Setup.json').write_text(json.dumps(report,indent=2))
unreal.log('BFG_SETUP_OK '+selected.get_actor_label())
