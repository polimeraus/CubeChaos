"""Pale coursed limestone, with continuous courses around the spiral walls."""
import unreal

def update_materials():
    folder='/Game/Structures/Destructible/SpiralTower/Materials'
    edit=unreal.MaterialEditingLibrary
    master=unreal.load_asset(folder+'/M_SpiralTowerMasonry')
    expressions=edit.get_material_expressions(master)
    custom=next(e for e in expressions if isinstance(e,unreal.MaterialExpressionCustom))
    origin=next((e for e in expressions if isinstance(e,unreal.MaterialExpressionActorPositionWS)),None)
    if not origin:origin=edit.create_material_expression(master,unreal.MaterialExpressionActorPositionWS)
    pins=list(custom.get_editor_property('inputs'))
    if 'Origin' not in [str(p.get_editor_property('input_name')) for p in pins]:
        pin=unreal.CustomInput();pin.set_editor_property('input_name','Origin');pins.append(pin)
        custom.set_editor_property('inputs',pins)
    assert edit.connect_material_expressions(origin,'',custom,'Origin')
    custom.set_editor_property('code','''
struct StoneNoise {
 float Hash(float3 p) { p=frac(p*.1031); p+=dot(p,p.yzx+33.33); return frac((p.x+p.y)*p.z); }
 float Value(float3 p) {
  float3 i=floor(p),f=frac(p); f=f*f*(3-2*f);
  return lerp(lerp(lerp(Hash(i),Hash(i+float3(1,0,0)),f.x),lerp(Hash(i+float3(0,1,0)),Hash(i+float3(1,1,0)),f.x),f.y),
   lerp(lerp(Hash(i+float3(0,0,1)),Hash(i+float3(1,0,1)),f.x),lerp(Hash(i+float3(0,1,1)),Hash(i+float3(1,1,1)),f.x),f.y),f.z);
 }
}; StoneNoise noise;
float3 p=Position-Origin,n=abs(normalize(Normal));
float row=floor(p.z/90.0);
float horizontal=atan2(p.x,-p.y)*length(p.xy);
if(p.z<221.0) horizontal=n.x>n.y?p.y:p.x;
float2 tile=float2((horizontal+fmod(row,2.0)*95.0)/190.0,p.z/90.0);
float2 scale=float2(190.0,90.0);
if(n.z>.7) { tile=p.xy/float2(145.0,92.0); scale=float2(145.0,92.0); }
float2 edge=min(frac(tile),1-frac(tile))*scale;
float seam=1-smoothstep(.45,1.35,min(edge.x,edge.y));
float variation=noise.Hash(float3(floor(tile),row));
float clouds=.74+.16*noise.Value(p*.025)+.07*noise.Value(p*.11)+.03*variation;
return Tint*clouds*(1-seam*Joints);
''')
    edit.recompile_material(master)
    assert unreal.EditorAssetLibrary.save_loaded_asset(master,False)
    for name,color,joints in [('Stone',(.82,.70,.52),.17),('Stairs',(.85,.76,.61),.12)]:
        m=unreal.load_asset(folder+'/MI_SpiralTower'+name)
        edit.set_material_instance_parent(m,master)
        edit.set_material_instance_vector_parameter_value(m,'StoneColor',unreal.LinearColor(*color,1))
        edit.set_material_instance_scalar_parameter_value(m,'JointStrength',joints)
        assert unreal.EditorAssetLibrary.save_loaded_asset(m,False)
    path=folder+'/M_SpiralTowerStudioGround'
    ground=unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
    if not ground:
        ground=unreal.AssetToolsHelpers.get_asset_tools().create_asset('M_SpiralTowerStudioGround',folder,unreal.Material,unreal.MaterialFactoryNew())
        color=edit.create_material_expression(ground,unreal.MaterialExpressionConstant3Vector)
        color.set_editor_property('constant',unreal.LinearColor(.82,.81,.79,1))
        rough=edit.create_material_expression(ground,unreal.MaterialExpressionConstant);rough.set_editor_property('r',.95)
        assert edit.connect_material_property(color,'',unreal.MaterialProperty.MP_BASE_COLOR)
        assert edit.connect_material_property(rough,'',unreal.MaterialProperty.MP_ROUGHNESS)
        edit.recompile_material(ground)
        assert unreal.EditorAssetLibrary.save_loaded_asset(ground,False)
    sky=unreal.load_asset(folder+'/M_SpiralTowerPreviewSky')
    color=edit.get_material_property_input_node(sky,unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    color.set_editor_property('constant',unreal.LinearColor(1.4,1.38,1.34,1))
    edit.recompile_material(sky)
    assert unreal.EditorAssetLibrary.save_loaded_asset(sky,False)
    return ground
