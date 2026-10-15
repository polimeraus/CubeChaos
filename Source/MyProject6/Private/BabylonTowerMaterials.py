"""Babylon-only limestone surface; the shared legacy sandstone remains available to other structures."""
import unreal

def create_limestone_material(folder, refresh=False):
    path=folder+'/Materials/M_BabylonLimestone'
    edit=unreal.MaterialEditingLibrary
    noise_source='''struct LimestoneNoise {
float Hash(float3 p) { p=frac(p*.1031); p+=dot(p,p.yzx+33.33); return frac((p.x+p.y)*p.z); }
float Value(float3 p) {
 float3 i=floor(p),f=frac(p); f=f*f*(3-2*f);
 return lerp(lerp(lerp(Hash(i),Hash(i+float3(1,0,0)),f.x),lerp(Hash(i+float3(0,1,0)),Hash(i+float3(1,1,0)),f.x),f.y),
 lerp(lerp(Hash(i+float3(0,0,1)),Hash(i+float3(1,0,1)),f.x),lerp(Hash(i+float3(0,1,1)),Hash(i+float3(1,1,1)),f.x),f.y),f.z);
}
}; LimestoneNoise noise;
'''

    color_code=noise_source+"return Tint*(.89+.08*noise.Value(Position*.035)+.03*noise.Value(Position*.12));"
    bump_code=noise_source+"float3 p=Position*.08; float v=noise.Value(p); float3 g=float3(noise.Value(p+float3(.2,0,0))-v,noise.Value(p+float3(0,.2,0))-v,noise.Value(p+float3(0,0,.2))-v); float3 n=normalize(Normal); return normalize(n+.65*(g-n*dot(n,g)));"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        material=unreal.load_asset(path)
        if refresh:
            edit.get_material_property_input_node(material,unreal.MaterialProperty.MP_BASE_COLOR).set_editor_property('code',color_code)
            edit.get_material_property_input_node(material,unreal.MaterialProperty.MP_NORMAL).set_editor_property('code',bump_code)
            edit.recompile_material(material)
            assert unreal.EditorAssetLibrary.save_loaded_asset(material,False)
        return material
    material=unreal.AssetToolsHelpers.get_asset_tools().create_asset('M_BabylonLimestone',folder+'/Materials',unreal.Material,unreal.MaterialFactoryNew())
    material.set_editor_property('tangent_space_normal',False)
    tint=edit.create_material_expression(material,unreal.MaterialExpressionVectorParameter,-700,0)
    tint.set_editor_property('parameter_name','SandstoneColor')
    tint.set_editor_property('default_value',unreal.LinearColor(.73,.70,.63,1))
    position=edit.create_material_expression(material,unreal.MaterialExpressionWorldPosition,-700,180)
    normal=edit.create_material_expression(material,unreal.MaterialExpressionVertexNormalWS,-700,320)
    rough=edit.create_material_expression(material,unreal.MaterialExpressionScalarParameter,-250,500)
    rough.set_editor_property('parameter_name','Roughness');rough.set_editor_property('default_value',.96)
    color=edit.create_material_expression(material,unreal.MaterialExpressionCustom,-250,0)
    inputs=[]
    for name in ('Position','Tint'):
        pin=unreal.CustomInput();pin.set_editor_property('input_name',name);inputs.append(pin)
    color.set_editor_property('inputs',inputs);color.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    color.set_editor_property('code',color_code)
    bump=edit.create_material_expression(material,unreal.MaterialExpressionCustom,-250,260)
    inputs=[]
    for name in ('Position','Normal'):
        pin=unreal.CustomInput();pin.set_editor_property('input_name',name);inputs.append(pin)
    bump.set_editor_property('inputs',inputs);bump.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    bump.set_editor_property('code',bump_code)
    edit.connect_material_expressions(position,'',color,'Position');edit.connect_material_expressions(tint,'RGB',color,'Tint')
    edit.connect_material_expressions(position,'',bump,'Position');edit.connect_material_expressions(normal,'',bump,'Normal')
    edit.connect_material_property(color,'',unreal.MaterialProperty.MP_BASE_COLOR)
    edit.connect_material_property(bump,'',unreal.MaterialProperty.MP_NORMAL)
    edit.connect_material_property(rough,'',unreal.MaterialProperty.MP_ROUGHNESS)
    edit.layout_material_expressions(material);edit.recompile_material(material)
    assert unreal.EditorAssetLibrary.save_loaded_asset(material,False)
    return material
