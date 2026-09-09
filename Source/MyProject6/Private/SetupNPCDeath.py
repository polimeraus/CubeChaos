"""Create Ghoul death fade materials and a hit cue, then migrate the placed NPC.

Run with UnrealEditor-Cmd -run=pythonscript -script=<this file>
-EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities -unattended -nullrhi.
May also run before compiling: assets are prepared, actor migration waits for the new class.
Existing packages are backed up under Saved/NPCDeathBackup before changes.
"""
from datetime import datetime
from pathlib import Path
import array
import math
import random
import shutil
import wave
import unreal

ROOT = '/Game/NPC'
PARAMETER = 'NPCDeathOpacity'
LEVEL = '/Game/FirstPerson/Lvl_FirstPerson'
saved = Path(unreal.Paths.project_saved_dir())
backup_dir = saved / 'NPCDeathBackup' / datetime.now().strftime('%Y%m%d_%H%M%S')
edit = unreal.MaterialEditingLibrary


def backup(package, extension='.uasset'):
    relative = Path(package.removeprefix('/Game/') + extension)
    source = Path(unreal.Paths.project_content_dir()) / relative
    if source.exists():
        destination = backup_dir / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)


def save(asset):
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, False):
        raise RuntimeError('Could not save ' + asset.get_path_name())


def create_hit_sound():
    path = ROOT + '/Audio/S_NPC_GhoulHit'
    existing = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
    if existing:
        return existing
    # A soft attack, low body thump and a descending, breathy growl; no gunshot sample.
    sample_rate = 44100
    duration = 0.52
    rng = random.Random(7351)
    samples = []
    noise_low = 0.0
    for index in range(int(sample_rate * duration)):
        t = index / sample_rate
        attack = min(1.0, t / 0.003)
        tail = min(1.0, (duration - t) / 0.06)
        noise = rng.uniform(-1.0, 1.0)
        noise_low += 0.16 * (noise - noise_low)
        thump = math.sin(2.0 * math.pi * (105.0 * t - 45.0 * t * t)) * math.exp(-19.0 * t)
        phase = 2.0 * math.pi * (132.0 * t - 72.0 * t * t)
        growl = (math.sin(phase) + 0.3 * math.sin(2.0 * phase) + 0.13 * math.sin(5.0 * phase))
        growl *= (1.0 + 0.16 * math.sin(2.0 * math.pi * 31.0 * t)) * math.exp(-7.5 * t)
        breath = noise_low * math.exp(-10.0 * t)
        samples.append(attack * tail * (0.55 * thump + 0.32 * growl + 0.48 * breath))
    peak = max(abs(value) for value in samples)
    pcm = array.array('h', (int(0.82 * 32767 * value / peak) for value in samples))
    source = saved / 'NPCDeathAudioSource' / 'S_NPC_GhoulHit.wav'
    source.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(source), 'wb') as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(sample_rate)
        output.writeframes(pcm.tobytes())
    task = unreal.AssetImportTask()
    task.set_editor_property('filename', str(source))
    task.set_editor_property('destination_path', ROOT + '/Audio')
    task.set_editor_property('destination_name', 'S_NPC_GhoulHit')
    task.set_editor_property('automated', True)
    task.set_editor_property('save', True)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    sound = unreal.load_asset(path)
    if not isinstance(sound, unreal.SoundWave):
        raise RuntimeError('Hit sound import failed')
    return sound


def create_fade_material(source, index):
    # Duplicate the character's shader so normal maps, textures and lighting stay intact.
    path = ROOT + f'/Materials/M_NPCDeath_Slot{index}'
    material = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
    if material is None:
        material = unreal.EditorAssetLibrary.duplicate_asset(source.get_base_material().get_path_name(), path)
        if not material:
            raise RuntimeError('Could not duplicate Ghoul material')
        material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED)
        material.set_editor_property('dither_opacity_mask', True)
        material.set_editor_property('used_with_skeletal_mesh', True)
        parameter = edit.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -250, 500)
        parameter.set_editor_property('parameter_name', PARAMETER)
        parameter.set_editor_property('default_value', 1.0)
        # Ghoul's original shaders are opaque and have no opacity mask to preserve.
        if not edit.connect_material_property(parameter, '', unreal.MaterialProperty.MP_OPACITY_MASK):
            raise RuntimeError('Could not connect the death opacity mask')
        edit.recompile_material(material)
        save(material)
    instance_path = ROOT + f'/Materials/MI_NPCDeath_Slot{index}'
    instance = unreal.load_asset(instance_path) if unreal.EditorAssetLibrary.does_asset_exist(instance_path) else None
    if instance is None:
        instance = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            f'MI_NPCDeath_Slot{index}', ROOT + '/Materials', unreal.MaterialInstanceConstant,
            unreal.MaterialInstanceConstantFactoryNew())
        edit.set_material_instance_parent(instance, material)
        edit.set_material_instance_scalar_parameter_value(instance, PARAMETER, 1.0)
        save(instance)
    return instance


sound = create_hit_sound()
mesh = unreal.load_asset('/Game/Characters/Ghoul/Mesh/SK_Ghoul_UE5')
materials = [create_fade_material(slot.get_editor_property('material_interface'), index)
             for index, slot in enumerate(mesh.get_editor_property('materials'))]
animation = unreal.load_asset('/Game/Characters/Mannequins/Anims/Death/MM_Death_Front_01')
skeleton = mesh.get_editor_property('skeleton')
animation_skeleton = animation.get_editor_property('skeleton')
if animation_skeleton not in list(skeleton.get_editor_property('compatible_skeletons')):
    backup(skeleton.get_path_name().split('.')[0])
    skeleton.add_compatible_skeleton(animation_skeleton)
    save(skeleton)

defaults = unreal.get_default_object(unreal.ChasingNPC)
if hasattr(defaults, 'hit_sound'):
    assert defaults.get_editor_property('hit_sound') == sound, 'Rebuild editor target to load new defaults'
    if not unreal.EditorLoadingAndSavingUtils.load_map(LEVEL):
        raise RuntimeError('Could not load gameplay level')
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    npcs = [actor for actor in actors if actor.get_actor_label() == 'NPC_PlayerChaser']
    if len(npcs) != 1 or not isinstance(npcs[0], unreal.ChasingNPC):
        raise RuntimeError('Expected one existing NPC_PlayerChaser')
    npc = npcs[0]
    old_sound = unreal.load_asset('/Game/Weapons/GrenadeLauncher/Audio/FirstPersonTemplateWeaponFire02')
    # Retire the old serialized gunshot override; respect an explicitly customized death cue.
    if npc.get_editor_property('death_sound') == old_sound:
        backup(LEVEL, '.umap')
        npc.set_editor_property('death_sound', None)
        if not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level():
            raise RuntimeError('Could not save NPC death sound migration')
    assert npc.get_editor_property('hit_sound') == sound
    assert npc.get_editor_property('death_animation') == animation
    assert list(npc.get_editor_property('death_fade_materials')) == materials
    unreal.log('NPC_DEATH_SETUP_OK: animation=1.1s hold=3s fade=2s; new editable hit cue; map verified')
else:
    unreal.log('NPC_DEATH_ASSETS_OK: rebuild MyProject6Editor and rerun to verify/migrate the placed NPC')
