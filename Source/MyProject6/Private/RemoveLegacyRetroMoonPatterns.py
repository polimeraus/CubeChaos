"""Audit before rebuilding, then migrate saved rigs and the sky material to the three remaining styles."""
import ast
import json
import shutil
import sys
from pathlib import Path
import unreal

source=Path(__file__).parent
out=Path(unreal.Paths.project_saved_dir())/'TrimMoonPatterns'
out.mkdir(parents=True,exist_ok=True)
bp_path='/Game/Environment/RetroTwilight/BP_RetroTwilightEnvironment'
map_path='/Game/FirstPerson/Lvl_FirstPerson'
obsolete={'moon_detail_strength','moon_exotic_tint','moon_mineral_color'}
tree=ast.parse((source/'UpgradeRetroTwilightMoonStyles.py').read_text())
preserve=next(ast.literal_eval(n.value) for n in tree.body if isinstance(n,ast.Assign) and
              any(isinstance(t,ast.Name) and t.id=='preserve' for t in n.targets))
keys=[k for k in preserve if k not in obsolete]+[
    'moon_texture','moon_emissive_intensity','moon_texture_rotation',
    'moon_retro_top_color','moon_retro_bottom_color','moon_retro_band_gap','sky_material']

def stable(value):
    if isinstance(value,unreal.Object):return value.get_path_name()
    return str(value).split('{',1)[-1]

def settings(actor):return {k:stable(actor.get_editor_property(k)) for k in keys}

def snapshot(actor):
    return dict(settings=settings(actor),label=actor.get_actor_label(),location=stable(actor.get_actor_location()),
                rotation=stable(actor.get_actor_rotation()),scale=stable(actor.get_actor_scale3d()),
                folder=str(actor.get_folder_path()),cls=actor.get_class().get_path_name())

def migrated(before):
    result=json.loads(json.dumps(before))
    def visit(value):
        if isinstance(value,dict):
            for key,item in value.items():
                if key=='moon_pattern' and any(name in item for name in ('ORIGINAL','MARBLED','CRATERED')):
                    value[key]='<RetroMoonPattern.REALISTIC: 5>'
                elif key=='moon_pattern' and item=='<RetroMoonPattern.SMOOTH: 2>':
                    value[key]='<RetroMoonPattern.SMOOTH: 0>'
                else:visit(item)
    visit(result)
    return result

world=unreal.EditorLoadingAndSavingUtils.load_map(map_path)
assert world
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
bp=unreal.load_asset(bp_path)
rigs=[a for a in actors.get_all_level_actors() if isinstance(a,unreal.RetroTwilightEnvironment)]
assert rigs
if '-MoonPatternAudit' in unreal.SystemLibrary.get_command_line():
    before=dict(blueprint=bp_path,map=map_path,actor_count=len(actors.get_all_level_actors()),
                class_defaults=settings(unreal.get_default_object(bp.generated_class())),
                placed={a.get_path_name():snapshot(a) for a in rigs})
    (out/'before.json').write_text(json.dumps(before,indent=2))
    content=Path(unreal.Paths.project_content_dir())
    packages=[bp_path,'/Game/Environment/RetroTwilight/M_RetroTwilightSky',map_path]
    packages += [a.get_package().get_name() for a in rigs]
    for package in set(packages):
        relative=package.removeprefix('/Game/')
        for extension in ('.uasset','.umap'):
            original=content/(relative+extension)
            if original.exists():
                backup=out/'BeforeChanges/Content'/(relative+extension)
                backup.parent.mkdir(parents=True,exist_ok=True)
                if not backup.exists():shutil.copy2(original,backup)
    unreal.log('MOON_PATTERN_AUDIT_OK '+json.dumps(before))
else:
    before=migrated(json.loads((out/'before.json').read_text()))
    latest=dict(blueprint=bp_path,map=map_path,actor_count=len(actors.get_all_level_actors()),
                class_defaults=settings(unreal.get_default_object(bp.generated_class())),
                placed={a.get_path_name():snapshot(a) for a in rigs})
    # Keep edits the user saved while the editor was holding these packages open.
    if latest!=before:
        (out/'latest_before.json').write_text(json.dumps(latest,indent=2))
        before=latest
    sys.path.insert(0,str(source))
    from BuildRetroTwilightMaterial import build_material
    build_material()
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    assert unreal.EditorAssetLibrary.save_loaded_asset(bp,False)
    packages=[]
    for actor in actors.get_all_level_actors():
        if isinstance(actor,unreal.RetroTwilightEnvironment):
            actor.apply_environment()
            assert snapshot(actor)==before['placed'][actor.get_path_name()]
            packages.append(actor.get_package())
    assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    assert unreal.EditorLoadingAndSavingUtils.save_packages(packages,False)
    before['passed']=True
    (out/'migration.json').write_text(json.dumps(before,indent=2))
    unreal.log('MOON_PATTERNS_TRIMMED_OK')
