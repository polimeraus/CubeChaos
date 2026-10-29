"""Create durable lobby materials, Blueprint and a separate reference preview map.

Run with -run=pythonscript -script=... -AllowCommandletRendering -RenderOffscreen
before compilation to create materials, then again to create the Blueprint and map.
Keep the real RHI enabled so the saved material shaders are compiled too.
"""
import json
from pathlib import Path
import unreal

ROOT = '/Game/GameplayActors/LobbyPlatform'
assets = unreal.AssetToolsHelpers.get_asset_tools()
edit = unreal.MaterialEditingLibrary


def mask_code():
    code = (Path(__file__).parent / 'LobbySurfaceMask.ush').read_text()
    for token, prefix, count in [('PAD_ARRAY','Pad',16), ('SEGMENT_ARRAY','Segment',32),
                                 ('WIDTH_ARRAY','PathWidth',32), ('INSET_ARRAY','PathInset',32), ('CORE_ARRAY','Core',32)]:
        code = code.replace(token, ','.join(prefix+str(i) for i in range(count)))
    return code


def upgrade_surface(m):
    nodes = edit.get_material_expressions(m)
    custom = next(n for n in nodes if isinstance(n, unreal.MaterialExpressionCustom))
    inputs = list(custom.get_editor_property('inputs'))
    known = {str(i.get_editor_property('input_name')) for i in inputs}
    pending = []
    specs = [('OutlineData', (1800,1400,300,0)), ('CoreCount', 4.)]
    specs += [('Core'+str(i), (0,0,0,0)) for i in range(32)]
    specs += [('PathInset'+str(i), -1.) for i in range(32)]
    for name, value in specs:
        if name in known:
            continue
        vector = isinstance(value, tuple)
        n = edit.create_material_expression(m, unreal.MaterialExpressionVectorParameter if vector else unreal.MaterialExpressionScalarParameter)
        n.set_editor_property('parameter_name', name)
        n.set_editor_property('default_value', unreal.LinearColor(*value) if vector else value)
        item = unreal.CustomInput()
        item.set_editor_property('input_name', name)
        inputs.append(item)
        pending.append((n, 'RGBA' if vector else '', name))
    custom.set_editor_property('inputs', inputs)
    for node, output, name in pending:
        assert edit.connect_material_expressions(node, output, custom, name)
    custom.set_editor_property('code', mask_code())
    edit.recompile_material(m)
    assert unreal.EditorAssetLibrary.save_loaded_asset(m, False)


