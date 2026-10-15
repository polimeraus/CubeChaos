"""Fresh-process verification of package locations, actors, and placeable classes."""
import json, re
from pathlib import Path
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
out = root/'Saved/ContentOrganization'
plan = json.loads((out/'Plan.json').read_text())
baseline = json.loads((out/'BaselineAssets.json').read_text())
mapping = {r['source']:r['target'] for r in plan['moves']}
pattern = re.compile('|'.join(re.escape(p) for p in sorted(mapping,key=len,reverse=True)))
prefixes={}
for row in plan['moves']:
    if row['source'].startswith('/Game/Characters/'):
        prefixes['/'.join(row['source'].split('/')[:4])+'/']='/'.join(row['target'].split('/')[:4])+'/'
for name in ('ParagonCrunch','ParagonFengMao','ParagonGRIMexe'):
    prefixes['/Game/'+name+'/']='/Game/NPC/'+name+'/'
def remap(value):
    value=pattern.sub(lambda m:mapping[m.group()],value)
    for old,new in prefixes.items():
        if value.startswith(old):return new+value[len(old):]
    return value
def normalized(value):
    return re.sub(r'0x[0-9A-Fa-f]+','POINTER',str(value))
def transform_equal(current,previous):
    def numbers(v): return [float(n) for n in re.findall(r'(?:x|y|z|w|pitch|yaw|roll): (-?\d+\.\d+)',str(v))]
    a,b=numbers(current),numbers(previous)
    return bool(a) and len(a)==len(b) and all(abs(x-y)<0.0001 for x,y in zip(a,b))

registry=unreal.AssetRegistryHelpers.get_asset_registry()
registry.search_all_assets(True)
registry.wait_for_completion()
assets=list(registry.get_assets_by_path('/Game',True))
by_package={str(a.package_name):a for a in assets}
checks=[]
def check(condition,label):
    assert condition,label
    checks.append(label)

for row in plan['moves']:
    if row['type']=='ObjectRedirector':continue
    check(row['target'] in by_package,'Saved package '+row['target'])
    suffix='.umap' if row['type']=='World' else '.uasset'
    check((root/'Content'/(row['target'].removeprefix('/Game/')+suffix)).exists(),'File '+row['target'])

# Folder roots expose exactly one structure Blueprint; support assets stay below.
structure_bps=[a for a in assets if str(a.package_name).startswith('/Game/Structures/') and str(a.asset_class_path.asset_name)=='Blueprint']
for bp in structure_bps:
    folder=str(bp.package_path)
    direct=list(registry.get_assets_by_path(folder,False))
    check(len(direct)==1 and str(direct[0].asset_class_path.asset_name)=='Blueprint','Structure root '+folder)
check(len(structure_bps)==24,'All 24 structure entry points exist')

options=unreal.AssetRegistryDependencyOptions(include_soft_package_references=True,
    include_hard_package_references=True,include_searchable_names=False,
    include_soft_management_references=False,include_hard_management_references=False)
baseline_packages={a['package'] for a in baseline}
known_missing={(remap(a['package']),remap(d)) for a in baseline for d in a['dependencies'] if d not in baseline_packages}
new_missing=[]
for package,a in by_package.items():
    if str(a.asset_class_path.asset_name) in ('ObjectRedirector','Rig'):continue
    for dep in registry.get_dependencies(a.package_name,options):
        dep=str(dep)
        if dep.startswith('/Game/') and dep not in by_package and remap(dep) not in by_package:
            if (package,dep) not in known_missing:new_missing.append((package,dep))
(out/'DependencyAudit.json').write_text(json.dumps(dict(new_missing=new_missing,baseline_missing=len(known_missing)),indent=2))
check(not new_missing,'No new missing package dependencies')

loaded=[]
vendor_blueprints_with_errors=[]
for a in assets:
    kind=str(a.asset_class_path.asset_name)
    path=str(a.package_name)
    if kind not in ('Blueprint','AnimBlueprint','SkeletalMesh','MapBuildDataRegistry') and not path.startswith(('/Game/Structures/','/Game/GameplayActors/','/Game/Materials/','/Game/Weapons/Railgun/','/Game/Weapons/BFG/')):
        continue
    if kind in ('World','ObjectRedirector'):continue
    asset=a.get_asset()
    check(asset is not None,'Fresh load '+path)
    if kind in ('Blueprint','AnimBlueprint'):
        check(asset.generated_class() is not None,'Generated class '+path)
        if asset.get_editor_property('status') == unreal.BlueprintStatus.BS_ERROR:
            parent = str(a.get_tag_value('NativeParentClass') or '')
            check('/Script/MyProject6.' not in parent,'Project Blueprint compiles '+path)
            vendor_blueprints_with_errors.append(path)
    loaded.append(path)

