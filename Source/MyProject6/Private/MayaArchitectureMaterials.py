"""Texture-free architectural finishes and translucent glass for the revised Maya pyramid."""
import unreal

ROOT='/Game/Structures/Destructible/MayaPyramid/Materials'
edit=unreal.MaterialEditingLibrary
lib=unreal.EditorAssetLibrary
assets=unreal.AssetToolsHelpers.get_asset_tools()

def material(name, glass=False, animated=False):
    path=ROOT+'/'+name
    if lib.does_asset_exist(path): return unreal.load_asset(path)
    mat=assets.create_asset(name,ROOT,unreal.Material,unreal.MaterialFactoryNew())
    if glass:
        mat.set_editor_property('blend_mode',unreal.BlendMode.BLEND_TRANSLUCENT)
        mat.set_editor_property('translucency_lighting_mode',unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
        mat.set_editor_property('two_sided',False)
    inputs={}
    def scalar(name,value):
        node=edit.create_material_expression(mat,unreal.MaterialExpressionScalarParameter)
        node.set_editor_property('parameter_name',name)
        node.set_editor_property('default_value',value)
        inputs[name]=node
        return node
    def vector(name,value):
        node=edit.create_material_expression(mat,unreal.MaterialExpressionVectorParameter)
        node.set_editor_property('parameter_name',name)
        node.set_editor_property('default_value',unreal.LinearColor(*value,1))
        inputs[name]=node
        return node
    color=vector('Color',(.6,.6,.6))
    tint=vector('GlowTint',(1.,.69,.37))
    rough=scalar('Roughness',.6)
    metal=scalar('Metallic',0.)
    spec=scalar('Specular',.5)
    glow=scalar('Glow',0.)
    for node,prop in ((color,unreal.MaterialProperty.MP_BASE_COLOR),(rough,unreal.MaterialProperty.MP_ROUGHNESS),
                      (metal,unreal.MaterialProperty.MP_METALLIC),(spec,unreal.MaterialProperty.MP_SPECULAR)):
        assert edit.connect_material_property(node,'',prop)
    if animated:
        clock=edit.create_material_expression(mat,unreal.MaterialExpressionTime)
        inputs['Clock']=clock
        scalar('InteriorBrightness',1.)
        scalar('AnimateLighting',1.)
        scalar('LightingCycle',8.)
        shader=edit.create_material_expression(mat,unreal.MaterialExpressionCustom)
        names=('GlowTint','Glow','Clock','InteriorBrightness','AnimateLighting','LightingCycle')
        pins=[]
        for name in names:
            pin=unreal.CustomInput();pin.set_editor_property('input_name',name);pins.append(pin)
        shader.set_editor_property('inputs',pins)
        shader.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT3)
        shader.set_editor_property('description','Warm architectural lighting fades smoothly; no actor tick or moving geometry')
        shader.set_editor_property('code','''
float wave = 0.5 + 0.5 * cos(Clock * 6.28318530718 / max(LightingCycle, 2.0));
float fade = lerp(0.06, 1.0, smoothstep(0.12, 0.88, wave));
return GlowTint.rgb * max(Glow, 0.0) * max(InteriorBrightness, 0.0) * lerp(1.0, fade, saturate(AnimateLighting));
''')
        for name in names: assert edit.connect_material_expressions(inputs[name],'',shader,name)
        assert edit.connect_material_property(shader,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    else:
        multiply=edit.create_material_expression(mat,unreal.MaterialExpressionMultiply)
        assert edit.connect_material_expressions(tint,'',multiply,'A')
        assert edit.connect_material_expressions(glow,'',multiply,'B')
        assert edit.connect_material_property(multiply,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    if glass:
        opacity=scalar('Opacity',.18)
        assert edit.connect_material_property(opacity,'',unreal.MaterialProperty.MP_OPACITY)
    edit.layout_material_expressions(mat)
    assert not edit.recompile_material(mat)
    assert lib.save_loaded_asset(mat,False)
    return mat

def create_palette():
    solid=material('M_MayaArchitecturalSolid')
    glass=material('M_MayaArchitecturalGlass',glass=True)
    lighting=material('M_MayaArchitecturalLighting',animated=True)
    # Names describe their actual finishes; the previous neon assets are kept for compatibility.
    definitions={
        'ArchitecturalGlass':(glass,(.16,.24,.27),.12,.05,0.,'wall_material'),
        'IvoryConcrete':(solid,(.65,.68,.65),.67,0.,0.,'ledge_material'),
        'BronzeFrames':(solid,(.055,.048,.036),.28,.7,0.,'accent_material'),
        'BlueGraphite':(solid,(.022,.058,.095),.27,.3,.015,'stair_material'),
        'WarmRooms':(lighting,(.48,.35,.21),.8,0.,.24,'shrine_material'),
        'WarmLight':(lighting,(.8,.72,.52),.5,0.,3.,'crest_material'),
        'TerraceStone':(solid,(.29,.32,.31),.73,0.,0.,'gold_material'),
        'CharcoalSteps':(solid,(.035,.044,.055),.67,0.,0.,'step_material'),
        'Courtyard':(solid,(.16,.20,.22),.68,0.,0.,None),
        'Pool':(solid,(.008,.12,.15),.27,.1,.04,None),
    }
    result={}
    for name,(parent,color,rough,metal,glow,prop) in definitions.items():
        path=ROOT+'/MI_Maya_'+name
        mi=unreal.load_asset(path) if lib.does_asset_exist(path) else assets.create_asset('MI_Maya_'+name,ROOT,unreal.MaterialInstanceConstant,unreal.MaterialInstanceConstantFactoryNew())
        edit.set_material_instance_parent(mi,parent)
        edit.set_material_instance_vector_parameter_value(mi,'Color',unreal.LinearColor(*color,1))
        tint=(1.,.69,.37) if parent==lighting else color
        edit.set_material_instance_vector_parameter_value(mi,'GlowTint',unreal.LinearColor(*tint,1))
        for key,value in {'Roughness':rough,'Metallic':metal,'Glow':glow}.items():edit.set_material_instance_scalar_parameter_value(mi,key,value)
        if parent==glass:
            edit.set_material_instance_scalar_parameter_value(mi,'Opacity',.12)
            edit.set_material_instance_scalar_parameter_value(mi,'Specular',.85)
        assert lib.save_loaded_asset(mi,False)
        if prop:result[prop]=mi
    return result
