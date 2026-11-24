"""Author the shared arcade housing finish alongside the original pickup icon material."""
from pathlib import Path
import runpy
import unreal as u

folder='/Game/Materials/Pickups'
lib=u.EditorAssetLibrary
edit=u.MaterialEditingLibrary
code="""
float role=floor(FinishUV.x+.5);
float3 pigment=BodyColor;
RoughOut=clamp(Roughness,.045,1.);
MetalOut=saturate(Metallic);
float fill=.24;
if (role>.5 && role<1.5) {
    pigment=TrimColor; RoughOut=.24; MetalOut=.72; fill=.29;
} else if (role>1.5 && role<2.5) {
    pigment=InlayColor; RoughOut=.31; MetalOut=.14; fill=.22;
} else if (role>2.5 && role<3.5) {
    pigment=BadgeColor*.77; RoughOut=.34; MetalOut=.23; fill=.17;
} else if (role>3.5) {
    pigment=lerp(InlayColor,IconColor,.56); RoughOut=.21; MetalOut=.08;
    float wave=.5+.5*cos(6.2831853*(Clock/max(PulsePeriod,.05)+PulsePhase));
    wave=wave*wave*(3.-2.*wave);
    float glow=lerp(MinimumGlow,MaximumGlow,PulseEnabled>.5 ? wave : 1.);
    fill=.62+.29*min(glow,8.);
}
GlowOut=pigment*fill;
return pigment;
"""


def build():
    path=folder+'/M_PickupEnamel'
    mat=u.load_asset(path) if lib.does_asset_exist(path) else u.AssetToolsHelpers.get_asset_tools().create_asset(
        'M_PickupEnamel',folder,u.Material,u.MaterialFactoryNew())
    mat.set_editor_property('shading_model',u.MaterialShadingModel.MSM_DEFAULT_LIT)
    old=list(edit.get_material_expressions(mat))
    def node(kind,label=None):
        for n in old:
            if isinstance(n,kind) and (not label or str(n.get_editor_property('parameter_name'))==label):
                return n
        n=edit.create_material_expression(mat,kind)
        if label: n.set_editor_property('parameter_name',label)
        return n
    uv=node(u.MaterialExpressionTextureCoordinate)
    uv.set_editor_property('coordinate_index',1)
    sources={'FinishUV':(uv,''),'Clock':(node(u.MaterialExpressionTime),'')}
    for key,rgb in [('BodyColor',(.035,.38,.34)),('IconColor',(.03,.75,1.)),
                    ('TrimColor',(.79,.48,.18)),('InlayColor',(.96,.81,.55)),('BadgeColor',(.020,.038,.046))]:
        n=node(u.MaterialExpressionVectorParameter,key)
        n.set_editor_property('default_value',u.LinearColor(*rgb,1))
        sources[key]=(n,'RGB')
    for key,value in [('Roughness',.32),('Metallic',.3),('PulseEnabled',1.),('PulsePeriod',2.),
                      ('PulsePhase',0.),('MinimumGlow',0.),('MaximumGlow',6.)]:
        n=node(u.MaterialExpressionScalarParameter,key)
        n.set_editor_property('default_value',value)
        sources[key]=(n,'')
    shader=node(u.MaterialExpressionCustom)
    shader.set_editor_property('description','Pickup enamel, plating, porcelain and lamps')
    shader.set_editor_property('code',code)
    types=u.CustomMaterialOutputType
    shader.set_editor_property('output_type',types.CMOT_FLOAT3)
    if [str(p.get_editor_property('input_name')) for p in shader.get_editor_property('inputs')]!=list(sources):
        pins=[]
        for key in sources:
            pin=u.CustomInput()
            pin.set_editor_property('input_name',key)
            pins.append(pin)
        shader.set_editor_property('inputs',pins)
    outputs={'GlowOut':types.CMOT_FLOAT3,'RoughOut':types.CMOT_FLOAT1,'MetalOut':types.CMOT_FLOAT1}
    if [str(p.get_editor_property('output_name')) for p in shader.get_editor_property('additional_outputs')]!=list(outputs):
        pins=[]
        for key,typ in outputs.items():
            pin=u.CustomOutput()
            pin.set_editor_property('output_name',key)
            pin.set_editor_property('output_type',typ)
            pins.append(pin)
        shader.set_editor_property('additional_outputs',pins)
    for key,(n,output) in sources.items():
        assert edit.connect_material_expressions(n,output,shader,key),key
    prop=u.MaterialProperty
    for output,target in [('',prop.MP_BASE_COLOR),('GlowOut',prop.MP_EMISSIVE_COLOR),
                          ('RoughOut',prop.MP_ROUGHNESS),('MetalOut',prop.MP_METALLIC)]:
        assert edit.connect_material_property(shader,output,target),output
    edit.layout_material_expressions(mat)
    edit.recompile_material(mat)
    assert lib.save_loaded_asset(mat,False)
    runpy.run_path(str(Path(__file__).with_name('UpdatePickupMotifMaterial.py')),run_name='__main__')
    return mat


if __name__=='__main__':
    build()
