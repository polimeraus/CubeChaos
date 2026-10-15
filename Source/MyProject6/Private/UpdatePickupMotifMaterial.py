"""Update the owned icon shader in-place, preserving material parameters and Blueprint defaults."""
from datetime import datetime
from pathlib import Path
import shutil
import unreal

material = unreal.load_asset('/Game/GameplayActors/Pickups/Shared/Materials/M_PickupSurface')
assert material, 'Run SetupParametricPickup.py first'
edit = unreal.MaterialEditingLibrary
shaders = [node for node in edit.get_material_expressions(material)
           if isinstance(node, unreal.MaterialExpressionCustom)
           and node.get_editor_property('description') == 'Procedural pickup icons + timed glow']
assert len(shaders) == 1, 'Could not identify the pickup icon shader unambiguously'
asset_file = Path(unreal.Paths.project_content_dir()) / 'GameplayActors/Pickups/Shared/Materials/M_PickupSurface.uasset'
backup = Path(unreal.Paths.project_saved_dir()) / 'Pickups/Backups' / datetime.now().strftime('%Y%m%d_%H%M%S')
backup.mkdir(parents=True, exist_ok=True)
shutil.copy2(asset_file, backup / asset_file.name)
shaders[0].set_editor_property('code', Path(__file__).with_name('PickupSurfaceMaterial.ush').read_text())
edit.recompile_material(material)
assert unreal.EditorAssetLibrary.save_loaded_asset(material, False)
unreal.log('PICKUP_ICON_SHADER_UPDATED icons=10')
