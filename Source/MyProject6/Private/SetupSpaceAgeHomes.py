"""Create reusable Space Age homes and their playable architectural preview in Unreal Editor."""
import unreal as u

ROOT = '/Game/Structures/Destructible/SpaceAgeHomes'
MAPS = ROOT + '/Maps'
LIB = u.EditorAssetLibrary
TOOLS = u.AssetToolsHelpers.get_asset_tools()
EDIT = u.MaterialEditingLibrary
PROP = u.MaterialProperty
V, R, C = u.Vector, u.Rotator, u.LinearColor

FINISH = """
float3 n=normalize(Normal), view=normalize(Eye);
float facing=saturate(abs(dot(n,view)));
float3 reflected=reflect(-view,n);
if (Style>.5 && Style<1.5) {
    float sky=smoothstep(-.19,.68,reflected.z);
    float glint=pow(1.-facing,2.8);
    float stripe=frac(UV.x*.23+UV.y*.105);
    float flank=smoothstep(.16,.175,stripe)*(1.-smoothstep(.24,.251,stripe));
    float thread=smoothstep(.265,.273,stripe)*(1.-smoothstep(.281,.289,stripe));
    return Tint*(.60+1.04*sky)+float3(.23,.37,.38)*(.10*sky+.40*glint+.18*flank+.13*thread);
}
if (Style>1.5) {
    float2 p=UV*35., id=floor(p), q=frac(p)-.5;
    float h=frac(sin(dot(id,float2(127.1,311.7)))*43758.5453);
    q+=float2(h-.5,frac(h*17.)-.5)*.36;
    float d=abs(q.x)*.79+abs(q.y)*1.28, aa=clamp(fwidth(d),.013,.32);
    float chip=(1.-smoothstep(.17-aa,.17+aa,d))*(1.-smoothstep(.35,1.3,max(fwidth(p.x),fwidth(p.y))));
    return lerp(Tint,lerp(Tint*.58,Tint*1.38,h),chip);
}
return Tint*(.98+.02*facing);
"""

SKY = """
float3 d=normalize(-Eye);
float y=d.z;
float3 sky=lerp(float3(.66,.83,.79),float3(.18,.48,.62),smoothstep(-.025,.36,y));
sky=lerp(sky,float3(.074,.27,.40),smoothstep(.38,.94,y));
float az=atan2(d.y,d.x);
float ridge=.010+.010*sin(az*8.+.7)+.004*sin(az*23.)+.002*sin(az*55.);
sky=lerp(sky,float3(.52,.46,.43),.62*(1.-smoothstep(ridge-.0015,ridge+.0015,y)));
float3 moon=normalize(float3(-.12,.985,.114));
float radius=.0255, dist=length(d-moon)/radius;
float2 face=float2(dot(d-moon,normalize(cross(float3(0,0,1),moon))),d.z-moon.z)/radius;
float disc=1.-smoothstep(.977,1.014,dist);
return lerp(sky,float3(.93,.82,.61)*(.86+.12*saturate(.6-face.x*.7+face.y*.22)),disc*.64);
"""

GLASS = """
float3 n=normalize(Normal), v=normalize(Eye);
float3 reflection=reflect(-v,n);
float sky=smoothstep(-.34,.6,reflection.z);
float phase=frac(UV.x*.21+UV.y*.095);
float streak=smoothstep(.12,.14,phase)*(1.-smoothstep(.20,.22,phase));
streak+=.58*smoothstep(.245,.253,phase)*(1.-smoothstep(.264,.272,phase));
float edge=pow(1.-saturate(abs(dot(n,v))),2.);
return Tint*(1.20+1.40*sky)+float3(.045,.082,.089)+float3(.24,.35,.34)*(.55*edge+.44*streak);
"""


def asset(name, folder, cls, factory):
    result = TOOLS.create_asset(name, folder, cls, factory)
    assert result, name
    return result


