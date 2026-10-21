"""Persist lightweight neon weapon shaders; reuse nodes to avoid rooted-node deletion in UE 5.8."""
from pathlib import Path
import unreal

edit=unreal.MaterialEditingLibrary
path='/Game/Weapons/FuseBomb/Materials/M_BombEffect'
fx=unreal.load_asset(path)
if not fx:
    fx=unreal.AssetToolsHelpers.get_asset_tools().create_asset('M_BombEffect',path.rsplit('/',1)[0],unreal.Material,unreal.MaterialFactoryNew())
existing=list(edit.get_material_expressions(fx))
for item in existing:
    if isinstance(item,unreal.MaterialExpressionTextureObject):item.set_editor_property('texture',None)
fx.set_editor_property('blend_mode',unreal.BlendMode.BLEND_ADDITIVE)
fx.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
fx.set_editor_property('two_sided',True)

def node(cls):
    for i,item in enumerate(existing):
        if isinstance(item,cls):return existing.pop(i)
    return edit.create_material_expression(fx,cls)

shader=node(unreal.MaterialExpressionCustom)
shader.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT4)
vertex=node(unreal.MaterialExpressionVertexColor)
rgba=node(unreal.MaterialExpressionAppendVector)
assert edit.connect_material_expressions(vertex,'',rgba,'A')
assert edit.connect_material_expressions(vertex,'A',rgba,'B')
age=node(unreal.MaterialExpressionScalarParameter);age.set_editor_property('parameter_name','Age')
sources={'UV':node(unreal.MaterialExpressionTextureCoordinate),'Particle':rgba,'Age':age}
inputs=[]
for name in sources:
    pin=unreal.CustomInput();pin.set_editor_property('input_name',name);inputs.append(pin)
shader.set_editor_property('inputs',inputs)
for name,source in sources.items():assert edit.connect_material_expressions(source,'',shader,name)
shader.set_editor_property('code',Path(__file__).with_name('BombEffect.ush').read_text())
for channels,prop in [('rgb',unreal.MaterialProperty.MP_EMISSIVE_COLOR),('a',unreal.MaterialProperty.MP_OPACITY)]:
    mask=node(unreal.MaterialExpressionComponentMask)
    for channel in 'rgba':mask.set_editor_property(channel,channel in channels)
    assert edit.connect_material_expressions(shader,'',mask,'')
    if channels=='a':
        fade=node(unreal.MaterialExpressionDepthFade);fade.set_editor_property('fade_distance_default',20.)
        assert edit.connect_material_expressions(mask,'',fade,'')
        mask=fade
    assert edit.connect_material_property(mask,'',prop)
edit.recompile_material(fx)
assert unreal.EditorAssetLibrary.save_loaded_asset(fx,False)

bfg=unreal.load_asset('/Game/Weapons/BFG/Materials/M_BFGPlasma');assert bfg
bfg_shader=next(n for n in edit.get_material_expressions(bfg) if isinstance(n,unreal.MaterialExpressionCustom))
bfg_shader.set_editor_property('code',Path(__file__).with_name('BFGPlasma.ush').read_text())
edit.recompile_material(bfg)
assert unreal.EditorAssetLibrary.save_loaded_asset(bfg,False)
unreal.log('NEON_WEAPON_MATERIALS_SAVED')
