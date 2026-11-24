"""Create the reusable Helix Monument materials, Blueprint and isolated preview level in Unreal Editor."""
import unreal as u


ROOT = '/Game/Structures/Destructible/HelixMonument'
MATERIALS = ROOT + '/Materials'
MAPS = ROOT + '/Maps'
EDIT = u.MaterialEditingLibrary
ASSETS = u.EditorAssetLibrary
TOOLS = u.AssetToolsHelpers.get_asset_tools()

# Both fine inclusions and larger stone mottling are anchored in the mesh UVs.
# Attenuating sub-pixel aggregate keeps the thin silhouette quiet at a distance.
STONE_SHADER = """
float2 q = UV * Density;
float2 tile = floor(q);
float2 p = frac(q) - .5;
float3 hash = frac(float3(tile.xyx) * float3(.1031,.1030,.0973));
hash += dot(hash, hash.yxz + 33.33);
float3 h = frac((hash.xxy + hash.yzz) * hash.zyx);
float2 v = (p - (h.xy - .5)*.53) * float2(1.0,1.32);
float edge = length(v);
float width = max(.035, min(.4, max(fwidth(q.x),fwidth(q.y))*.65));
float chip = 1.-smoothstep(.19-width,.19+width,edge);
float grit = (h.z-.5)*.22 + chip * lerp(-.7, .88, h.x);
float visibility = 1.-smoothstep(.37,1.45,max(fwidth(q.x),fwidth(q.y)));
float2 coarse = UV * 1.7;
float2 c = floor(coarse), t = smoothstep(0.,1.,frac(coarse));
float4 seeds = float4(dot(c,float2(71.4,39.7)), dot(c+float2(1,0),float2(71.4,39.7)),
                      dot(c+float2(0,1),float2(71.4,39.7)), dot(c+1,float2(71.4,39.7)));
float4 n = frac(sin(seeds)*13975.243);
float vein = lerp(lerp(n.x,n.y,t.x),lerp(n.z,n.w,t.x),t.y)-.5;
return max(0.001, Tint * (1.+Strength*(vein*.25 + min(grit,0.)*visibility))
                  + Fleck * (Strength*max(grit,0.)*visibility));
"""


def create(name, folder, cls, factory):
    return TOOLS.create_asset(name, folder, cls, factory)


def parameter(master, cls, name, value):
    node = EDIT.create_material_expression(master, cls)
    node.set_editor_property('parameter_name', name)
    node.set_editor_property('default_value', value)
    return node


def shader(master, code, inputs):
    node = EDIT.create_material_expression(master, u.MaterialExpressionCustom)
    node.set_editor_property('output_type', u.CustomMaterialOutputType.CMOT_FLOAT3)
    pins = []
    for name, _, _ in inputs:
        pin = u.CustomInput()
        pin.set_editor_property('input_name', name)
        pins.append(pin)
    node.set_editor_property('inputs', pins)
    node.set_editor_property('code', code)
    for name, source, output in inputs:
        assert EDIT.connect_material_expressions(source, output, node, name)
    return node


def save_material(mat):
    EDIT.layout_material_expressions(mat)
    EDIT.recompile_material(mat)
    assert ASSETS.save_loaded_asset(mat, False)


