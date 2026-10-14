"""Import original railgun, create its particles, and persist player presentation defaults."""
from pathlib import Path
import json, runpy, unreal

root = Path(unreal.Paths.project_dir())
base = '/Game/Weapons/Railgun'
assets = unreal.AssetToolsHelpers.get_asset_tools()
edit = unreal.MaterialEditingLibrary
out = root / 'Saved/Railgun/Quake'
out.mkdir(parents=True, exist_ok=True)

def make_material(name, code, inputs, additive=False):
    path = base + '/' + name
    m = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else assets.create_asset(name, base, unreal.Material, unreal.MaterialFactoryNew())
    # Reuse existing nodes when rerun: constructor-loaded expressions can be rooted.
    expressions = list(edit.get_material_expressions(m))
    def node(cls, parameter=None):
        for n in expressions:
            if isinstance(n, cls) and (parameter is None or str(n.get_editor_property('parameter_name')) == parameter): return n
        n = edit.create_material_expression(m, cls); expressions.append(n); return n
    if additive:
        m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
        m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_ADDITIVE)
        m.set_editor_property('two_sided', True)
    shader = node(unreal.MaterialExpressionCustom)
    pins=[]
    sources={}
    for name, value in inputs.items():
        p=unreal.CustomInput();p.set_editor_property('input_name',name);pins.append(p)
        if name == 'UV': sources[name]=(node(unreal.MaterialExpressionTextureCoordinate),'')
        elif name == 'VertexAlpha': sources[name]=(node(unreal.MaterialExpressionVertexColor),'A')
        elif name == 'VertexMode': sources[name]=(node(unreal.MaterialExpressionVertexColor),'R')
        elif isinstance(value, tuple):
            n=node(unreal.MaterialExpressionVectorParameter,name)
            n.set_editor_property('parameter_name',name);n.set_editor_property('default_value',unreal.LinearColor(*value,1));sources[name]=(n,'RGB')
        else:
            n=node(unreal.MaterialExpressionScalarParameter,name)
            n.set_editor_property('parameter_name',name);n.set_editor_property('default_value',float(value));sources[name]=(n,'')
    shader.set_editor_property('inputs',pins)
    shader.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    shader.set_editor_property('code',code)
    for name,(n,pin) in sources.items(): assert edit.connect_material_expressions(n,pin,shader,name)
    assert edit.connect_material_property(shader,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR if additive else unreal.MaterialProperty.MP_BASE_COLOR)
    if additive:
        opacity=node(unreal.MaterialExpressionConstant);opacity.set_editor_property('r',1)
        edit.connect_material_property(opacity,'',unreal.MaterialProperty.MP_OPACITY)
    else:
        for name,prop in [('Metallic',unreal.MaterialProperty.MP_METALLIC),('Roughness',unreal.MaterialProperty.MP_ROUGHNESS)]:
            edit.connect_material_property(sources[name][0],'',prop)
        glow=node(unreal.MaterialExpressionMultiply)
        edit.connect_material_expressions(sources['Tint'][0],'RGB',glow,'A')
        edit.connect_material_expressions(sources['Emission'][0],'',glow,'B')
        edit.connect_material_property(glow,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    edit.layout_material_expressions(m);edit.recompile_material(m)
    assert unreal.EditorAssetLibrary.save_loaded_asset(m,False)
    return m

make_material('M_RailSpiral', Path(__file__).with_name('RailgunSpiral.ush').read_text(),
              {'UV':0,'VertexAlpha':0,'VertexMode':0,'Tint':(.008,.5,1),'Brightness':7,'Density':1},True)

surface=make_material('M_RailMetal','''
float grain=frac(sin(dot(floor(UV*170.0),float2(12.9898,78.233)))*43758.5453);
return Tint*(.91+grain*.18);
''', {'UV':0,'Tint':(.39,.43,.45),'Metallic':.82,'Roughness':.3,'Emission':0})
palette={
 'RailSilver':((.39,.43,.45),.82,.3,0),
 'RailGunmetal':((.045,.06,.065),.75,.4,0),
 'RailRubber':((.013,.019,.018),.08,.65,0),
 'RailEnergy':((.12,.8,.025),.25,.25,2.4),
 'RailBrass':((.28,.20,.07),.8,.35,0)}
materials={}
for name,(color,metal,rough,emission) in palette.items():
    path=base+'/MI_'+name
    mi=unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else assets.create_asset('MI_'+name,base,unreal.MaterialInstanceConstant,unreal.MaterialInstanceConstantFactoryNew())
    edit.set_material_instance_parent(mi,surface)
    edit.set_material_instance_vector_parameter_value(mi,'Tint',unreal.LinearColor(*color,1))
    for key,val in [('Metallic',metal),('Roughness',rough),('Emission',emission)]:edit.set_material_instance_scalar_parameter_value(mi,key,val)
    unreal.EditorAssetLibrary.save_loaded_asset(mi,False);materials[name]=mi

task=unreal.AssetImportTask()
for key,value in {'filename':str(root/'ContentSource/Railgun/SM_QuakeRailgun.fbx'),'destination_path':base,
                  'destination_name':'SM_QuakeRailgun','automated':True,'replace_existing':True,'save':True}.items():task.set_editor_property(key,value)
options=unreal.FbxImportUI()
for key,value in {'import_mesh':True,'import_as_skeletal':False,'import_materials':False,'import_textures':False,
                  'mesh_type_to_import':unreal.FBXImportType.FBXIT_STATIC_MESH,'automated_import_should_detect_type':False}.items():options.set_editor_property(key,value)
data=options.get_editor_property('static_mesh_import_data')
for key,value in {'combine_meshes':True,'auto_generate_collision':False,'convert_scene':True,'convert_scene_unit':True,
                  'normal_import_method':unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS}.items():data.set_editor_property(key,value)
task.set_editor_property('options',options);assets.import_asset_tasks([task])
mesh=unreal.load_asset(base+'/SM_QuakeRailgun');assert mesh
slots=mesh.get_editor_property('static_materials')
for index,slot in enumerate(slots):
    name=str(slot.get_editor_property('material_slot_name'));assert name in materials,name
    slot.set_editor_property('material_interface',materials[name]);slots[index]=slot
mesh.set_editor_property('static_materials',slots)
assert unreal.EditorAssetLibrary.save_loaded_asset(mesh,False)

runpy.run_path(str(Path(__file__).with_name('SetupRailgunTrail.py')),run_name='__main__')

bp=unreal.load_asset('/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter')
defaults=unreal.get_default_object(bp.generated_class())
visuals=unreal.RailgunVisualSettings()
defaults.set_editor_property('railgun_visuals',visuals)
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
assert unreal.EditorAssetLibrary.save_loaded_asset(bp,False)

# Reconcile only this presentation struct on placed players. Pickup grants are
# native and need no per-instance flags, transforms, labels, or references.
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
report={'pickups':[], 'players_updated':[], 'mesh_bounds':str(mesh.get_bounds())}
for actor in actors:
    if isinstance(actor,unreal.ParametricPickup):
        report['pickups'].append({'label':actor.get_actor_label(),'class':actor.get_class().get_path_name()})
    if isinstance(actor,unreal.MyProject6Character):
        actor.set_editor_property('railgun_visuals',visuals)
        report['players_updated'].append(actor.get_actor_label())
assert report['pickups'], 'No native pickup actors found in gameplay map'
if report['players_updated']:assert level.save_current_level()
(out/'Setup.json').write_text(json.dumps(report,indent=2))
unreal.log('QUAKE_RAILGUN_SETUP_OK '+str(len(report['pickups']))+' pickups')
