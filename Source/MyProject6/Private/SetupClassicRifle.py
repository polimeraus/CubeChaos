"""Import the Solstice carbine, finite laser bolt and one-shot. Use -RifleOnly or -ProjectileOnly for a targeted update."""
from pathlib import Path
import unreal as u

folder = '/Game/Weapons/DefaultWeapon'
source = Path(u.Paths.project_dir()).resolve() / 'ContentSource/ClassicRifle'
assets = u.AssetToolsHelpers.get_asset_tools()
edit = u.MaterialEditingLibrary
lib = u.EditorAssetLibrary

CORE = """
float facing=saturate(abs(dot(normalize(Normal),normalize(Eye))));
float3 edge=max(Tint.rgb,0.)*float3(2.95,1.35,2.45);
float3 heart=lerp(Tint.rgb,float3(1.,.965,.86),.80)*5.3;
float tail=lerp(.54,1.,smoothstep(.005,.21,UV.x));
return lerp(edge,heart,smoothstep(.20,.93,facing))*tail;
"""

GLOW = """
float facing=saturate(abs(dot(normalize(Normal),normalize(Eye))));
float width=.20*pow(facing,2.0)+2.05*pow(facing,7.5);
float lengthwise=smoothstep(.005,.25,UV.x)*(1.-.62*smoothstep(.88,1.,UV.x));
return Tint.rgb*float3(1.75,.74,1.4)*width*lengthwise;
"""

CORONA = """
float facing = saturate(dot(normalize(Normal), normalize(Eye)));
float phase = UV.y*17.0 + .85*sin(UV.x*6.28318 + Clock*.94) - Clock*1.25;
float ribbon = smoothstep(.65,.98,.5+.5*cos(phase));
float3 amber = lerp(float3(.36,.066,.003),float3(1.48,.61,.046),smoothstep(.02,.70,facing));
return lerp(amber,float3(2.35,1.33,.22),smoothstep(.65,.99,facing)) + ribbon*float3(.29,.13,.012);
"""

PHOSPHOR = """
float2 uv=saturate(UV);
float2 lattice=abs(frac(uv*float2(8,4)+.5)-.5);
float grid=1.-smoothstep(.022,.085,min(lattice.x,lattice.y));
float phase=uv.x*1.30-Clock*.27+.07;
float y=.48+.23*sin(6.28318*phase)+.065*sin(6.28318*(2.*phase-.22));
float d=abs(uv.y-y);
float aa=clamp(fwidth(uv.y),.006,.09);
float beam=1.-smoothstep(.018,.026+aa,d);
float halo=exp(-d*d/.006);
float sweep=1.-frac(Clock*.34-uv.x);
float3 face=float3(.008,.043,.036)+grid*float3(.012,.045,.032);
return face+(beam+.19*halo)*(.76+.27*sweep)*float3(.35,1.48,.88);
"""


def find_or_create(name, cls, factory):
    path = folder + '/Materials/' + name
    if lib.does_asset_exist(path):
        return u.load_asset(path)
    return assets.create_asset(name, folder+'/Materials', cls, factory)