def material(name, folder, build):
    path = folder + '/' + name
    if LIB.does_asset_exist(path):
        return u.load_asset(path)
    m = asset(name, folder, u.Material, u.MaterialFactoryNew())
    build(m)
    EDIT.layout_material_expressions(m)
    EDIT.recompile_material(m)
    assert LIB.save_loaded_asset(m, False)
    return m


def parameter(m, kind, name, value):
    node = EDIT.create_material_expression(m, kind)
    node.set_editor_properties(dict(parameter_name=name, default_value=value))
    return node


def custom(m, code, inputs, output_type=u.CustomMaterialOutputType.CMOT_FLOAT3):
    node = EDIT.create_material_expression(m, u.MaterialExpressionCustom)
    pins = []
    for name, _, _ in inputs:
        pin = u.CustomInput()
        pin.set_editor_property('input_name', name)
        pins.append(pin)
    node.set_editor_properties(dict(inputs=pins, code=code, output_type=output_type))
    for name, source, channel in inputs:
        assert EDIT.connect_material_expressions(source, channel, node, name)
    return node


def build_finish(m):
    scalar = u.MaterialExpressionScalarParameter
    tint = parameter(m, u.MaterialExpressionVectorParameter, 'Tint', C(.84,.70,.48,1))
    gloss = parameter(m, scalar, 'Roughness', .34)
    metal = parameter(m, scalar, 'Metallic', .14)
    mode = parameter(m, scalar, 'Finish', 0.)
    glow = parameter(m, scalar, 'Glow', .04)
    uv = EDIT.create_material_expression(m, u.MaterialExpressionTextureCoordinate)
    eye = EDIT.create_material_expression(m, u.MaterialExpressionCameraVectorWS)
    normal = EDIT.create_material_expression(m, u.MaterialExpressionPixelNormalWS)
    surface = custom(m, FINISH, [('UV',uv,''),('Normal',normal,''),('Eye',eye,''),('Tint',tint,'RGB'),('Style',mode,'')])
    emissive = EDIT.create_material_expression(m, u.MaterialExpressionMultiply)
    assert EDIT.connect_material_expressions(surface, '', emissive, 'A')
    assert EDIT.connect_material_expressions(glow, '', emissive, 'B')
    for node, output in ((surface,PROP.MP_BASE_COLOR),(metal,PROP.MP_METALLIC),
                         (gloss,PROP.MP_ROUGHNESS),(emissive,PROP.MP_EMISSIVE_COLOR)):
        assert EDIT.connect_material_property(node, '', output)


def build_sky(m):
    m.set_editor_properties(dict(shading_model=u.MaterialShadingModel.MSM_UNLIT, two_sided=True, is_sky=True))
    eye = EDIT.create_material_expression(m, u.MaterialExpressionCameraVectorWS)
    assert EDIT.connect_material_property(custom(m,SKY,[('Eye',eye,'')]), '', PROP.MP_EMISSIVE_COLOR)

def build_glazing(m):
    m.set_editor_properties(dict(blend_mode=u.BlendMode.BLEND_TRANSLUCENT,shading_model=u.MaterialShadingModel.MSM_UNLIT))
    tint=parameter(m,u.MaterialExpressionVectorParameter,'Tint',C(.018,.105,.14,1))
    opacity=parameter(m,u.MaterialExpressionScalarParameter,'Opacity',.49)
    eye=EDIT.create_material_expression(m,u.MaterialExpressionCameraVectorWS)
    normal=EDIT.create_material_expression(m,u.MaterialExpressionPixelNormalWS)
    uv=EDIT.create_material_expression(m,u.MaterialExpressionTextureCoordinate)
    pins=[('Normal',normal,''),('Eye',eye,'')]
    color=custom(m,GLASS,pins+[('Tint',tint,'RGB'),('UV',uv,'')])
    alpha=custom(m,'return saturate(Alpha+.30*pow(1.-abs(dot(normalize(Normal),normalize(Eye))),2.));',
                 pins+[('Alpha',opacity,'')],u.CustomMaterialOutputType.CMOT_FLOAT1)
    assert EDIT.connect_material_property(color,'',PROP.MP_EMISSIVE_COLOR)
    assert EDIT.connect_material_property(alpha,'',PROP.MP_OPACITY)


