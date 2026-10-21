"""Real rendered PIE frame capture, including sustained fire, lighting and impact work."""
from pathlib import Path
import json, time, traceback, unreal
out=Path(unreal.Paths.project_saved_dir()).resolve()/'WeaponCombat';out.mkdir(parents=True,exist_ok=True)
label='After' if '-CombatAfter' in unreal.SystemLibrary.get_command_line() else 'Before'
assert unreal.EditorLoadingAndSavingUtils.load_map('/Game/FirstPerson/Lvl_FirstPerson')
editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for actor in actors.get_all_level_actors():
    if isinstance(actor,unreal.NPCSpawnPoint):actor.set_editor_property('spawn_on_begin_play',False)
world=editor.get_editor_world()
for command in ['t.MaxFPS 0','r.VSync 0','r.GPUCsvStatsEnabled 1','r.SetRes 1280x720w']:
    unreal.SystemLibrary.execute_console_command(world,command)
phases=[('Idle',5),('Flight',12),('FlightNoLights',12),('FlightNoVisuals',12),('Impact',8),('FloorImpact',8)]
# Each phase is rendered in PIE; CSV captures include CPU/GPU timings, not just
# the cost of generating isolated projectile meshes. No level assets are saved.
state={'stage':'start','started':time.monotonic(),'since':time.monotonic(),'index':-1,'rows':[]}

def finish(error=None):
    if state.get('game'):unreal.SystemLibrary.execute_console_command(state['game'],'csvprofile STOP')
    unreal.unregister_slate_post_tick_callback(state['handle'])
    (out/('Combat'+label+'.json')).write_text(json.dumps({'error':error,'samples':state['rows']},indent=2))
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    unreal.log('BFG_COMBAT_PROFILE_'+label.upper()+(' FAILED '+str(error) if error else ' OK'))

def phase(now):
    state['index']+=1
    if state['index']==len(phases):state.update(stage='finish',since=now);return
    name,_=phases[state['index']]
    game,player=state['game'],state['player']
    for shot in unreal.GameplayStatics.get_all_actors_of_class(game,unreal.BFGProjectile):shot.destroy_actor()
    player.set_actor_location(unreal.Vector(-2470,-800,500),False,True)
    player.get_controller().set_control_rotation(unreal.Rotator(pitch=55,yaw=90))
    if name=='Impact':player.get_controller().set_control_rotation(unreal.Rotator(pitch=0,yaw=90))
    if name=='FloorImpact':player.get_controller().set_control_rotation(unreal.Rotator(pitch=-60,yaw=90))
    unreal.SystemLibrary.execute_console_command(game,'csvprofile STARTFILE=BFGCombat'+label+'_'+name+'.csv')
    unreal.SystemLibrary.execute_console_command(game,'csvprofile START')
    state.update(stage='capture',since=now,previous=now,last_shot=-1.,frame=0)
    unreal.log('COMBAT_PHASE '+name)

def tick(delta):
    try:
        now=time.monotonic()
        if now-state['started']>210:raise RuntimeError('Timeout '+state['stage'])
        if state['stage']=='start' and now-state['since']>3:
            unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play();state['stage']='world'
        elif state['stage']=='world':
            game=editor.get_game_world()
            if not game:return
            player=unreal.GameplayStatics.get_player_character(game,0)
            if not player:return
            player.character_movement.disable_movement()
            player.set_editor_property('void_fall_energy_drain_per_second',0.)
            player.equip_bfg()
            for npc in unreal.GameplayStatics.get_all_actors_of_class(game,unreal.Character):
                if npc!=player:
                    npc.character_movement.disable_movement();npc.set_actor_tick_enabled(False)
                    if npc.get_controller():npc.get_controller().stop_movement()
            state.update(game=game,player=player,gun=player.get_editor_property('railgun'),stage='warmup',since=now)
        elif state['stage']=='warmup' and now-state['since']>12:phase(now)
        elif state['stage']=='capture':
            name,duration=phases[state['index']];game=state['game'];gun=state['gun']
            elapsed=now-state['since']
            if elapsed>=duration:
                unreal.SystemLibrary.execute_console_command(game,'csvprofile STOP');state.update(stage='gap',since=now);return
            shot_ms=0.
            if name!='Idle' and now-state['last_shot']>=.25:
                start=time.perf_counter();gun.fire_bfg();shot_ms=(time.perf_counter()-start)*1000;state['last_shot']=now
            shots=unreal.GameplayStatics.get_all_actors_of_class(game,unreal.BFGProjectile)
            if name in ['FlightNoLights','FlightNoVisuals']:
                gun.bfg_muzzle_light.set_visibility(False)
                for shot in shots:
                    shot.plasma_light.set_visibility(False)
                    if name=='FlightNoVisuals':shot.plasma.set_visibility(False)
            else:gun.bfg_muzzle_light.set_visibility(True)
            state['rows'].append({'phase':name,'time':elapsed,'frame_ms':(now-state['previous'])*1000,'fire_ms':shot_ms,'projectiles':len(shots),'shots':gun.bfg_shots_fired})
            state['previous']=now
        elif state['stage']=='gap' and now-state['since']>1:phase(now)
        elif state['stage']=='finish' and now-state['since']>2:finish()
    except Exception:finish(traceback.format_exc())

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state['handle']=unreal.register_slate_post_tick_callback(tick)