def optical_material(name, code, pins, additive=False):
    mat = find_or_create(name, u.Material, u.MaterialFactoryNew())
    expressions=list(edit.get_material_expressions(mat))
    mat.set_editor_property('shading_model',u.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property('blend_mode',u.BlendMode.BLEND_ADDITIVE if additive else u.BlendMode.BLEND_OPAQUE)
    mat.set_editor_property('two_sided',False)
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
             'Tint':u.MaterialExpressionVectorParameter}
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
        assert edit.connect_material_expressions(node,'RGB' if label=='Tint' else '',shader,label)
    prop=u.MaterialProperty
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
            ('SolMandarin',(.76,.164,.032),.33,.24,.22),('SolPetrol',(.022,.073,.09),.53,.30,.32),
            ('SolNickel',(.57,.62,.62),.76,.24,.22),('RadioIvory',(.86,.72,.50),.17,.30,.24),
            ('RadioEbony',(.023,.033,.033),.21,.41,.20)]:
        path=folder+'/Materials/MI_'+slot
        if slot in ('RadioIvory','RadioEbony') and lib.does_asset_exist(path):
            by_slot[slot]=u.load_asset(path)
            continue
        mi=find_or_create('MI_'+slot,u.MaterialInstanceConstant,u.MaterialInstanceConstantFactoryNew())
        edit.set_material_instance_parent(mi,parent)
        edit.set_material_instance_vector_parameter_value(mi,'Tint',u.LinearColor(*color,1))
        for key,value in [('Metallic',metal),('Roughness',rough),('Emission',fill)]:
            edit.set_material_instance_scalar_parameter_value(mi,key,value)
        assert lib.save_loaded_asset(mi,False)
        by_slot[slot]=mi
    by_slot['SolCorona']=optical_material('M_SolCorona',CORONA,['UV','Normal','Eye','Clock'])
    by_slot['SolPhosphor']=optical_material('M_SolPhosphor',PHOSPHOR,['UV','Clock'])
    return by_slot


def import_mesh(name, by_slot, lods, legacy_slots=()):
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
    opts.reset_to_fbx_on_material_conflict=True
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
    names={str(s.material_slot_name) for s in slots}
    assert set(by_slot)<=names<=set(by_slot).union(legacy_slots),slots
    for i,slot in enumerate(slots):
        key=str(slot.material_slot_name)
        if key in by_slot:
            slot.set_editor_property('material_interface',by_slot[key])
        slots[i]=slot
    obj.set_editor_property('static_materials',slots)
    reduction=u.StaticMeshReductionOptions()
    reduction.auto_compute_lod_screen_size=False
    reduction.reduction_settings=[u.StaticMeshReductionSettings(percent_triangles=ratio,screen_size=screen)
                                  for ratio,screen in lods]
    editor=u.get_editor_subsystem(u.StaticMeshEditorSubsystem)
    assert editor.set_lods(obj,reduction)==len(lods)
    for lod in range(len(lods)):
        used={str(slots[editor.get_lod_material_slot(obj,lod,i)].material_slot_name)
              for i in range(obj.get_num_sections(lod))}
        assert used==set(by_slot),(name,lod,used)
    assert lib.save_loaded_asset(obj,False)
    return obj


def build_rifle():
    return import_mesh('SM_Rifle',finishes(),[(1.,1.),(.58,.43),(.25,.18)])


def build_projectile():
    optics={'PulseCore':optical_material('M_SunPulseCore',CORE,['UV','Normal','Eye','Tint']),
            'PulseGlow':optical_material('M_SunPulseGlow',GLOW,['UV','Normal','Eye','Tint'],additive=True)}
    # Interchange retains old slot indices for existing overrides. Only the filament and halo have geometry.
    bolt=import_mesh('SM_SunPulse',optics,[(1.,1.),(.65,.18),(.38,.065)],legacy_slots=('PulseOrbit',))
    directory=folder+'/Audio'
    if not lib.does_directory_exist(directory):
        assert lib.make_directory(directory)
    name='S_SolBoltFire'
    task=u.AssetImportTask()
    task.set_editor_properties(dict(filename=str(source/(name+'.wav')),destination_path=directory,
                                   destination_name=name,automated=True,replace_existing=True,save=False))
    assets.import_asset_tasks([task])
    wave=u.load_asset(directory+'/'+name)
    assert isinstance(wave,u.SoundWave) and .20 < wave.duration < .23
    wave.set_editor_property('looping',False)
    wave.set_sound_asset_compression_type(u.SoundAssetCompressionType.PCM)
    wave.set_editor_property('loading_behavior',u.SoundWaveLoadingBehavior.FORCE_INLINE)
    assert lib.save_loaded_asset(wave,False)
    return bolt


def build():
    return build_rifle(),build_projectile()


if __name__=='__main__':
    command=u.SystemLibrary.get_command_line()
    if '-RifleOnly' in command:
        build_rifle()
    elif '-ProjectileOnly' in command:
        build_projectile()
    else:
        build()
