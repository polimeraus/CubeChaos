"""Import the radio carbine and the emissive solar projectile into their gameplay packages."""
from pathlib import Path
import unreal as u

folder = '/Game/Weapons/DefaultWeapon'
source = Path(u.Paths.project_dir()).resolve() / 'ContentSource/ClassicRifle'
assets = u.AssetToolsHelpers.get_asset_tools()
edit = u.MaterialEditingLibrary
lib = u.EditorAssetLibrary

CORE = """
float facing = saturate(dot(normalize(Normal), normalize(Eye)));
float hot = smoothstep(0.68, 0.97, facing);
float latitude = UV.y * 23.0 + 0.55 * sin(UV.x * 6.283 + Clock * 2.1) - Clock * 3.8;
float band = pow(saturate(0.5 + 0.5 * cos(latitude)), 8.0);
float3 amber = lerp(float3(0.83, 0.105, 0.004), Tint * 1.78, smoothstep(0.09, 0.65, facing));
amber *= 0.73 + 0.27 * smoothstep(0.12, 0.68, 0.5 + 0.5 * cos(latitude));
float3 heart = lerp(amber, float3(5.5, 4.0, 1.35), hot);
return heart + band * float3(0.82, 0.43, 0.06) * (1.0 - 0.65 * hot);
"""

ORBIT = """
float glint = pow(saturate(0.5 + 0.5 * sin(dot(Normal, float3(2.2, 3.1, 1.9)) + Clock * 5.4)), 5.0);
return lerp(float3(1.12, 0.22, 0.009), float3(3.4, 1.68, 0.18), glint);
"""

GLOW = """
if (UV.x > 1.5) {
    float facing = saturate(dot(normalize(Normal), normalize(Eye)));
    return float3(1.08, 0.38, 0.02) * pow(saturate(facing), 2.35) * 0.36;
}
float t = saturate(UV.x);
float across = abs(2.0 * UV.y - 1.0);
float width = pow(saturate(1.0 - across), 2.5);
float energy = pow(1.0 - t, 1.35) * (0.77 + 0.23 * cos(t * 23.0 - Clock * 10.0));
return lerp(float3(1.55, 0.69, 0.065), float3(1.0, 0.22, 0.008), t) * width * energy;
"""

WALNUT = """
float grain = Local.x * 3.9 + Local.z * 1.7 + 0.8 * sin(Local.y * 0.30 + Local.z * 0.21);
float streak = 0.5 + 0.5 * sin(grain + 0.38 * sin(grain * 2.15 + Local.y * 0.19));
float pore = pow(1.0 - streak, 6.0);
return lerp(float3(0.097, 0.032, 0.015), float3(0.233, 0.082, 0.031), streak) * (1.0 - 0.19 * pore);
"""


def find_or_create(name, cls, factory):
    path = folder + '/Materials/' + name
    if lib.does_asset_exist(path):
        return u.load_asset(path)
    return assets.create_asset(name, folder+'/Materials', cls, factory)


