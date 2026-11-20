"""Reimport the authored meshes and update their presentation in place in Unreal."""
from pathlib import Path
import runpy
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
here = Path(__file__).resolve().parent
edit = unreal.MaterialEditingLibrary
library = unreal.EditorAssetLibrary


def import_mesh(source, folder, name):
    mesh = unreal.load_asset(folder + '/' + name)
    slots = {str(s.material_slot_name): s.material_interface for s in mesh.static_materials}
    assert slots and all(slots.values()), name
    task = unreal.AssetImportTask()
    task.filename = str(root / 'ContentSource' / source / (name + '.fbx'))
    task.destination_path = folder
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = False
    options = unreal.FbxImportUI()
    options.import_materials = False
    options.import_textures = False
    options.automated_import_should_detect_type = False
    options.mesh_type_to_import = unreal.FBXImportType.FBXIT_STATIC_MESH
    data = options.static_mesh_import_data
    data.combine_meshes = True
    data.convert_scene = True
    data.convert_scene_unit = True
    data.auto_generate_collision = source == 'BallerinaRide'
    data.normal_import_method = unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS
    task.options = options
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    mesh = unreal.load_asset(folder + '/' + name)
    assert mesh.get_path_name() in task.imported_object_paths, name
    assert {str(s.material_slot_name) for s in mesh.static_materials} == slots.keys(), name
    for index, slot in enumerate(mesh.static_materials):
        mesh.set_material(index, slots[str(slot.material_slot_name)])
    assert library.save_loaded_asset(mesh, False)


def rifle_finish():
    mat = unreal.load_asset('/Game/Weapons/Rifle/Materials/M_Rifle')
    nodes = edit.get_material_expressions(mat)
    name = 'Receiver enamel bands'
    shader = next((n for n in nodes if isinstance(n, unreal.MaterialExpressionCustom)
                   and n.get_editor_property('description') == name), None)
    if shader is None:
        base = unreal.MaterialProperty.MP_BASE_COLOR
        pigment = edit.get_material_property_input_node(mat, base)
        output = edit.get_material_property_input_node_output_name(mat, base)
        assert pigment
        local = edit.create_material_expression(mat, unreal.MaterialExpressionPreSkinnedPosition)
        interp = edit.create_material_expression(mat, unreal.MaterialExpressionVertexInterpolator)
        assert edit.connect_material_expressions(local, '', interp, '')
        shader = edit.create_material_expression(mat, unreal.MaterialExpressionCustom)
        shader.set_editor_property('description', name)
        shader.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
        pins = []
        for label in ('Surface', 'Local'):
            pin = unreal.CustomInput()
            pin.set_editor_property('input_name', label)
            pins.append(pin)
        shader.set_editor_property('inputs', pins)
        assert edit.connect_material_expressions(pigment, output, shader, 'Surface')
        assert edit.connect_material_expressions(interp, '', shader, 'Local')
        assert edit.connect_material_property(shader, '', base)
        edit.layout_material_expressions(mat)
    shader.set_editor_property('code', (here / 'RifleEnamel.ush').read_text())
    edit.recompile_material(mat)
    assert library.save_loaded_asset(mat, False)


def refresh_effects():
    runpy.run_path(str(here / 'SetupRailgunTrail.py'))
    for path, source in [('Railgun/Materials/M_RailSpiral','RailgunSpiral.ush'),
                         ('FuseBomb/Materials/M_BombEffect','BombEffect.ush')]:
        mat = unreal.load_asset('/Game/Weapons/' + path)
        shader, = [n for n in edit.get_material_expressions(mat) if isinstance(n, unreal.MaterialExpressionCustom)]
        shader.set_editor_property('code', (here / source).read_text())
        edit.recompile_material(mat)
        assert library.save_loaded_asset(mat, False)


if __name__ == '__main__':
    for source, folder, mesh in [
        ('BallerinaRide', 'GameplayActors/BallerinaRide', 'SM_BallerinaFigure'),
        ('Railgun', 'Weapons/Railgun', 'SM_QuakeRailgun'),
        ('BFGFusion', 'Weapons/BFG/Fusion', 'SM_BFGFusion'),
        ('FuseBomb', 'Weapons/FuseBomb', 'SM_FuseBomb'),
        ('ElectricTether', 'Weapons/ElectricTether', 'SM_ArcCage'),
        ('SleepSignal', 'Weapons/SleepSignal', 'SM_SleepSignalDevice'),
    ]:
        import_mesh(source, '/Game/' + folder + '/Meshes', mesh)
    refresh_effects()
    for slot, color, metal, rough in [
        ('CrimsonVelvet', (.41,.012,.043), .035, .54),
        ('Porcelain', (.84,.58,.44), .025, .44),
        ('MidnightHair', (.025,.014,.022), .09, .34),
    ]:
        mi = unreal.load_asset('/Game/GameplayActors/BallerinaRide/Materials/MI_' + slot)
        edit.set_material_instance_vector_parameter_value(mi, 'Tint', unreal.LinearColor(*color, 1))
        edit.set_material_instance_scalar_parameter_value(mi, 'Metallic', metal)
        edit.set_material_instance_scalar_parameter_value(mi, 'Roughness', rough)
        assert library.save_loaded_asset(mi, False)
    rifle_finish()
    runpy.run_path(str(here / 'SetupPurpleRailgunMaterials.py'), run_name='__main__')
    unreal.log('EQUIPMENT_ART_SAVED')
