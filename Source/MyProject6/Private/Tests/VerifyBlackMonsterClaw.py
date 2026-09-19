"""Check the saved claw asset's broad overhead swings and continuous loop.

Run with UnrealEditor-Cmd -run=pythonscript -script=<this file> -nullrhi.
Gameplay/contact behavior is covered separately by BlackMonsterContactTests.
"""
import json
import math
from pathlib import Path
import unreal

asset = unreal.load_asset('/Game/Characters/black-monster/Animations/Anim_BlackMonster_Claw')
ext = unreal.AnimPoseExtensions
world = unreal.AnimPoseSpaces.WORLD
names = ['Hips', 'Head', 'LeftArm', 'RightArm', 'LeftForeArm', 'RightForeArm',
         'LeftHand', 'RightHand', 'LeftToeBase', 'RightToeBase']
length = asset.get_play_length()
samples = []
for frame in range(121):
    pose = ext.get_anim_pose_at_time(asset, length*frame/120, unreal.AnimPoseEvaluationOptions())
    samples.append({name: ext.get_bone_pose(pose, name, world).translation for name in names})

report = {'length_seconds': length, 'arms': {}}
for side, start in [('Right', 0), ('Left', 60)]:
    strike = samples[start:start+46]
    hand, elbow, shoulder = side+'Hand', side+'ForeArm', side+'Arm'
    directions = [s[elbow]-s[shoulder] for s in strike]
    directions = [v*(1/v.length()) for v in directions]
    excursion = max(math.degrees(math.acos(max(-1, min(1, a.x*b.x+a.y*b.y+a.z*b.z))))
                    for a in directions for b in directions)
    overhead = max(s[hand].z-s['Head'].z for s in strike)
    span = max((a[hand]-b[hand]).length() for a in strike for b in strike)
    peak_speed = max((b[hand]-a[hand]).length()/(length/120) for a, b in zip(strike, strike[1:]))
    report['arms'][side] = {'upper_arm_sweep_degrees': excursion,
                            'wrist_above_head_cm': overhead, 'wrist_span_cm': span,
                            'peak_wrist_speed_cm_s': peak_speed}
    assert excursion > 100, (side, 'Swing must originate at the shoulder', excursion)
    assert overhead > 45, (side, 'Claw must wind up above the head', overhead)
    assert span > 140, (side, 'Claw must sweep through a broad arc', span)
    assert peak_speed > 650, (side, 'Downswing must have a sharp acceleration', peak_speed)

turns = [math.degrees(math.atan2((s['LeftArm']-s['RightArm']).y,
                               (s['LeftArm']-s['RightArm']).x)) for s in samples]
report['shoulder_turn_range_degrees'] = max(turns)-min(turns)
report['toe_drift_cm'] = max((s[foot]-samples[0][foot]).length()
                            for foot in ['LeftToeBase', 'RightToeBase'] for s in samples)
report['loop_seam_cm'] = max((samples[0][name]-samples[-1][name]).length() for name in names)
output = Path(unreal.Paths.project_saved_dir()) / 'BlackMonster/claw_validation.json'
output.write_text(json.dumps(report, indent=2))
assert report['shoulder_turn_range_degrees'] > 100, 'Both slashes must drive the torso'
assert report['toe_drift_cm'] < 1, 'Feet must stay planted during the body twist'
assert report['loop_seam_cm'] < .1, 'The combo must loop without a pose jump'
report['passed'] = True
output.write_text(json.dumps(report, indent=2))
unreal.log('BLACK_MONSTER_CLAW_VERIFIED ' + json.dumps(report))
