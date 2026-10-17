"""Read-only inventory of the main map before replacing its environment rig."""
import json
from pathlib import Path
import unreal

out = Path(unreal.Paths.project_saved_dir()) / 'TwilightEnvironment'
out.mkdir(parents=True, exist_ok=True)
world = unreal.EditorLoadingAndSavingUtils.load_map('/Game/FirstPerson/Lvl_FirstPerson')
assert world
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
rows = []
for actor in actors:
    row = dict(label=actor.get_actor_label(), path=actor.get_path_name(),
               cls=actor.get_class().get_path_name(), location=str(actor.get_actor_location()),
               rotation=str(actor.get_actor_rotation()))
    components = []
    for component in actor.get_components_by_class(unreal.ActorComponent):
        if isinstance(component, (unreal.LightComponentBase, unreal.ExponentialHeightFogComponent,
                                  unreal.SkyAtmosphereComponent, unreal.PostProcessComponent)):
            detail = dict(cls=component.get_class().get_name())
            for prop in ('intensity', 'light_color', 'fog_density', 'source_type', 'real_time_capture'):
                try:
                    detail[prop] = str(component.get_editor_property(prop))
                except Exception:
                    pass
            components.append(detail)
    row['environment_components'] = components
    rows.append(row)
(out / 'before.json').write_text(json.dumps(rows, indent=2), encoding='utf-8')
unreal.log('TWILIGHT_INVENTORY_OK actors=' + str(len(rows)))