for index,row in enumerate(json.loads((out/'ArchivedResidualPackages.json').read_text())):
    asset=unreal.load_asset(row['source'])
    check(asset is not None,'Legacy reference load '+row['source'])
    check(asset.get_path_name().split('.')[0]==row['target'],'Legacy reference target '+row['source'])
    if index%50==0:unreal.SystemLibrary.collect_garbage()

# Native paths cannot rely on package redirectors after fix-up.
native_paths=set()
for source in (root/'Source/MyProject6').rglob('*'):
    if source.suffix not in ('.cpp','.h') or 'Tests' in source.parts:continue
    native_paths.update(re.findall(r'"(/Game/[^"\r\n]+)"',source.read_text(encoding='utf-8-sig')))
for path in native_paths:
    package=path.split('.')[0]
    check(package in by_package,'Native asset path '+path)
    check(unreal.load_asset(package) is not None,'Native asset load '+path)

# Compare the saved primary level against its pre-migration snapshot.
check(unreal.EditorLoadingAndSavingUtils.load_map('/Game/FirstPerson/Lvl_FirstPerson') is not None,'Primary map reload')
actor_system=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actors={a.get_name():a for a in actor_system.get_all_level_actors()}
before=json.loads((out/'BaselineActors.json').read_text())
persistent={a['name'] for a in baseline if a['package'].startswith('/Game/__ExternalActors__/FirstPerson/Lvl_FirstPerson/')}
check(len(actors)==len(before) and persistent.issubset(actors),'Persistent level actor identities unchanged')
# ChildActorComponent regenerates its 18 child actor object names on each load.
# Match these by class, label, and world transform instead of transient UAIDs.
children=[a for name,a in actors.items() if name not in persistent]
for row in before:
    if row['name'] in persistent:continue
    matches=[a for a in children if a.get_class().get_path_name()==remap(row['actor_class'])
        and a.get_actor_label()==row['label'] and transform_equal(a.get_actor_transform().to_tuple(),row['transform'])]
    check(len(matches)==1,'Reconstructed child '+row['name'])
    actors[row['name']]=matches[0]
    children.remove(matches[0])
check(not children,'All reconstructed child actors match baseline')
for row in before:
    actor=actors[row['name']]
    check(actor.get_actor_label()==row['label'],'Actor label '+row['label'])
    check(actor.get_class().get_path_name()==remap(row['actor_class']),'Actor class '+row['label'])
    check(str(actor.get_folder_path())==row['folder'],'Actor folder '+row['label'])
    check(transform_equal(actor.get_actor_transform().to_tuple(),row['transform']),'Actor transform '+row['label'])
    if 'mesh' in row:
        mesh=actor.get_component_by_class(unreal.SkeletalMeshComponent)
        asset=mesh.get_editor_property('skeletal_mesh_asset')
        check((asset.get_path_name() if asset else None)==(remap(row['mesh']) if row['mesh'] else None),'NPC mesh '+row['label'])
        check(transform_equal(mesh.get_relative_transform(),row['mesh_relative']),'NPC mesh placement '+row['label'])

portal=unreal.load_asset('/Game/GameplayActors/NPCSpawner/BP_NPCSpawnPoint')
types=unreal.get_default_object(portal.generated_class()).get_editor_property('npc_types')
for name,cls in types.items():
    check(cls is not None,'Portal class '+str(name))
    check(cls.get_path_name().startswith(('/Game/NPC/','/Script/MyProject6.')),'Portal canonical class '+str(name))

# Exercise each newly created Blueprint's construction on an unsaved temporary map.
unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
spawned=[]
spawn_rows=list(plan['create']) + [dict(package=r['target']) for r in plan['moves'] if r['type']=='Blueprint' and r['target'].startswith('/Game/Structures/')]
for row in spawn_rows:
    bp=unreal.load_asset(row['package'])
    check(bp is not None,'New Blueprint reload '+row['package'])
    actor=actor_system.spawn_actor_from_class(bp.generated_class(),unreal.Vector())
    check(actor is not None,'Blueprint placement '+row['package'])
    components=actor.get_components_by_class(unreal.PrimitiveComponent)
    check(len(components)>0,'Constructed geometry '+row['package'])
    spawned.append(dict(package=row['package'],components=len(components)))
    actor_system.destroy_actor(actor)
    unreal.SystemLibrary.collect_garbage()

report=dict(passed=True,checks=len(checks),moved_packages=len(plan['moves']),
    structure_blueprints=len(structure_bps),fresh_loaded=loaded,placed_actor_count=len(before),
    portal_types=len(types),spawned=spawned,vendor_blueprints_with_errors=vendor_blueprints_with_errors)
(out/'Verification.json').write_text(json.dumps(report,indent=2))
unreal.log('CONTENT_ORGANIZATION_VERIFIED '+json.dumps({k:v for k,v in report.items() if k not in ('fresh_loaded','spawned')}))
