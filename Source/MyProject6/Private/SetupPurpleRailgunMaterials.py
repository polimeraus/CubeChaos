"""Material slots shared by the held Comet mesh and its charge animation."""
from pathlib import Path
import runpy
import unreal

folder = '/Game/Weapons/PurpleRailgun/Materials'
parent = unreal.load_asset('/Game/Materials/Weapons/M_RailMetal')
edit = unreal.MaterialEditingLibrary
lib = unreal.EditorAssetLibrary
assets = unreal.AssetToolsHelpers.get_asset_tools()

def make_finishes(preserve_existing=False):
    instruments = runpy.run_path(str(Path(__file__).with_name('SetupRailgunInstruments.py')))
    finishes = {'RailGauge': instruments['make_instrument'](True)}
    for slot, name, tint, metallic, roughness, fill in [
        ('PurpleBody', 'Cherry', (.64, .015, .033), .38, .27, .28),
        ('CometIvory', 'Ivory', (.95, .78, .50), .14, .31, .32),
        ('CometBrass', 'Brass', (.76, .44, .16), .70, .27, .22),
        ('CometOxblood', 'Oxblood', (.090, .019, .030), .23, .37, .32),
        ('CometGrip', 'Grip', (.052, .032, .039), .12, .52, .22),
        ('PurpleEnergy', 'Ruby', (1., .015, .045), .20, .23, 2.8),
        ('CometDial', 'Dial', (.026, .052, .061), .23, .28, .25),
    ]:
        name = 'MI_Comet' + name
        path = folder + '/' + name
        if lib.does_asset_exist(path):
            material = unreal.load_asset(path)
            if preserve_existing:
                finishes[slot] = material
                continue
        else:
            material = assets.create_asset(name, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        edit.set_material_instance_parent(material, parent)
        edit.set_material_instance_vector_parameter_value(material, 'Tint', unreal.LinearColor(*tint, 1))
        for key, value in [('Metallic', metallic), ('Roughness', roughness), ('Emission', fill)]:
            edit.set_material_instance_scalar_parameter_value(material, key, value)
        assert edit.get_material_instance_vector_parameter_value(material, 'Tint') == unreal.LinearColor(*tint, 1)
        assert lib.save_loaded_asset(material, False)
        finishes[slot] = material
    return finishes

if __name__ == '__main__':
    make_finishes()
