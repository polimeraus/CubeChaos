"""Read saved levels in a fresh Unreal process; verify class defaults, placed instances and new spawns."""
import json
from pathlib import Path
import unreal
out=Path(unreal.Paths.project_saved_dir())/'StructureUpgrade'
records=json.loads((out/'SavedInstances.json').read_text())
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
cones=(unreal.ChunkedDestructibleConeStructure,unreal.QuadPanelConeStructure)
def check(a,placed=False):
    s=a.get_editor_property('traversal')
    assert s.get_editor_property('floor_openings') and s.get_editor_property('ground_launch_pad'),a.get_name()
    if isinstance(a,cones):assert a.get_editor_property('spiral_outer_rim') and a.get_editor_property('spiral_rim_height')>0
    if isinstance(a,unreal.ChunkedDestructibleBabylonTower):assert a.get_editor_property('open_facade_recesses')
    if placed:
        assert a.effective_opening_width>0
        pad=a.get_editor_property('ground_launch_pad').get_editor_property('child_actor')
        assert isinstance(pad,unreal.LaunchPad)
        assert pad.get_attach_parent_actor()==a
        assert pad.get_editor_property('center_before_launch') and pad.get_editor_property('forward_speed')==0
        assert pad.get_editor_property('pad_material')
for path in json.loads((out/'Blueprints.json').read_text()):
    bp=unreal.load_asset(path);check(unreal.get_default_object(bp.generated_class()))
for level in sorted(set(r['map'] for r in records)):
    assert unreal.EditorLoadingAndSavingUtils.load_map(level)
    all_actors=actors.get_all_level_actors()
    for row in [r for r in records if r['map']==level]:
        matches=[a for a in all_actors if a.get_actor_label()==row['label'] and a.get_class().get_path_name()==row['class']]
        assert len(matches)==1,row
        a=matches[0];check(a,True)
        if isinstance(a,cones):assert a.get_cone_structure_metrics().generated_block_count==row['pieces'],row
        else:assert a.generated_piece_count==row['pieces'] and not a.generation_warning,row
        assert a.effective_opening_width==row['opening_width'],row

folder='/Game/Structures/Destructible/PolygonTower'
assert unreal.EditorLoadingAndSavingUtils.load_map(folder+'/Maps/Lvl_PolygonTowerPreview')
bp=unreal.load_asset(folder+'/BP_ChunkedDestructiblePolygonTower');cdo=unreal.get_default_object(bp.generated_class())
placed=next(a for a in actors.get_all_level_actors() if isinstance(a,unreal.ChunkedDestructiblePolygonTower))
native=actors.spawn_actor_from_class(unreal.ChunkedDestructiblePolygonTower,unreal.Vector(10000,0,0))
fresh=actors.spawn_actor_from_class(bp.generated_class(),unreal.Vector(15000,0,0))
keys=('side_count','tier_count','base_radius','tier_height','top_scale','wall_batter','piece_size','wall_thickness',
      'piece_gap','windows_per_side','window_width','window_height','window_sill_height','entrance_count','entrance_width',
      'entrance_height','entrance_stairs','stair_count','stair_tread_depth','terrace_overhang','floor_thickness',
      'cornice_height','foundation_height','foundation_overhang','stone_material','cornice_material','floor_material')
for k in keys:
    assert native.get_editor_property(k)==fresh.get_editor_property(k)==cdo.get_editor_property(k)==placed.get_editor_property(k),k
for k in ('floor_openings','opening_width','ground_launch_pad','pad_width','auto_launch_height','height_above_roof',
          'launch_up_speed','cooldown','bAffectNPCs','pattern','color','brightness','animation_speed','ring_count',
          'ring_thickness','arrows_material','rings_material'):
    values=[a.get_editor_property('traversal').get_editor_property(k) for a in (native,fresh,cdo,placed)]
    assert all(v==values[0] for v in values),(k,[str(v) for v in values])
for a in [native,fresh,placed]:
    check(a,True);assert a.generated_piece_count==placed.generated_piece_count and not a.generation_warning
fresh.set_editor_property('side_count',5);fresh.set_editor_property('tier_count',7);fresh.rebuild_tower()
assert fresh.generated_piece_count!=placed.generated_piece_count and not fresh.generation_warning
result={'fresh_process_persistence':True,'verified_saved_actors':len(records),
        'polygon_native_blueprint_placed_match':True,'polygon_rebuild':True,
        'polygon_pieces':placed.generated_piece_count,'polygon_openings':placed.generated_opening_count,
        'polygon_height_cm':placed.generated_height}
(out/'Verification.json').write_text(json.dumps(result,indent=2))
unreal.log('STRUCTURE_UPGRADE_VERIFIED '+json.dumps(result))