def optical_material(name, code, pins, wood=False, additive=False):
    mat = find_or_create(name, u.Material, u.MaterialFactoryNew())
    expressions=list(edit.get_material_expressions(mat))
    mat.set_editor_property('shading_model',u.MaterialShadingModel.MSM_DEFAULT_LIT if wood else u.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property('blend_mode',u.BlendMode.BLEND_ADDITIVE if additive else u.BlendMode.BLEND_OPAQUE)
    mat.set_editor_property('two_sided',additive)
    def make(cls):
        for existing in expressions:
            if isinstance(existing,cls):
                return existing
        return edit.create_material_expression(mat,cls)
    def constant(value, prop):
        val=edit.get_material_property_input_node(mat,prop)
        if not isinstance(val,u.MaterialExpressionConstant):
            val=edit.create_material_expression(mat,u.MaterialExpressionConstant)
        val.set_editor_property('r',value)
        assert edit.connect_material_property(val,'',prop)
    kinds = {'Normal':u.MaterialExpressionPixelNormalWS, 'Eye':u.MaterialExpressionCameraVectorWS,
             'Clock':u.MaterialExpressionTime, 'UV':u.MaterialExpressionTextureCoordinate,
             'Tint':u.MaterialExpressionVectorParameter, 'Local':u.MaterialExpressionVertexInterpolator}
    shader=make(u.MaterialExpressionCustom)
    shader.set_editor_property('description',name)
    shader.set_editor_property('code',code)
    shader.set_editor_property('output_type',u.CustomMaterialOutputType.CMOT_FLOAT3)
    inputs=[]
    for label in pins:
        pin=u.CustomInput()
        pin.set_editor_property('input_name',label)
        inputs.append(pin)
    if [str(p.get_editor_property('input_name')) for p in shader.get_editor_property('inputs')] != list(pins):
        shader.set_editor_property('inputs',inputs)
    for label in pins:
        node=make(kinds[label])
        if label=='Tint':
            node.set_editor_property('parameter_name','Tint')
            node.set_editor_property('default_value',u.LinearColor(1.,.72,0.,1.))
        if label=='Local':
            assert edit.connect_material_expressions(make(u.MaterialExpressionPreSkinnedPosition),'',node,'')
        assert edit.connect_material_expressions(node,'RGB' if label=='Tint' else '',shader,label)
    prop=u.MaterialProperty
    if wood:
        assert edit.connect_material_property(shader,'',prop.MP_BASE_COLOR)
        constant(.11,prop.MP_METALLIC)
        constant(.38,prop.MP_ROUGHNESS)
        glow=make(u.MaterialExpressionMultiply)
        glow.set_editor_property('const_b',.20)
        assert edit.connect_material_expressions(shader,'',glow,'A')
        assert edit.connect_material_property(glow,'',prop.MP_EMISSIVE_COLOR)
    else:
        assert edit.connect_material_property(shader,'',prop.MP_EMISSIVE_COLOR)
    if additive:
        constant(1.,prop.MP_OPACITY)
    edit.layout_material_expressions(mat)
    edit.recompile_material(mat)
    assert lib.save_loaded_asset(mat,False)
    return mat


def finishes():
    parent=u.load_asset('/Game/Materials/Weapons/M_RailMetal')
    by_slot={}
    for slot,color,metal,rough,fill in [
            ('RadioJade',(.016,.305,.164),.34,.25,.25),('RadioIvory',(.86,.72,.50),.17,.30,.24),
            ('RadioBrass',(.73,.38,.10),.70,.25,.24),('RadioEbony',(.023,.033,.033),.21,.41,.20),
            ('RadioAmber',(1.,.39,.015),.16,.23,1.25)]:
        mi=find_or_create('MI_'+slot,u.MaterialInstanceConstant,u.MaterialInstanceConstantFactoryNew())
        edit.set_material_instance_parent(mi,parent)
        edit.set_material_instance_vector_parameter_value(mi,'Tint',u.LinearColor(*color,1))
        for key,value in [('Metallic',metal),('Roughness',rough),('Emission',fill)]:
            edit.set_material_instance_scalar_parameter_value(mi,key,value)
        assert lib.save_loaded_asset(mi,False)
        by_slot[slot]=mi
    by_slot['RadioWalnut']=optical_material('M_RadioWalnut',WALNUT,['Local'],wood=True)
    return by_slot


def import_mesh(name, by_slot, lods):
    task=u.AssetImportTask()
    task.filename=str(source/(name+'.fbx'))
    task.destination_path=folder+'/Meshes'
    task.destination_name=name
    task.automated=True
    task.replace_existing=True
    task.replace_existing_settings=True
    task.save=False
    opts=u.FbxImportUI()
    opts.import_mesh=True
    opts.import_as_skeletal=False
    opts.import_materials=False
    opts.import_textures=False
    opts.automated_import_should_detect_type=False
    opts.mesh_type_to_import=u.FBXImportType.FBXIT_STATIC_MESH
    data=opts.static_mesh_import_data
    data.combine_meshes=True
    data.auto_generate_collision=False
    data.convert_scene=True
    data.convert_scene_unit=True
    data.normal_import_method=u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS
    task.options=opts
    assets.import_asset_tasks([task])
    obj=u.load_asset(folder+'/Meshes/'+name)
    assert obj and obj.get_path_name() in task.imported_object_paths,name
    slots=list(obj.static_materials)
    assert {str(s.material_slot_name) for s in slots} == set(by_slot),slots
    for i,slot in enumerate(slots):
        slot.set_editor_property('material_interface',by_slot[str(slot.material_slot_name)])
        slots[i]=slot
    obj.set_editor_property('static_materials',slots)
    reduction=u.StaticMeshReductionOptions()
    reduction.auto_compute_lod_screen_size=False
    reduction.reduction_settings=[u.StaticMeshReductionSettings(percent_triangles=ratio,screen_size=screen)
                                  for ratio,screen in lods]
    assert u.get_editor_subsystem(u.StaticMeshEditorSubsystem).set_lods(obj,reduction)==len(lods)
    assert lib.save_loaded_asset(obj,False)
    return obj


def build():
    rifle=import_mesh('SM_Rifle',finishes(),[(1.,1.),(.58,.43),(.25,.18)])
    optics={'PulseCore':optical_material('M_SunPulseCore',CORE,['UV','Normal','Eye','Clock','Tint']),
            'PulseOrbit':optical_material('M_SunPulseOrbit',ORBIT,['Normal','Clock']),
            'PulseGlow':optical_material('M_SunPulseGlow',GLOW,['UV','Normal','Eye','Clock'],additive=True)}
    pulse=import_mesh('SM_SunPulse',optics,[(1.,1.),(.48,.16),(.23,.055)])
    return rifle,pulse


if __name__=='__main__':
    build()
