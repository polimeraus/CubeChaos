"""Update only the saved bomb shader; leave other weapon materials unchanged."""
from pathlib import Path
import shutil, unreal

root=Path(unreal.Paths.project_dir()).resolve()
backup=root/'Saved/BombPerformance/Before'
backup.mkdir(parents=True,exist_ok=True)
asset=root/'Content/Weapons/FuseBomb/Materials/M_BombEffect.uasset'
if not (backup/asset.name).exists():shutil.copy2(asset,backup/asset.name)
fx=unreal.load_asset('/Game/Weapons/FuseBomb/Materials/M_BombEffect')
shader=next(n for n in unreal.MaterialEditingLibrary.get_material_expressions(fx) if isinstance(n,unreal.MaterialExpressionCustom))
shader.set_editor_property('code',Path(__file__).with_name('BombEffect.ush').read_text())
unreal.MaterialEditingLibrary.recompile_material(fx)
assert unreal.EditorAssetLibrary.save_loaded_asset(fx,False)
unreal.log('SPHERICAL_BOMB_MATERIAL_SAVED')