def blueprint(name, master, glazing, comet):
    path = ROOT + '/' + name
    if LIB.does_asset_exist(path):
        bp=u.load_asset(path)
        defaults=u.get_default_object(bp.generated_class())
        if not defaults.get_editor_property('glazing_material'):
            defaults.set_editor_property('glazing_material',glazing)
            u.BlueprintEditorLibrary.compile_blueprint(bp)
            assert LIB.save_loaded_asset(bp,False)
        return bp
    factory = u.BlueprintFactory()
    factory.set_editor_property('parent_class', u.ParametricSpaceAgeHouse)
    bp = asset(name, ROOT, u.Blueprint, factory)
    defaults = u.get_default_object(bp.generated_class())
    defaults.set_editor_property('surface_material', master)
    defaults.set_editor_property('glazing_material', glazing)
    if comet:
        defaults.set_editor_properties(dict(roof_form=u.SpaceAgeRoof.COMET, footprint=u.Vector2D(1630,865),
            deck_height=65., wall_height=265., roof_rise=225., eave_depth=137., facade_bays=12,
            sill_height=69., porthole_spacing=5, sunshade_depth=34., facade_seed=1971, entrance_width=193.,
            porch_depth=169., support_count=6, screen_courses=3, clerestory=False, star_aerial=False))
        palette = defaults.get_editor_property('palette')
        for key, rgb in dict(shell=(.89,.74,.52),roof=(.65,.125,.055),accent=(.94,.48,.075),
                             glass=(.016,.104,.092),metal=(.53,.48,.37),lamp=(.56,1.,.74),
                             terrazzo=(.54,.42,.32)).items():
            palette.set_editor_property(key, C(*rgb,1))
        defaults.set_editor_property('palette', palette)
    u.BlueprintEditorLibrary.compile_blueprint(bp)
    assert LIB.save_loaded_asset(bp, False)
    return bp


