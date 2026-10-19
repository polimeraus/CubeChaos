"""Shared idempotent procedural sky material builder, used by setup and moon upgrades."""
from pathlib import Path
import unreal


def import_moon_texture():
    """Keep the real lunar imagery bundled and cookable, with normal streaming mips."""
    root = '/Game/Environment/RetroTwilight'
    path = root + '/T_Moon_NASA_LRO_8K'
    texture = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
    if texture is None:
        source = Path(unreal.Paths.project_dir()) / 'SourceArt/RetroTwilight/T_Moon_NASA_LRO_8K.tif'
        assert source.exists(), source
        task = unreal.AssetImportTask()
        task.set_editor_property('filename', str(source.resolve()))
        task.set_editor_property('destination_path', root)
        task.set_editor_property('destination_name', 'T_Moon_NASA_LRO_8K')
        task.set_editor_property('automated', True)
        task.set_editor_property('save', True)
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        texture = unreal.load_asset(path)
    assert isinstance(texture, unreal.Texture2D)
    texture.set_editor_property('srgb', True)
    texture.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_DEFAULT)
    texture.set_editor_property('lod_group', unreal.TextureGroup.TEXTUREGROUP_SKYBOX)
    texture.set_editor_property('max_texture_size', 8192)
    texture.set_editor_property('lod_bias', 0)
    texture.set_editor_property('mip_gen_settings', unreal.TextureMipGenSettings.TMGS_FROM_TEXTURE_GROUP)
    texture.set_editor_property('address_x', unreal.TextureAddress.TA_WRAP)
    texture.set_editor_property('address_y', unreal.TextureAddress.TA_CLAMP)
    unreal.EditorAssetLibrary.set_metadata_tag(texture, 'Source', 'https://svs.gsfc.nasa.gov/4720/')
    unreal.EditorAssetLibrary.set_metadata_tag(texture, 'Credit', "NASA's Scientific Visualization Studio / LROC (NASA/GSFC/ASU)")
    assert unreal.EditorAssetLibrary.save_loaded_asset(texture, False)
    return texture


def build_material():
    root = '/Game/Environment/RetroTwilight'
    texture_path = root + '/T_Moon_NASA_LRO_8K'
    texture = unreal.load_asset(texture_path) if unreal.EditorAssetLibrary.does_asset_exist(texture_path) else import_moon_texture()
    path = root + '/M_RetroTwilightSky'
    edit = unreal.MaterialEditingLibrary
    material = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else (
        unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            'M_RetroTwilightSky', root, unreal.Material, unreal.MaterialFactoryNew()))
    assert material
    material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property('two_sided', True)
    material.set_editor_property('is_sky', True)
    existing = list(edit.get_material_expressions(material))

    def expression(cls, name=None):
        for node in existing:
            if isinstance(node, cls) and (name is None or str(node.get_editor_property('parameter_name')) == name):
                existing.remove(node)
                return node
        return edit.create_material_expression(material, cls)

    nodes = {'ViewToCamera': (expression(unreal.MaterialExpressionCameraVectorWS), ''),
             'SkyTime': (expression(unreal.MaterialExpressionTime), '')}
    for key, value in {
        'DominantColor': (0.32, 0.22, 0.65), 'MoonDirection': (-0.742, 0.52, 0.407),
        'MoonColor': (1.0, 0.88, 0.60),
        'MoonGlowColor': (0.36, 0.48, 1.0),
        'MoonRetroTopColor': (1.0, 0.97, 0.64), 'MoonRetroBottomColor': (1.0, 0.16, 0.58),
        'AuroraLowerColor': (0.07, 1.0, 0.49), 'AuroraUpperColor': (0.22, 0.38, 1.0),
        'AuroraAccentColor': (0.75, 0.14, 0.82)
    }.items():
        node = expression(unreal.MaterialExpressionVectorParameter, key)
        node.set_editor_property('parameter_name', key)
        node.set_editor_property('default_value', unreal.LinearColor(*value, 1))
        node.set_editor_property('group', 'Aurora' if key.startswith('Aurora') else 'Moon' if key.startswith('Moon') else 'Sky')
        nodes[key] = (node, 'RGB')
    for key, value in {
        'SkyBrightness': 0.8, 'MoonVisibility': 1.0, 'MoonRadius': 0.08726646,
        'StarBrightness': 0.35, 'MoonPattern': 5.0, 'MoonPhase': 0.0,
        'MoonPhaseRotation': 0.0, 'MoonBrightness': 1.0,
        'MoonEmissiveIntensity': 1.35, 'MoonTextureRotation': 0.0, 'MoonRetroBandGap': 1.0,
        'MoonSurfaceContrast': 1.15,
        'MoonEarthshine': 0.025, 'MoonGlowIntensity': 0.65, 'MoonGlowWidth': 2.4, 'MoonGlowSoftness': 1.4,
        'AuroraVisibility': 1.0, 'AuroraIntensity': 0.85, 'AuroraDensity': 0.8,
        'AuroraWidth': 0.20943951, 'AuroraElevation': 0.59341195, 'AuroraAzimuth': 2.53072742,
        'AuroraCoverage': 3.83972435, 'AuroraFlowSpeed': 0.12, 'AuroraTurbulence': 0.65,
        'AuroraDetailScale': 1.0, 'AuroraSeed': 7.0
    }.items():
        node = expression(unreal.MaterialExpressionScalarParameter, key)
        node.set_editor_property('parameter_name', key)
        node.set_editor_property('default_value', value)
        node.set_editor_property('group', 'Aurora' if key.startswith('Aurora') else 'Moon' if key.startswith('Moon') else 'Sky')
        nodes[key] = (node, '')
    photo = expression(unreal.MaterialExpressionTextureObjectParameter, 'MoonTexture')
    photo.set_editor_property('parameter_name', 'MoonTexture')
    photo.set_editor_property('texture', texture)
    photo.set_editor_property('group', 'Moon')
    nodes['MoonTexture'] = (photo, '')
    custom = expression(unreal.MaterialExpressionCustom)
    custom.set_editor_property('description', 'Twilight: flowing aurora curtains, lunar geology and soft neon scattering')
    custom.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    custom.set_editor_property('code', (Path(__file__).parent / 'RetroTwilightSky.ush').read_text())
    inputs = []
    for key in nodes:
        item = unreal.CustomInput()
        item.set_editor_property('input_name', key)
        inputs.append(item)
    custom.set_editor_property('inputs', inputs)
    for key, (node, pin) in nodes.items():
        assert edit.connect_material_expressions(node, pin, custom, key)
    assert edit.connect_material_property(custom, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    edit.layout_material_expressions(material)
    edit.recompile_material(material)
    assert unreal.EditorAssetLibrary.save_loaded_asset(material, False)
    return material


if __name__ == '__main__':
    build_material()
