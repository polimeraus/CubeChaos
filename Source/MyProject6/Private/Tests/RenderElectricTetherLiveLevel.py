"""Saved main map + actual player viewport, input bindings and level exposure.

Only transient PIE actors are repositioned. No map or asset is saved.
Run in UnrealEditor /Game/FirstPerson/Lvl_FirstPerson -RenderOffscreen
-ExecutePythonScript=<this file>. Requires the editor automation test bridge.
"""
import json,time,traceback
from pathlib import Path
import unreal

out=Path(unreal.Paths.project_saved_dir()).resolve()/'ElectricTether/LiveLevel'
out.mkdir(parents=True,exist_ok=True)
editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
world=editor.get_editor_world()
assert world.get_name()=='Lvl_FirstPerson'
state={'phase':'start','since':time.monotonic(),'started':time.monotonic(),'frames':[]}

def close(error=None):
 unreal.unregister_slate_post_tick_callback(state['handle'])
 level.editor_request_end_play()
 (out/'Result.json').write_text(json.dumps({'passed':error is None,'error':error,'spawn_rotation':state.get('spawn_rotation'),'weapon_rotation':state.get('weapon_rotation'),'frames':state['frames']},indent=2))
 unreal.log('ARC_LIVE_LEVEL '+('FAILED '+error if error else 'OK'))
 unreal.EditorPythonScripting.set_keep_python_script_alive(False)
 unreal.SystemLibrary.execute_console_command(world,'QUIT_EDITOR')

def key(down):
 unreal.SystemLibrary.execute_console_command(state['game'],'MyProject6.Test.ArcMouse '+('down' if down else 'up'))

def shot(name):
 p=out/(name+'.png')
 # A regular Shot is a global request that another editor viewport can consume.
 # HighResShot explicitly schedules this player's game viewport.
 state['capturing']=True
 try:unreal.SystemLibrary.execute_console_command(state['game'],f'HighResShot 1280x720 filename="{p}"')
 finally:state['capturing']=False
 state['frames'].append({'name':name,'firing':state['weapon'].firing,'captured':state['weapon'].has_target(),'field_visible':state['weapon'].field.is_visible(),'npc_position':str(state['npc'].get_actor_location())})

def tick(delta):
 if state.get('capturing'):return
 try:
  now=time.monotonic()
  if now-state['started']>180:raise RuntimeError('Timed out in '+state['phase'])
  if state['phase']=='start' and now-state['since']>5:
   level.editor_request_begin_play();state['phase']='world'
  elif state['phase']=='world':
   game=editor.get_game_world()
   if not game:return
   player=unreal.GameplayStatics.get_player_character(game,0)
   if not isinstance(player,unreal.MyProject6Character):return
   state['spawn_rotation']=str(player.get_actor_rotation())
   state['weapon_rotation']=str(player.electric_weapon.get_actor_rotation())
   assert abs(abs(player.electric_weapon.get_actor_rotation().yaw)-180)<1,'Test must retain the saved level spawn rotation'
   npcs=unreal.GameplayStatics.get_all_actors_of_class(game,unreal.ChasingNPC)
   for n in npcs+unreal.GameplayStatics.get_all_actors_of_class(game,unreal.WhisperFollower):
    if n.get_controller():n.get_controller().destroy_actor()
    n.character_movement.disable_movement()
   n=next(n for n in npcs if not n.is_dead())
   player.character_movement.disable_movement();player.equip_electric_tether()
   state.update(game=game,player=player,npc=n,weapon=player.electric_weapon,phase='warm',since=now)
  elif state['phase']=='warm' and now-state['since']>5:
   p=state['player'];camera=p.get_component_by_class(unreal.CameraComponent)
   n=state['npc'];n.set_actor_location(camera.get_world_location()+camera.get_forward_vector()*650,False,True)
   n.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(n.get_actor_location(),p.get_actor_location()),False)
   shot('01_Idle');state.update(phase='press',since=now)
  elif state['phase']=='press' and now-state['since']>.5:
   key(True);state.update(phase='hold',since=now)
  elif state['phase']=='hold' and now-state['since']>2:
   assert state['weapon'].firing,'Mouse press did not sustain beam'
   assert state['weapon'].has_target(),'Mouse press did not capture the visible NPC'
   assert state['weapon'].field.is_visible(),'Field is hidden'
   shot('02_Held');state.update(phase='hold_again',since=now)
  elif state['phase']=='hold_again' and now-state['since']>.8:
   assert state['weapon'].has_target()
   shot('02b_FieldMotion');state.update(phase='turn',since=now)
  elif state['phase']=='turn' and now-state['since']>.5:
   pc=state['player'].get_controller();r=pc.get_control_rotation()
   pc.set_control_rotation(unreal.Rotator(pitch=r.pitch+5,yaw=r.yaw+15,roll=r.roll))
   state.update(phase='swing',since=now)
  elif state['phase']=='swing' and now-state['since']>.2:
   assert state['weapon'].has_target(),'Mouse turn lost the captive'
   shot('03_Swing');state.update(phase='release',since=now)
  elif state['phase']=='release' and now-state['since']>.15:
   key(False);state.update(phase='released',since=now)
  elif state['phase']=='released' and now-state['since']>.3:
   assert not state['weapon'].firing and not state['weapon'].has_target(),'Mouse release failed'
   assert not state['weapon'].field.is_visible(),'Electricity remains after release fade'
   shot('04_Released');state.update(phase='finish',since=now)
  elif state['phase']=='finish' and now-state['since']>1:
   for row in state['frames']:assert (out/(row['name']+'.png')).is_file(),row
   close()
 except Exception:close(traceback.format_exc())

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state['handle']=unreal.register_slate_post_tick_callback(tick)
