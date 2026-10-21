"""Create independent glow material and persist class/instance defaults, preserving placement."""
import json
import shutil
from datetime import datetime
from pathlib import Path
import unreal

folder = '/Game/Structures/Destructible/RadialSculpture'
out = Path(unreal.Paths.project_saved_dir()).resolve() / 'RadialSculpture'
out.mkdir(parents=True, exist_ok=True)
content = Path(unreal.Paths.project_content_dir()).resolve()
backup = out / ('BeforeGlow_' + datetime.now().strftime('%Y%m%d_%H%M%S'))
paths = list((content / 'Structures/Destructible/RadialSculpture').rglob('*.uasset'))
paths += list((content / 'Structures/Destructible/RadialSculpture').rglob('*.umap'))
paths += [content / 'FirstPerson/Lvl_FirstPerson.umap']
paths += list((content / '__ExternalActors__/FirstPerson/Lvl_FirstPerson').rglob('*.uasset'))
for src in paths:
    dst = backup / src.relative_to(content)
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)

name = 'M_RadialSculptureGlow'
path = folder + '/Materials/' + name
edit = unreal.MaterialEditingLibrary
library = unreal.EditorAssetLibrary
material = unreal.load_asset(path) if library.does_asset_exist(path) else None
if material is None:
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, folder + '/Materials', unreal.Material, unreal.MaterialFactoryNew())
material.modify()
# Update expressions in place so a loaded native actor can safely reference the asset.
expressions = list(edit.get_material_expressions(material))
def node(cls, parameter=None):
    existing = next((e for e in expressions if isinstance(e, cls) and
                     (parameter is None or str(e.get_editor_property('parameter_name')) == parameter)), None)
    return existing or edit.create_material_expression(material, cls)
material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_ADDITIVE)
material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
material.set_editor_property('two_sided', False)
material.set_editor_property('disable_depth_test', False)
uv = node(unreal.MaterialExpressionTextureCoordinate)
uv.set_editor_property('coordinate_index', 1)
clock = node(unreal.MaterialExpressionTime)
clock.set_editor_property('ignore_pause', False)
nodes = {'Path': uv, 'Clock': clock}
for key, value in dict(Mode=1., Duration=4., Pause=.25, FinCount=16., LegWidth=2.,
                       BandHeight=100., Height=704., Intensity=6., PreviewTime=-1.).items():
    p = node(unreal.MaterialExpressionScalarParameter, key)
    p.set_editor_property('parameter_name', key)
    p.set_editor_property('default_value', value)
    nodes[key] = p
tint = node(unreal.MaterialExpressionVectorParameter, 'Tint')
tint.set_editor_property('parameter_name', 'Tint')
tint.set_editor_property('default_value', unreal.LinearColor(.025, .55, 1., 1.))
nodes['Tint'] = tint
shader = node(unreal.MaterialExpressionCustom)
pins = []
for key in nodes:
    pin = unreal.CustomInput()
    pin.set_editor_property('input_name', key)
    pins.append(pin)
shader.set_editor_property('inputs', pins)
shader.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
shader.set_editor_property('description', 'Sequential legs or continuous bottom-to-top glow')
shader.set_editor_property('code', (Path(__file__).parent / 'RadialSculptureGlow.ush').read_text())
for key, expression in nodes.items():
    assert edit.connect_material_expressions(expression, '', shader, key), key
assert edit.connect_material_property(shader, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
opacity = node(unreal.MaterialExpressionConstant)
opacity.set_editor_property('r', 1.)
assert edit.connect_material_property(opacity, '', unreal.MaterialProperty.MP_OPACITY)
edit.layout_material_expressions(material)
edit.recompile_material(material)
assert library.save_loaded_asset(material, False)

bp_path = folder + '/BP_DestructibleRadialSculpture'
bp = unreal.load_asset(bp_path)
defaults = unreal.get_default_object(bp.generated_class())
assert isinstance(defaults, unreal.DestructibleRadialSculpture)
defaults.modify()
defaults.set_editor_property('glow_animation', unreal.RadialSculptureGlowAnimation.SEQUENTIAL_FINS)
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
assert library.save_loaded_asset(bp, False)

actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
shape_keys = ('fin_count', 'outer_radius', 'fin_height', 'fin_thickness', 'sphere_radius',
              'sphere_wall_thickness', 'neck_radius', 'curve_exponent', 'include_base',
              'base_margin', 'base_thickness', 'piece_size', 'piece_health')
def record(a):
    t = a.get_actor_transform()
    return dict(name=a.get_name(), label=a.get_actor_label(), folder=str(a.get_folder_path()),
                owning_class=a.get_class().get_path_name(),
                location=[t.translation.x,t.translation.y,t.translation.z],
                rotation=[t.rotation.x,t.rotation.y,t.rotation.z,t.rotation.w],
                scale=[t.scale3d.x,t.scale3d.y,t.scale3d.z],
                shape={k:a.get_editor_property(k) for k in shape_keys},
                material=a.sculpture_material.get_path_name())
report = dict(backup=str(backup), blueprint=bp_path, maps={})
for map_path in [folder + '/Maps/Lvl_RadialSculpturePreview', '/Game/FirstPerson/Lvl_FirstPerson']:
    world = unreal.EditorLoadingAndSavingUtils.load_map(map_path)
    assert world
    found = [a for a in actors.get_all_level_actors() if isinstance(a, unreal.DestructibleRadialSculpture)]
    assert found
    records = []
    for a in found:
        before = record(a)
        a.modify()
        a.set_editor_property('glow_animation', defaults.glow_animation)
        a.rebuild_sculpture()
        assert not a.generation_warning and a.generated_piece_count > 0
        assert record(a) == before, 'Unrelated placement or shape changed'
        records.append(before)
    report['maps'][map_path] = records
    assert unreal.EditorLoadingAndSavingUtils.save_map(world, map_path)
(out / 'GlowUpgrade.json').write_text(json.dumps(report, indent=2))
unreal.log('RADIAL_GLOW_UPGRADE_OK ' + json.dumps(report))
