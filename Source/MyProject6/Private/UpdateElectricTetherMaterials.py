"""Author and persist the electric discharge and translucent plasma materials."""
from pathlib import Path
import unreal

source=Path(__file__).parent
base='/Game/Weapons/ElectricTether/Materials'
edit=unreal.MaterialEditingLibrary
assets=unreal.AssetToolsHelpers.get_asset_tools()

def material(name,shader_file,membrane=False):
 path=base+'/'+name
 m=unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else assets.create_asset(name,base,unreal.Material,unreal.MaterialFactoryNew())
 m.set_editor_property('blend_mode',unreal.BlendMode.BLEND_TRANSLUCENT if membrane else unreal.BlendMode.BLEND_ADDITIVE)
 m.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
 m.set_editor_property('two_sided',not membrane)
 # Reuse loaded expressions; deleting rooted expressions in a commandlet is
 # unsupported. Reassigning every pin also keeps repeated runs idempotent.
 existing=list(edit.get_material_expressions(m))
 def node(cls,x=0,y=0):
  for i,e in enumerate(existing):
   if isinstance(e,cls):return existing.pop(i)
  return edit.create_material_expression(m,cls,x,y)
 shader=node(unreal.MaterialExpressionCustom,0,0)
 shader.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT4)
 inputs=[('UV',unreal.MaterialExpressionTextureCoordinate,''),('Color',unreal.MaterialExpressionVertexColor,''),('Alpha',unreal.MaterialExpressionVertexColor,'A'),('DepthFade',unreal.MaterialExpressionDepthFade,'')]
 if membrane:inputs += [('Time',unreal.MaterialExpressionTime,''),('Normal',unreal.MaterialExpressionPixelNormalWS,''),('View',unreal.MaterialExpressionCameraVectorWS,'')]
 pins=[]
 for name,cls,output in inputs:
  pin=unreal.CustomInput();pin.set_editor_property('input_name',name);pins.append(pin)
 shader.set_editor_property('inputs',pins)
 for i,(name,cls,output) in enumerate(inputs):
  expression=node(cls,-400,i*110)
  if name=='DepthFade':expression.set_editor_property('fade_distance_default',18.)
  assert edit.connect_material_expressions(expression,output,shader,name)
 shader.set_editor_property('code',(source/shader_file).read_text())
 for i,(channels,prop) in enumerate([('rgb',unreal.MaterialProperty.MP_EMISSIVE_COLOR),('a',unreal.MaterialProperty.MP_OPACITY)]):
  mask=node(unreal.MaterialExpressionComponentMask,400,i*160)
  for c in 'rgba':mask.set_editor_property(c,c in channels)
  assert edit.connect_material_expressions(shader,'',mask,'')
  assert edit.connect_material_property(mask,'',prop)
 edit.recompile_material(m)
 assert unreal.EditorAssetLibrary.save_loaded_asset(m,False)
 return m

material('M_ArcField','ArcRibbonMaterial.ush')
material('M_ArcEnvelope','ArcEnvelopeMaterial.ush',True)
unreal.log('ARC_PLASMA_MATERIALS_SAVED')