def finishes():
    master_path = MATERIALS + '/M_HelixSurface'
    if ASSETS.does_asset_exist(master_path):
        master = u.load_asset(master_path)
    else:
        master = create('M_HelixSurface', MATERIALS, u.Material, u.MaterialFactoryNew())
        uv = EDIT.create_material_expression(master, u.MaterialExpressionTextureCoordinate)
        vec, scalar = u.MaterialExpressionVectorParameter, u.MaterialExpressionScalarParameter
        tint = parameter(master, vec, 'Tint', u.LinearColor(.42,.38,.33,1))
        fleck = parameter(master, vec, 'FleckColor', u.LinearColor(.69,.64,.52,1))
        strength = parameter(master, scalar, 'GrainStrength', .95)
        density = parameter(master, scalar, 'GrainDensity', 43.)
        roughness = parameter(master, scalar, 'Roughness', .83)
        metal = parameter(master, scalar, 'Metallic', .02)
        color = shader(master, STONE_SHADER, [('UV',uv,''),('Tint',tint,'RGB'),('Fleck',fleck,'RGB'),
                                              ('Strength',strength,''),('Density',density,'')])
        assert EDIT.connect_material_property(color, '', u.MaterialProperty.MP_BASE_COLOR)
        assert EDIT.connect_material_property(roughness, '', u.MaterialProperty.MP_ROUGHNESS)
        assert EDIT.connect_material_property(metal, '', u.MaterialProperty.MP_METALLIC)
        save_material(master)
    result = {}
    for name, slot, tint, fleck, density, strength, rough, metal in (
        ('Aggregate','aggregate_material',(.47,.446,.397),(.84,.78,.65),35.,1.16,.84,.02),
        ('Limestone','limestone_material',(.69,.629,.548),(.83,.76,.63),58.,.38,.69,.02),
        ('SmokedInset','inset_material',(.108,.105,.096),(.15,.15,.14),85.,.07,.30,.29),
        ('Bronze','bronze_material',(.31,.215,.127),(.43,.31,.15),63.,.22,.44,.62),
    ):
        name = 'MI_Helix' + name
        path = MATERIALS + '/' + name
        if ASSETS.does_asset_exist(path):
            instance = u.load_asset(path)
        else:
            instance = create(name, MATERIALS, u.MaterialInstanceConstant, u.MaterialInstanceConstantFactoryNew())
            EDIT.set_material_instance_parent(instance, master)
            for key, rgb in [('Tint',tint),('FleckColor',fleck)]:
                color = u.LinearColor(*rgb,1)
                EDIT.set_material_instance_vector_parameter_value(instance, key, color)
                assert EDIT.get_material_instance_vector_parameter_value(instance, key) == color
            for key, value in [('GrainDensity',density),('GrainStrength',strength),('Roughness',rough),('Metallic',metal)]:
                EDIT.set_material_instance_scalar_parameter_value(instance, key, value)
                assert abs(EDIT.get_material_instance_scalar_parameter_value(instance, key)-value) < .0001
            assert ASSETS.save_loaded_asset(instance, False)
        result[slot] = instance
    return result


def preview_sky():
    name = 'M_HelixPreviewSky'
    if ASSETS.does_asset_exist(MAPS + '/' + name):
        return u.load_asset(MAPS + '/' + name)
    sky = create(name, MAPS, u.Material, u.MaterialFactoryNew())
    sky.set_editor_properties(dict(shading_model=u.MaterialShadingModel.MSM_UNLIT, two_sided=True, is_sky=True))
    view = EDIT.create_material_expression(sky, u.MaterialExpressionCameraVectorWS)
    code = 'return lerp(float3(.65,.79,.95),float3(.145,.285,.55),smoothstep(-.035,.65,-View.z));'
    color = shader(sky, code, [('View',view,'')])
    assert EDIT.connect_material_property(color, '', u.MaterialProperty.MP_EMISSIVE_COLOR)
    save_material(sky)
    return sky