def upgrade_trim(m):
    nodes = edit.get_material_expressions(m)
    params = {str(n.get_editor_property('parameter_name')): n for n in nodes
              if isinstance(n, (unreal.MaterialExpressionScalarParameter, unreal.MaterialExpressionVectorParameter))}
    if 'InteriorEnabled' not in params:
        def scalar(name, value):
            n = edit.create_material_expression(m, unreal.MaterialExpressionScalarParameter)
            n.set_editor_property('parameter_name', name)
            n.set_editor_property('default_value', value)
            return n

        def op(kind, a, b, out_a='', out_b=''):
            n = edit.create_material_expression(m, kind)
            assert edit.connect_material_expressions(a, out_a, n, 'A')
            assert edit.connect_material_expressions(b, out_b, n, 'B')
            return n

        uv = edit.create_material_expression(m, unreal.MaterialExpressionTextureCoordinate)
        connections = [(uv, 'UV'), (scalar('LayerCenter', .72), 'Center'),
                       (scalar('LayerHalfWidth', .09), 'HalfWidth'), (scalar('InteriorEnabled', 0), 'Enabled')]
        mask = edit.create_material_expression(m, unreal.MaterialExpressionCustom)
        inputs = []
        for _, name in connections:
            item = unreal.CustomInput()
            item.set_editor_property('input_name', name)
            inputs.append(item)
        mask.set_editor_property('inputs', inputs)
        mask.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT1)
        mask.set_editor_property('code', 'float d=abs(UV.y-Center)-HalfWidth; float aa=max(fwidth(d),0.0001); return Enabled*(1-smoothstep(-aa,aa,d));')
        for node, name in connections:
            assert edit.connect_material_expressions(node, '', mask, name)
        neon = edit.create_material_expression(m, unreal.MaterialExpressionVectorParameter)
        neon.set_editor_property('parameter_name', 'NeonColor')
        neon.set_editor_property('default_value', unreal.LinearColor(0,.75,1,1))
        base = op(unreal.MaterialExpressionLinearInterpolate, params['Color'], neon, 'RGB', 'RGB')
        assert edit.connect_material_expressions(mask, '', base, 'Alpha')
        ordinary_emission = next(n for n in nodes if isinstance(n, unreal.MaterialExpressionMultiply))
        glow = op(unreal.MaterialExpressionMultiply, neon, scalar('NeonIntensity', 8), 'RGB')
        glow = op(unreal.MaterialExpressionMultiply, glow, mask)
        emission = op(unreal.MaterialExpressionAdd, ordinary_emission, glow)
        assert edit.connect_material_property(base, '', unreal.MaterialProperty.MP_BASE_COLOR)
        assert edit.connect_material_property(emission, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        edit.layout_material_expressions(m)
    errors = edit.recompile_material(m)
    assert not errors, str(errors)
    assert unreal.EditorAssetLibrary.save_loaded_asset(m, False)


def material(name, surface=False):
    path = ROOT + '/Materials/' + name
    # CDO references root the existing graph. Update it in place, never delete it.
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        m = unreal.load_asset(path)
        if surface:
            upgrade_surface(m)
        return m
    m = assets.create_asset(name, ROOT + '/Materials', unreal.Material, unreal.MaterialFactoryNew())
    assert m
    edit.delete_all_material_expressions(m)
    m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_OPAQUE)
    m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    m.set_editor_property('two_sided', False)

    def scalar(name, value):
        n = edit.create_material_expression(m, unreal.MaterialExpressionScalarParameter)
        n.set_editor_property('parameter_name', name)
        n.set_editor_property('default_value', value)
        return n

    def vector(name, value):
        n = edit.create_material_expression(m, unreal.MaterialExpressionVectorParameter)
        n.set_editor_property('parameter_name', name)
        n.set_editor_property('default_value', unreal.LinearColor(*value))
        return n

    def op(kind, a, b, out_a='', out_b=''):
        n = edit.create_material_expression(m, kind)
        assert edit.connect_material_expressions(a, out_a, n, 'A')
        assert edit.connect_material_expressions(b, out_b, n, 'B')
        return n

    color = vector('Color', (.105, .003, .46, 1))
    emission = scalar('Emission', .18)
    roughness = scalar('Roughness', .19)
    metallic = scalar('Metallic', .32)
    base = color
    emissive = op(unreal.MaterialExpressionMultiply, color, emission, 'RGB')
    if surface:
        inputs, connections = [], []

        def inp(name, node, output=''):
            item = unreal.CustomInput()
            item.set_editor_property('input_name', name)
            inputs.append(item)
            connections.append((node, output, name))

        uv = edit.create_material_expression(m, unreal.MaterialExpressionTextureCoordinate)
        inp('UV', uv)
        inp('Dimensions', vector('Dimensions', (3600, 2800, 0, 0)), 'RGBA')
        inp('PadCount', scalar('PadCount', 0))
        inp('SegmentCount', scalar('SegmentCount', 0))
        inp('ShowLayout', scalar('ShowLayout', 1))
        for i in range(16):
            inp(f'Pad{i}', vector(f'Pad{i}', (0, 0, 0, 0)), 'RGBA')
        for i in range(32):
            inp(f'Segment{i}', vector(f'Segment{i}', (0, 0, 0, 0)), 'RGBA')
            inp(f'PathWidth{i}', scalar(f'PathWidth{i}', 0))
        inp('OutlineData', vector('OutlineData', (1800,1400,300,0)), 'RGBA')
        inp('CoreCount', scalar('CoreCount', 4))
        for i in range(32):
            inp(f'Core{i}', vector(f'Core{i}', (0,0,0,0)), 'RGBA')
            inp(f'PathInset{i}', scalar(f'PathInset{i}', -1))
        code = mask_code()
        mask = edit.create_material_expression(m, unreal.MaterialExpressionCustom)
        mask.set_editor_property('description', 'Analytic union of named circular pads and rounded paths')
        mask.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT1)
        mask.set_editor_property('inputs', inputs)
        mask.set_editor_property('code', code)
        for node, output, name in connections:
            assert edit.connect_material_expressions(node, output, mask, name), name
        yellow = vector('YellowColor', (1, .87, .006, 1))
        yellow_emission = op(unreal.MaterialExpressionMultiply, yellow, scalar('YellowEmission', .55), 'RGB')

        def blend(a, b, a_output='', b_output=''):
            n = op(unreal.MaterialExpressionLinearInterpolate, a, b, a_output, b_output)
            assert edit.connect_material_expressions(mask, '', n, 'Alpha')
            return n

        base = blend(color, yellow, 'RGB', 'RGB')
        emissive = blend(emissive, yellow_emission)
        roughness = blend(roughness, scalar('PaintRoughness', .26))
        metallic = blend(metallic, scalar('PaintMetallic', .05))
    assert edit.connect_material_property(base, '' if surface else 'RGB', unreal.MaterialProperty.MP_BASE_COLOR)
    assert edit.connect_material_property(emissive, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    assert edit.connect_material_property(roughness, '', unreal.MaterialProperty.MP_ROUGHNESS)
    assert edit.connect_material_property(metallic, '', unreal.MaterialProperty.MP_METALLIC)
    edit.layout_material_expressions(m)
    edit.recompile_material(m)
    assert unreal.EditorAssetLibrary.save_loaded_asset(m, False)
    return m


surface = material('M_LobbySurface', True)
trim = material('M_LobbyTrim')
upgrade_trim(trim)
cls = getattr(unreal, 'ParametricLobbyPlatform', None)
if cls:
    bp_path = ROOT + '/BP_ParametricLobbyPlatform'
    bp = unreal.load_asset(bp_path) if unreal.EditorAssetLibrary.does_asset_exist(bp_path) else None
    if not bp:
        factory = unreal.BlueprintFactory()
        factory.set_editor_property('parent_class', cls)
        bp = assets.create_asset('BP_ParametricLobbyPlatform', ROOT, unreal.Blueprint, factory)
    assert bp
    cdo = unreal.get_default_object(bp.generated_class())
    cdo.set_editor_property('surface_material', surface)
    cdo.set_editor_property('trim_material', trim)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    assert unreal.EditorAssetLibrary.save_loaded_asset(bp, False)

    map_path = ROOT + '/Maps/Lvl_LobbyPlatformPreview'
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if unreal.EditorAssetLibrary.does_asset_exist(map_path):
        world = unreal.EditorLoadingAndSavingUtils.load_map(map_path)
        lobby = next(a for a in actors.get_all_level_actors() if isinstance(a, cls))
        lobby.set_editor_property('surface_material', surface)
        lobby.set_editor_property('trim_material', trim)
        lobby.rebuild_platform()
    else:
        world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
        world.get_world_settings().set_editor_property('default_game_mode', unreal.GameModeBase)
        lobby = actors.spawn_actor_from_class(bp.generated_class(), unreal.Vector())
        lobby.set_actor_label('LobbyPlatform_Reference')
        lobby.set_folder_path('Lobby Platform')
        for rotation, power, color in [((-52, -35, 0), 3.5, (1, .91, 1)), ((-35, 150, 0), 2.0, (.55, .65, 1))]:
            light = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 2500), unreal.Rotator(*rotation))
            light.light_component.set_editor_property('intensity', power)
            light.light_component.set_light_color(unreal.LinearColor(*color, 1))
            light.set_folder_path('Preview Lighting')
        for position, color, power in [((-1300, 500, 1300), (.15, .5, 1), 700),
                                        ((1000, 800, 1400), (1, .02, .4), 900)]:
            light = actors.spawn_actor_from_class(unreal.RectLight, unreal.Vector(*position), unreal.Rotator(pitch=-90))
            light.rect_light_component.set_editor_property('intensity', power)
            light.rect_light_component.set_editor_property('attenuation_radius', 6000)
            light.rect_light_component.set_editor_property('source_width', 1100)
            light.rect_light_component.set_editor_property('source_height', 800)
            light.rect_light_component.set_light_color(unreal.LinearColor(*color, 1))
            light.set_folder_path('Preview Lighting')
        volume = actors.spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector())
        volume.set_editor_property('unbound', True)
        post = volume.get_editor_property('settings')
        for key, value in {'override_auto_exposure_method': True, 'auto_exposure_method': unreal.AutoExposureMethod.AEM_MANUAL,
            'override_auto_exposure_apply_physical_camera_exposure': True, 'auto_exposure_apply_physical_camera_exposure': False,
            'override_auto_exposure_bias': True, 'auto_exposure_bias': 0.,
            'override_bloom_intensity': True, 'bloom_intensity': .45}.items():
            post.set_editor_property(key, value)
        volume.set_editor_property('settings', post)
        volume.set_folder_path('Preview Lighting')
        # Background plane uses a separate instance, not the platform's class settings.
        backdrop_mat_path = ROOT + '/Materials/MI_PreviewBackdrop'
        backdrop_mat = unreal.load_asset(backdrop_mat_path) if unreal.EditorAssetLibrary.does_asset_exist(backdrop_mat_path) else assets.create_asset(
            'MI_PreviewBackdrop', ROOT + '/Materials', unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        edit.set_material_instance_parent(backdrop_mat, trim)
        edit.set_material_instance_vector_parameter_value(backdrop_mat, 'Color', unreal.LinearColor(.033, .0015, .10, 1))
        for name, value in [('Emission', .6), ('Roughness', .8), ('Metallic', 0)]:
            edit.set_material_instance_scalar_parameter_value(backdrop_mat, name, value)
        unreal.EditorAssetLibrary.save_loaded_asset(backdrop_mat, False)
        backdrop = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, -150))
        backdrop.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Plane'))
        backdrop.static_mesh_component.set_material(0, backdrop_mat)
        backdrop.set_actor_scale3d(unreal.Vector(1000, 1000, 1))
        backdrop.set_actor_label('Preview_Backdrop'); backdrop.set_folder_path('Preview Lighting')
        position = unreal.Vector(1500, -6500, 5200)
        rotation = unreal.MathLibrary.find_look_at_rotation(position, unreal.Vector(0, 0, -30))
        camera = actors.spawn_actor_from_class(unreal.CameraActor, position, rotation)
        camera.camera_component.set_editor_property('field_of_view', 40.)
        camera.set_editor_property('auto_activate_for_player', unreal.AutoReceiveInput.PLAYER0)
        camera.set_actor_label('Preview_Camera')
        actors.spawn_actor_from_class(unreal.PlayerStart, position, rotation)
    for light in actors.get_all_level_actors():
        if isinstance(light, unreal.RectLight):
            light.rect_light_component.set_editor_property('intensity', 700 if light.get_actor_location().x < 0 else 900)
    position = unreal.Vector(1500, -6500, 5200)
    for camera in actors.get_all_level_actors():
        if isinstance(camera, unreal.CameraActor):
            camera.set_actor_location(position, False, False)
            camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(position, unreal.Vector(0, 0, -30)), False)
    unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(
        position, unreal.MathLibrary.find_look_at_rotation(position, unreal.Vector(0, 0, -30)))
    actors.set_selected_level_actors([lobby])
    assert not lobby.layout_warning, lobby.layout_warning
    assert unreal.EditorLoadingAndSavingUtils.save_map(world, map_path)
    out = Path(unreal.Paths.project_saved_dir()) / 'LobbyPlatform'
    out.mkdir(parents=True, exist_ok=True)
    (out / 'Setup.json').write_text(json.dumps({'blueprint': bp_path, 'map': map_path, 'pads': [str(n) for n in lobby.get_pad_ids()]}, indent=2))
    unreal.log('LOBBY_PLATFORM_SETUP_OK')
else:
    unreal.log('LOBBY_MATERIALS_READY; compile native class then run this script again')
