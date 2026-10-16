from pathlib import Path
import unreal,json
out=Path(unreal.Paths.project_saved_dir())/'RailTowerUpgrade';out.mkdir(parents=True,exist_ok=True)
player=unreal.get_default_object(unreal.load_class(None,'/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C'))
capsule=player.get_component_by_class(unreal.CapsuleComponent)
bp=unreal.load_asset('/Game/Structures/Destructible/BabylonTower/BP_ChunkedDestructibleBabylonTower')
keys=['entrance_width','entrance_height','window_width','window_height','tier_window_widths','tier_window_counts']
def settings(o):return {k:list(o.get_editor_property(k)) if k.startswith('tier_') else o.get_editor_property(k) for k in keys}
data={'player_capsule_radius':capsule.get_unscaled_capsule_radius(),'player_capsule_half_height':capsule.get_unscaled_capsule_half_height(),
      'native':settings(unreal.get_default_object(unreal.ChunkedDestructibleBabylonTower)),
      'blueprint':settings(unreal.get_default_object(bp.generated_class())),'instances':[],'rail_visuals':str(player.get_editor_property('railgun_visuals'))}
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for path in ['/Game/FirstPerson/Lvl_FirstPerson','/Game/Structures/Destructible/BabylonTower/Maps/Lvl_BabylonTowerPreview']:
    assert level.load_level(path)
    for a in actors.get_all_level_actors():
        if isinstance(a,unreal.ChunkedDestructibleBabylonTower):data['instances'].append({'map':path,'label':a.get_actor_label(),**settings(a)})
(out/'Before.json').write_text(json.dumps(data,indent=2))
unreal.log('RAIL_TOWER_INSPECTED')