def preview(bp):
    path = MAPS + '/Lvl_HelixMonumentPreview'
    if ASSETS.does_asset_exist(path):
        return
    backdrop = preview_sky()
    ambient = u.load_asset('/Engine/MapTemplates/Sky/DaylightAmbientCubemap')
    world = u.EditorLoadingAndSavingUtils.new_blank_map(False)
    world.get_world_settings().set_editor_property('force_no_precomputed_lighting', True)
    actors = u.get_editor_subsystem(u.EditorActorSubsystem)
    v, rot = u.Vector, u.Rotator

    def prop(cls, label, location, rotation=None):
        actor = actors.spawn_actor_from_class(cls, location, rotation or rot())
        actor.set_actor_label(label)
        actor.set_folder_path('Preview')
        return actor

    monument = actors.spawn_actor_from_class(bp.generated_class(), v())
    monument.set_actor_label('HelixMonument')
    assert monument.generated_fragments > 1000 and not monument.generation_warning
    for name, direction, tint, intensity in (
        ('Sun',rot(pitch=-37,yaw=154), (1,.91,.79), 5.6),
        ('Fill',rot(pitch=-27,yaw=43), (.76,.86,1), 2.25),
    ):
        light = prop(u.DirectionalLight, name, v(0,0,4800), direction).light_component
        light.set_mobility(u.ComponentMobility.MOVABLE)
        light.set_intensity(intensity)
        light.set_light_color(u.LinearColor(*tint,1))
        light.set_editor_property('light_source_angle',1.4)
        if name == 'Fill':
            light.set_editor_property('cast_shadows',False)
    sky = prop(u.SkyLight, 'Daylight', v(0,0,3900)).light_component
    sky.set_mobility(u.ComponentMobility.MOVABLE)
    sky.set_editor_property('source_type',u.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
    sky.set_cubemap(ambient)
    sky.set_intensity(1.25)
    sky.set_editor_property('cast_shadows',False)

    ground = prop(u.StaticMeshActor, 'Ground', v(0,0,-25))
    ground.static_mesh_component.set_static_mesh(u.load_asset('/Engine/BasicShapes/Cube'))
    ground.static_mesh_component.set_material(0,u.load_asset('/Game/Structures/Destructible/SpiralTower/Materials/MI_SpiralTowerStage'))
    ground.set_actor_scale3d(v(1500,1500,.5))
    dome = prop(u.StaticMeshActor, 'Sky', v())
    dome.static_mesh_component.set_static_mesh(u.load_asset('/Engine/BasicShapes/Sphere'))
    dome.static_mesh_component.set_material(0,backdrop)
    dome.static_mesh_component.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
    dome.static_mesh_component.set_editor_property('cast_shadow',False)
    dome.set_actor_scale3d(v(4900,4900,4900))

    haze = prop(u.ExponentialHeightFog, 'Horizon Haze', v()).get_component_by_class(u.ExponentialHeightFogComponent)
    haze.set_editor_properties(dict(fog_density=.018,fog_height_falloff=.30,start_distance=4500.,fog_max_opacity=.82,
                                    fog_inscattering_luminance=u.LinearColor(.49,.61,.75,1)))
    exposure = prop(u.PostProcessVolume, 'Daylight Exposure', v())
    exposure.set_editor_property('unbound',True)
    settings = exposure.get_editor_property('settings')
    for key, val in dict(override_auto_exposure_method=True,auto_exposure_method=u.AutoExposureMethod.AEM_MANUAL,
                         override_auto_exposure_apply_physical_camera_exposure=True,auto_exposure_apply_physical_camera_exposure=False,
                         override_auto_exposure_bias=True,auto_exposure_bias=.52,override_motion_blur_amount=True,motion_blur_amount=0.,
                         ambient_cubemap=ambient,override_ambient_cubemap_intensity=True,ambient_cubemap_intensity=.42,
                         override_vignette_intensity=True,vignette_intensity=.12,
                         override_bloom_intensity=True,bloom_intensity=.08).items():
        settings.set_editor_property(key,val)
    exposure.set_editor_property('settings',settings)

    position, focus = v(500,-6500,620), v(0,0,1435)
    angle = u.MathLibrary.find_look_at_rotation(position,focus)
    camera = prop(u.CameraActor, 'Reference View', position, angle)
    camera.camera_component.set_field_of_view(49.)
    start = v(210,-3300,120)
    prop(u.PlayerStart, 'Player Start', start, u.MathLibrary.find_look_at_rotation(start,v(0,0,760)))
    editor_eye = v(330,-4100,475)
    u.get_editor_subsystem(u.UnrealEditorSubsystem).set_level_viewport_camera_info(
        editor_eye,u.MathLibrary.find_look_at_rotation(editor_eye,v(0,0,1330)))
    actors.set_selected_level_actors([])
    assert u.EditorLoadingAndSavingUtils.save_map(world,path)
    u.log('HELIX_PREVIEW_SAVED {} fragments, {} cm'.format(monument.generated_fragments,monument.generated_height))


def main():
    for path in (MATERIALS, MAPS):
        if not ASSETS.does_directory_exist(path):
            assert ASSETS.make_directory(path)
    palette = finishes()
    name = 'BP_ChunkedDestructibleHelixMonument'
    if ASSETS.does_asset_exist(ROOT + '/' + name):
        bp = u.load_asset(ROOT + '/' + name)
    else:
        factory = u.BlueprintFactory()
        factory.set_editor_property('parent_class', u.ChunkedDestructibleHelixMonument)
        bp = create(name, ROOT, u.Blueprint, factory)
        u.get_default_object(bp.generated_class()).set_editor_properties(palette)
        u.BlueprintEditorLibrary.compile_blueprint(bp)
        assert ASSETS.save_loaded_asset(bp,False)
    preview(bp)
    u.log('HELIX_ASSETS_READY ' + bp.get_path_name())


if __name__ == '__main__':
    main()