def preview(orbit, comet, master):
    path = MAPS + '/Lvl_SpaceAgeHomes'
    if LIB.does_asset_exist(path):
        return
    sky_material = material('M_SpaceAgeHorizon', MAPS, build_sky)
    stage_name = 'MI_SpaceAgePlaza'
    if LIB.does_asset_exist(MAPS+'/'+stage_name):
        stage=u.load_asset(MAPS+'/'+stage_name)
    else:
        stage=asset(stage_name,MAPS,u.MaterialInstanceConstant,u.MaterialInstanceConstantFactoryNew())
        EDIT.set_material_instance_parent(stage,master)
        EDIT.set_material_instance_vector_parameter_value(stage,'Tint',C(.45,.36,.28,1))
        for k,v in [('Roughness',.83),('Metallic',.05),('Glow',.07)]:
            EDIT.set_material_instance_scalar_parameter_value(stage,k,v)
        EDIT.update_material_instance(stage)
        assert LIB.save_loaded_asset(stage,False)
    world = u.EditorLoadingAndSavingUtils.new_blank_map(False)
    settings = world.get_world_settings()
    settings.set_editor_property('force_no_precomputed_lighting',True)
    settings.set_editor_property('default_game_mode',LIB.load_blueprint_class('/Game/FirstPerson/Blueprints/BP_FirstPersonGameMode'))
    actors = u.get_editor_subsystem(u.EditorActorSubsystem)
    def spawn(cls, label, position, rotation=None, folder='Preview'):
        a=actors.spawn_actor_from_class(cls, position, rotation or R())
        a.set_actor_label(label)
        a.set_folder_path(folder)
        return a
    for bp,label,position in [(orbit,'Orbit House',V(-1430,0,0)),(comet,'Comet House',V(1430,270,0))]:
        home=spawn(bp.generated_class(),label,position,folder='Homes')
        assert home.generated_fragments>700 and not home.generation_warning,label
    deck=spawn(u.StaticMeshActor,'Plaza',V(0,0,-25))
    deck.static_mesh_component.set_static_mesh(u.load_asset('/Engine/BasicShapes/Cube'))
    deck.static_mesh_component.set_collision_profile_name('BlockAll')
    deck.static_mesh_component.set_material(0,stage)
    deck.set_actor_scale3d(V(480,480,.5))
    sky=spawn(u.StaticMeshActor,'Desert horizon',V())
    sky.static_mesh_component.set_static_mesh(u.load_asset('/Engine/BasicShapes/Sphere'))
    sky.static_mesh_component.set_material(0,sky_material)
    sky.set_actor_enable_collision(False)
    sky.static_mesh_component.set_collision_profile_name('NoCollision')
    sky.static_mesh_component.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
    sky.static_mesh_component.set_editor_property('cast_shadow',False)
    sky.set_actor_scale3d(V(4700,4700,4700))
    for label,rot,intensity,rgb in [('Sun',R(pitch=-38,yaw=73),5.4,(1,.89,.77)),
                                     ('Sky fill',R(pitch=-23,yaw=154),2.4,(.75,.91,1))]:
        light=spawn(u.DirectionalLight,label,V(0,0,3000),rot).light_component
        light.set_mobility(u.ComponentMobility.MOVABLE)
        light.set_intensity(intensity)
        light.set_light_color(C(*rgb,1))
        light.set_editor_property('light_source_angle',2.15)
        if label!='Sun': light.set_editor_property('cast_shadows',False)
    ambient=u.load_asset('/Engine/MapTemplates/Sky/DaylightAmbientCubemap')
    skyfill=spawn(u.SkyLight,'Ambient daylight',V(0,0,2500)).light_component
    skyfill.set_mobility(u.ComponentMobility.MOVABLE)
    skyfill.set_editor_property('source_type',u.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
    skyfill.set_cubemap(ambient)
    skyfill.set_intensity(1.5)
    skyfill.set_editor_property('cast_shadows',False)
    pp=spawn(u.PostProcessVolume,'Architectural daylight',V())
    pp.set_editor_property('unbound',True)
    exposure=pp.get_editor_property('settings')
    exposure.set_editor_properties(dict(override_auto_exposure_method=True,auto_exposure_method=u.AutoExposureMethod.AEM_MANUAL,
        override_auto_exposure_apply_physical_camera_exposure=True,auto_exposure_apply_physical_camera_exposure=False,
        override_auto_exposure_bias=True,auto_exposure_bias=.22,ambient_cubemap=ambient,
        override_ambient_cubemap_intensity=True,ambient_cubemap_intensity=.33,
        override_bloom_intensity=True,bloom_intensity=.15,override_vignette_intensity=True,vignette_intensity=.16))
    pp.set_editor_property('settings',exposure)
    focus=V(0,80,265)
    view=V(290,-5100,1090)
    look=u.MathLibrary.find_look_at_rotation
    cam=spawn(u.CameraActor,'Both homes',view,look(view,focus))
    cam.camera_component.set_field_of_view(58.)
    start=V(-850,-2400,97)
    spawn(u.PlayerStart,'Player Start',start,look(start,V(-1430,0,230)))
    u.get_editor_subsystem(u.UnrealEditorSubsystem).set_level_viewport_camera_info(view,look(view,focus))
    actors.set_selected_level_actors([])
    assert u.EditorLoadingAndSavingUtils.save_map(world,path)


def main():
    for folder in (ROOT+'/Materials', MAPS):
        if not LIB.does_directory_exist(folder):
            assert LIB.make_directory(folder)
    master=material('M_SpaceAgeFinish',ROOT+'/Materials',build_finish)
    glazing=material('M_SpaceAgeGlazing',ROOT+'/Materials',build_glazing)
    orbit=blueprint('BP_OrbitHouse',master,glazing,False)
    comet=blueprint('BP_CometHouse',master,glazing,True)
    preview(orbit,comet,master)
    u.log('SPACE_AGE_HOMES_READY')


if __name__=='__main__':
    main()
