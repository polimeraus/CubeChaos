"""Import the original fusion cannon and audio, save materials and corrected hand pose."""
from pathlib import Path
import unreal, json, runpy
root=Path(unreal.Paths.project_dir()).resolve()
base='/Game/Weapons/BFG/Fusion'
out=root/'Saved/BFGRework';out.mkdir(parents=True,exist_ok=True)
assets=unreal.AssetToolsHelpers.get_asset_tools();edit=unreal.MaterialEditingLibrary
surface=unreal.load_asset('/Game/Weapons/Railgun/Materials/M_RailMetal');assert surface
palette={'FusionSteel':((.15,.20,.21),.78,.32,0),'FusionBronze':((.40,.235,.085),.72,.36,0),
         'FusionDark':((.018,.025,.03),.6,.42,0),'FusionEdge':((.42,.48,.48),.83,.24,0),
         'FusionEnergy':((.025,.85,.58),.22,.24,3.8),'FusionRubber':((.012,.016,.02),.05,.72,0),
         'FusionWarning':((.9,.44,.06),.4,.4,0)}
materials={}
for name,(color,metal,rough,emission) in palette.items():
    path=base+'/Materials/MI_'+name
    mi=unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else assets.create_asset('MI_'+name,base+'/Materials',unreal.MaterialInstanceConstant,unreal.MaterialInstanceConstantFactoryNew())
    edit.set_material_instance_parent(mi,surface)
    edit.set_material_instance_vector_parameter_value(mi,'Tint',unreal.LinearColor(*color,1))
    for key,val in [('Metallic',metal),('Roughness',rough),('Emission',emission)]:edit.set_material_instance_scalar_parameter_value(mi,key,val)
    assert unreal.EditorAssetLibrary.save_loaded_asset(mi,False)
    materials[name]=mi
task=unreal.AssetImportTask()
for key,val in {'filename':str(root/'ContentSource/BFGFusion/SM_BFGFusion.fbx'),'destination_path':base+'/Meshes',
                'destination_name':'SM_BFGFusion','automated':True,'replace_existing':True,'save':True}.items():task.set_editor_property(key,val)
options=unreal.FbxImportUI()
for key,val in {'import_mesh':True,'import_as_skeletal':False,'import_materials':False,'import_textures':False,
                'mesh_type_to_import':unreal.FBXImportType.FBXIT_STATIC_MESH,'automated_import_should_detect_type':False}.items():options.set_editor_property(key,val)
data=options.get_editor_property('static_mesh_import_data')
for key,val in {'combine_meshes':True,'auto_generate_collision':False,'convert_scene':True,'convert_scene_unit':True,
                'normal_import_method':unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS}.items():data.set_editor_property(key,val)
task.set_editor_property('options',options);assets.import_asset_tasks([task])
mesh=unreal.load_asset(base+'/Meshes/SM_BFGFusion');assert mesh
slots=mesh.get_editor_property('static_materials')
for i,slot in enumerate(slots):
    name=str(slot.get_editor_property('material_slot_name'));assert name in materials,name
    slot.set_editor_property('material_interface',materials[name]);slots[i]=slot
mesh.set_editor_property('static_materials',slots)
assert unreal.EditorAssetLibrary.save_loaded_asset(mesh,False)
for name in ['S_BFGFusionFire','S_BFGFusionImpact']:
    task=unreal.AssetImportTask()
    for key,val in {'filename':str(root/'ContentSource/BFGFusion'/(name+'.wav')),'destination_path':base+'/Audio',
                    'destination_name':name,'automated':True,'replace_existing':True,'save':True}.items():task.set_editor_property(key,val)
    assets.import_asset_tasks([task]);sound=unreal.load_asset(base+'/Audio/'+name);assert sound
    sound.set_editor_property('volume',1.)
    assert unreal.EditorAssetLibrary.save_loaded_asset(sound,False)
runpy.run_path(str(Path(__file__).with_name('SetupBFGFusionGrip.py')))
plasma=unreal.load_asset('/Game/Weapons/BFG/Materials/M_BFGPlasma')
shader=next(n for n in edit.get_material_expressions(plasma) if isinstance(n,unreal.MaterialExpressionCustom))
shader.set_editor_property('code',Path(__file__).with_name('BFGPlasma.ush').read_text())
edit.recompile_material(plasma);assert unreal.EditorAssetLibrary.save_loaded_asset(plasma,False)
(out/'Assets.json').write_text(json.dumps({'model':mesh.get_path_name(),'materials':[m.get_path_name() for m in materials.values()],
    'bounds':str(mesh.get_bounds()),'audio':str(root/'ContentSource/BFGFusion'),'grip':base+'/Animations/A_BFGFusionHold'},indent=2))
unreal.log('BFG_FUSION_ASSETS_SAVED')
