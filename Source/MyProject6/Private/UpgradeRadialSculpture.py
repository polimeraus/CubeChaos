"""Snapshot then persist hollow sphere walls without changing placement or other settings."""
import json
import shutil
from datetime import datetime
from pathlib import Path
import unreal

folder = '/Game/Structures/Destructible/RadialSculpture'
bp_path = folder + '/BP_DestructibleRadialSculpture'
maps = [folder + '/Maps/Lvl_RadialSculpturePreview', '/Game/FirstPerson/Lvl_FirstPerson']
out = Path(unreal.Paths.project_saved_dir()).resolve() / 'RadialSculpture'
out.mkdir(parents=True, exist_ok=True)
content = Path(unreal.Paths.project_content_dir()).resolve()
snapshot = '-SnapshotRadialSculpture' in unreal.SystemLibrary.get_command_line()
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
keys = ('fin_count', 'outer_radius', 'fin_height', 'fin_thickness', 'sphere_radius', 'neck_radius',
        'curve_exponent', 'include_base', 'base_margin', 'base_thickness', 'piece_health')
def record(a):
    t = a.get_actor_transform()
    return {'label': a.get_actor_label(), 'name': a.get_name(), 'folder': str(a.get_folder_path()),
            'location': [t.translation.x,t.translation.y,t.translation.z],
            'rotation': [t.rotation.x,t.rotation.y,t.rotation.z,t.rotation.w],
            'scale': [t.scale3d.x,t.scale3d.y,t.scale3d.z],
            'shape': {k:a.get_editor_property(k) for k in keys},
            'material': a.get_editor_property('sculpture_material').get_path_name()}
if snapshot:
    backup = out / ('BeforeHollowSphere_' + datetime.now().strftime('%Y%m%d_%H%M%S'))
    paths = [content/'Structures/Destructible/RadialSculpture/BP_DestructibleRadialSculpture.uasset',
             content/'Structures/Destructible/RadialSculpture/Maps/Lvl_RadialSculpturePreview.umap',
             content/'FirstPerson/Lvl_FirstPerson.umap']
    paths += list((content/'__ExternalActors__/FirstPerson/Lvl_FirstPerson').rglob('*.uasset'))
    for src in paths:
        dst = backup / src.relative_to(content)
        dst.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(src,dst)
    report = {'backup': str(backup), 'maps': {}}
    for path in maps:
        assert unreal.EditorLoadingAndSavingUtils.load_map(path)
        report['maps'][path] = [record(a) for a in actors.get_all_level_actors() if isinstance(a,unreal.DestructibleRadialSculpture)]
    assert sum(len(v) for v in report['maps'].values()) >= 2
    (out/'BeforeHollowSphere.json').write_text(json.dumps(report,indent=2))
    unreal.log('RADIAL_SMALL_CELLS_SNAPSHOT_OK ' + json.dumps(report))
else:
    before = json.loads((out/'BeforeHollowSphere.json').read_text())
    bp = unreal.load_asset(bp_path)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    defaults = unreal.get_default_object(bp.generated_class())
    defaults.modify()
    defaults.set_editor_property('sphere_wall_thickness',15.)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    assert unreal.EditorAssetLibrary.save_loaded_asset(bp,False)
    report = {'maps': {}, 'blueprint': bp_path}
    for path in maps:
        world = unreal.EditorLoadingAndSavingUtils.load_map(path)
        assert world
        found = [a for a in actors.get_all_level_actors() if isinstance(a,unreal.DestructibleRadialSculpture)]
        assert len(found) == len(before['maps'][path]), path
        report['maps'][path] = []
        for a in found:
            expected = next(r for r in before['maps'][path] if r['name']==a.get_name())
            assert record(a) == expected, ('placement or presentation changed',path,record(a),expected)
            resolution = a.piece_size
            a.modify()
            a.set_editor_property('sphere_wall_thickness',15.)
            a.rebuild_sculpture()
            assert not a.generation_warning and a.generated_piece_count>0 and a.piece_size==resolution
            assert record(a) == expected
            assert all(not c.is_simulating_physics() for c in a.get_components_by_class(unreal.DynamicMeshComponent))
            report['maps'][path].append({'label':a.get_actor_label(),'pieces':a.generated_piece_count,
                                        'name':a.get_name(),'piece_size_cm':a.piece_size,
                                        'wall_thickness_cm':a.sphere_wall_thickness,'inner_radius_cm':a.get_sphere_inner_radius(),
                                        'chunks':len(a.get_components_by_class(unreal.DynamicMeshComponent))})
        assert unreal.EditorLoadingAndSavingUtils.save_map(world,path)
    (out/'HollowSphereUpgrade.json').write_text(json.dumps(report,indent=2))
    unreal.log('RADIAL_HOLLOW_SPHERE_UPGRADE_OK ' + json.dumps(report))
