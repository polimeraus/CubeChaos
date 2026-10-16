"""Persist the railgun's blue flowing coil/display material and updated display geometry."""
from pathlib import Path
from datetime import datetime
import shutil,unreal
root=Path(unreal.Paths.project_dir()).resolve()
base='/Game/Weapons/Railgun'
assets=unreal.AssetToolsHelpers.get_asset_tools()
edit=unreal.MaterialEditingLibrary

def build_material():
    path=base+'/Materials/M_RailEnergyFlow'
    m=unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else assets.create_asset('M_RailEnergyFlow',base+'/Materials',unreal.Material,unreal.MaterialFactoryNew())
    m.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
    expressions=list(edit.get_material_expressions(m))
    def node(cls,name=None):
        for n in expressions:
            if isinstance(n,cls) and (name is None or str(n.get_editor_property('parameter_name'))==name):return n
        n=edit.create_material_expression(m,cls);expressions.append(n);return n
    shader=node(unreal.MaterialExpressionCustom)
    shader.set_editor_property('code',Path(__file__).with_name('RailgunEnergy.ush').read_text())
    shader.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    uv=node(unreal.MaterialExpressionTextureCoordinate)
    clock=node(unreal.MaterialExpressionTime);clock.set_editor_property('ignore_pause',False)
    color=node(unreal.MaterialExpressionVectorParameter,'Tint')
    color.set_editor_property('parameter_name','Tint');color.set_editor_property('default_value',unreal.LinearColor(.008,.22,1,1))
    pins={'UV':(uv,''),'Time':(clock,''),'Tint':(color,'RGB')}
    for key,value in [('Emission',4.8),('FlowSpeed',.48)]:
        p=node(unreal.MaterialExpressionScalarParameter,key)
        p.set_editor_property('parameter_name',key);p.set_editor_property('default_value',value)
        pins[key]=(p,'')
    inputs=[]
    for key in pins:
        p=unreal.CustomInput();p.set_editor_property('input_name',key);inputs.append(p)
    shader.set_editor_property('inputs',inputs)
    for key,(n,output) in pins.items():assert edit.connect_material_expressions(n,output,shader,key)
    assert edit.connect_material_property(shader,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    edit.layout_material_expressions(m);edit.recompile_material(m)
    assert unreal.EditorAssetLibrary.save_loaded_asset(m,False)
    mi=unreal.load_asset(base+'/Materials/MI_RailEnergy');assert mi
    edit.set_material_instance_parent(mi,m)
    edit.set_material_instance_vector_parameter_value(mi,'Tint',unreal.LinearColor(.008,.22,1,1))
    edit.set_material_instance_scalar_parameter_value(mi,'Emission',4.8)
    edit.set_material_instance_scalar_parameter_value(mi,'FlowSpeed',.48)
    assert unreal.EditorAssetLibrary.save_loaded_asset(mi,False)
    return m

def import_mesh():
    task=unreal.AssetImportTask()
    for key,value in {'filename':str(root/'ContentSource/Railgun/SM_QuakeRailgun.fbx'),'destination_path':base+'/Meshes',
                      'destination_name':'SM_QuakeRailgun','automated':True,'replace_existing':True,'save':True}.items():task.set_editor_property(key,value)
    options=unreal.FbxImportUI()
    for key,value in {'import_mesh':True,'import_as_skeletal':False,'import_materials':False,'import_textures':False,
                      'mesh_type_to_import':unreal.FBXImportType.FBXIT_STATIC_MESH,'automated_import_should_detect_type':False}.items():options.set_editor_property(key,value)
    data=options.get_editor_property('static_mesh_import_data')
    for key,value in {'combine_meshes':True,'auto_generate_collision':False,'convert_scene':True,'convert_scene_unit':True,
                      'normal_import_method':unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS}.items():data.set_editor_property(key,value)
    task.set_editor_property('options',options);assets.import_asset_tasks([task])
    mesh=unreal.load_asset(base+'/Meshes/SM_QuakeRailgun');assert mesh
    slots=mesh.get_editor_property('static_materials')
    for i,slot in enumerate(slots):
        mat=unreal.load_asset(base+'/Materials/MI_'+str(slot.material_slot_name));assert mat
        slot.set_editor_property('material_interface',mat);slots[i]=slot
    mesh.set_editor_property('static_materials',slots)
    assert unreal.EditorAssetLibrary.save_loaded_asset(mesh,False)

if __name__=='__main__':
    backup=root/'Saved/RailTowerUpgrade/Backups'/datetime.now().strftime('%Y%m%d_%H%M%S')
    for relative in ['Meshes/SM_QuakeRailgun.uasset','Materials/MI_RailEnergy.uasset']:
        source=root/'Content/Weapons/Railgun'/relative
        dest=backup/relative;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,dest)
    build_material();import_mesh()
    unreal.log('RAIL_ENERGY_SAVED')
