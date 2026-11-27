"""Writes examples/playertest: an interactive in-game showcase of the entire Player package.

Every event (8), condition (16), action (23), and expression (56) of packages/player is wired
into a 3-page live HUD + hotkey showcase so all active PlayerApi calls can be tested in-game.

    python tools/make_playertest.py
"""
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROJECT = os.path.join(ROOT, 'examples', 'playertest')
PLAYER_PKG = os.path.join(ROOT, 'packages', 'player', 'catalog')
SDK = os.path.join(os.path.dirname(ROOT), 'sdk', 'include')


def current_api_version():
    path = os.path.join(SDK, 'ForgeTypes.h')
    if os.path.exists(path):
        m = re.search(r'^\s*#\s*define\s+AMOD_API_VERSION\s+(\d+)',
                      open(path, encoding='utf-8', errors='ignore').read(), re.M)
        if m:
            return int(m.group(1))
    return 1


def rule(event='', filters=None, conditions=(), actions=(), children=(),
         name='', group=False, collapsed=False, veto=False, enabled=True):
    return {
        'event': event,
        'filters': dict(filters or {}),
        'conditions': list(conditions),
        'actions': list(actions),
        'children': list(children),
        'veto': veto,
        'enabled': enabled,
        'group': group,
        'name': name,
        'collapsed': collapsed,
    }


def cond(entry_id, inverted=False, **args):
    out = {'id': entry_id, 'args': {k: str(v) for k, v in args.items()}}
    if inverted:
        out['inverted'] = True
    return out


def act(entry_id, **args):
    return {'id': entry_id, 'args': {k: str(v) for k, v in args.items()}}


def hud_line(y, text, r=0.90, g=0.94, b=0.99):
    return act('render.textAligned', text=text, x=16, y=y, font='', scale=0.42, align='0x08', r=r, g=g, b=b, a=1)


def set_status(text):
    return act('var.setText', name='last_cmd', value=text)


def build_sheet():
    variables = [
        {'name': 'hud_on', 'type': 'bool', 'value': '1'},
        {'name': 'hud_page', 'type': 'int', 'value': '0'},
        {'name': 'shield_on', 'type': 'bool', 'value': '0'},
        {'name': 'mirror_on', 'type': 'bool', 'value': '0'},
        {'name': 'hooks_paused', 'type': 'bool', 'value': '0'},
        {'name': 'cancel_probe', 'type': 'bool', 'value': '0'},
        {'name': 'drv_cycle', 'type': 'int', 'value': '0'},
        {'name': 'cp_saved', 'type': 'bool', 'value': '0'},
        {'name': 'cp_x', 'type': 'float', 'value': '0.0'},
        {'name': 'cp_y', 'type': 'float', 'value': '0.0'},
        {'name': 'cp_z', 'type': 'float', 'value': '0.0'},
        {'name': 'ev_died_cnt', 'type': 'int', 'value': '0'},
        {'name': 'ev_rev_cnt', 'type': 'int', 'value': '0'},
        {'name': 'ev_lives_cnt', 'type': 'int', 'value': '0'},
        {'name': 'ev_score_cnt', 'type': 'int', 'value': '0'},
        {'name': 'ev_blocked_cnt', 'type': 'int', 'value': '0'},
        {'name': 'ev_state_ticks', 'type': 'int', 'value': '0'},
        {'name': 'ev_drive_ticks', 'type': 'int', 'value': '0'},
        {'name': 'ev_move_ticks', 'type': 'int', 'value': '0'},
        {'name': 'last_ev_kind', 'type': 'int', 'value': '-1'},
        {'name': 'last_ev_lives', 'type': 'int', 'value': '0'},
        {'name': 'last_ev_was_lives', 'type': 'int', 'value': '0'},
        {'name': 'last_ev_score', 'type': 'int', 'value': '0'},
        {'name': 'last_ev_was_score', 'type': 'int', 'value': '0'},
        {'name': 'last_ev_crashed', 'type': 'int', 'value': '0'},
        {'name': 'last_d_kind', 'type': 'int', 'value': '0'},
        {'name': 'last_d_lives', 'type': 'int', 'value': '0'},
        {'name': 'last_d_x', 'type': 'float', 'value': '0.0'},
        {'name': 'last_d_y', 'type': 'float', 'value': '0.0'},
        {'name': 'last_d_z', 'type': 'float', 'value': '0.0'},
        {'name': 'hk_dt', 'type': 'float', 'value': '0.0'},
        {'name': 'hk_x', 'type': 'float', 'value': '0.0'},
        {'name': 'hk_y', 'type': 'float', 'value': '0.0'},
        {'name': 'hk_z', 'type': 'float', 'value': '0.0'},
        {'name': 'hk_fall', 'type': 'float', 'value': '0.0'},
        {'name': 'hk_st', 'type': 'int', 'value': '0'},
        {'name': 'hk_sub', 'type': 'int', 'value': '0'},
        {'name': 'mv_dx', 'type': 'int', 'value': '0'},
        {'name': 'mv_dy', 'type': 'int', 'value': '0'},
        {'name': 'cond_mask', 'type': 'int', 'value': '0'},
        {'name': 'step_dx', 'type': 'int', 'value': '0'},
        {'name': 'step_dy', 'type': 'int', 'value': '0'},
        {'name': 'step_jmp', 'type': 'int', 'value': '0'},
        {'name': 'st_here', 'type': 'text', 'value': 'MENU'},
        {'name': 'last_cmd', 'type': 'text', 'value': 'Ready: [1] Start Level 1 (in menu) / Save Checkpoint (in room)  [F2] HUD Page'},
    ]

    cond_probes = [
        rule(actions=[act('var.set', name='cond_mask', value='0')]),
        rule(conditions=[cond('player.isHere')],
             actions=[act('var.setText', name='st_here', value='IN ROOM'),
                      act('var.add', name='cond_mask', value='1')]),
        rule(conditions=[cond('player.isHere', inverted=True)],
             actions=[act('var.setText', name='st_here', value='MENU')]),
        rule(conditions=[cond('player.isDead')],
             actions=[act('var.add', name='cond_mask', value='2')]),
        rule(conditions=[cond('player.isCrashed')],
             actions=[act('var.add', name='cond_mask', value='4')]),
        rule(conditions=[cond('player.isOnFire')],
             actions=[act('var.add', name='cond_mask', value='8')]),
        rule(conditions=[cond('player.isShocked')],
             actions=[act('var.add', name='cond_mask', value='16')]),
        rule(conditions=[cond('player.stateIs', state='AMOD_PLAYER_STATE_ONFOOT')],
             actions=[act('var.add', name='cond_mask', value='32')]),
        rule(conditions=[cond('player.subStateIs', subState='AMOD_PLAYER_SUB_STILL')],
             actions=[act('var.add', name='cond_mask', value='64')]),
        rule(conditions=[cond('player.facingIs', dir='1')],
             actions=[act('var.add', name='cond_mask', value='128')]),
        rule(conditions=[cond('player.collides')],
             actions=[act('var.add', name='cond_mask', value='256')]),
        rule(conditions=[cond('player.fallSpeedIs', op='>', speed='0.05')],
             actions=[act('var.add', name='cond_mask', value='512')]),
        rule(conditions=[cond('player.livesCompare', op='>=', lives='3')],
             actions=[act('var.add', name='cond_mask', value='2048')]),
        rule(conditions=[cond('player.scoreCompare', op='>=', points='500')],
             actions=[act('var.add', name='cond_mask', value='8192')]),
        rule(conditions=[cond('player.driverIs', mode='AMOD_PLAYER_DRIVER_GAME')],
             actions=[act('var.add', name='cond_mask', value='16384')]),
        rule(conditions=[cond('player.placeCellIs', col='2', row='4', type='32')],
             actions=[act('var.add', name='cond_mask', value='32768')]),
        rule(conditions=[cond('player.ask', question='AMOD_PLAYER_ASK_FOOTING')],
             actions=[act('var.add', name='cond_mask', value='65536')]),
    ]

    hud_page_0 = [
        hud_line(40,
                 'Pos:({~player_position(0)}, {~player_position(1)}, {~player_position(2)})  Prev:({~player_num(3)}, {~player_num(4)}, {~player_num(5)})',
                 r=0.82, g=0.95, b=1.0),
        hud_line(60,
                 'Entity:{#player_int(0)}  Facing:{~player_num(6)} (dir:{#player_facing_dir()})  Fall:{~player_num(7)}  Collides:{#player_collides()}',
                 r=0.88, g=0.92, b=0.98),
        hud_line(80,
                 'State:{#player_int(1)} Sub:{#player_int(2)}  Dead:{#player_flag(0)} Crash:{#player_flag(1)} Fire:{#player_flag(2)} Shock:{#player_flag(3)}',
                 r=0.95, g=0.88, b=0.74),
        hud_line(100,
                 'Lives:{#player_lives()}  Score:{#player_score()}  Shield[5]:{shield_on}  Mirror[0]:{mirror_on}  Driver[9]:{#player_driver()}',
                 r=0.78, g=0.96, b=0.84),
        hud_line(124,
                 '[1]Start/SaveCP+Score [2]TeleportCP [3]NudgeUp [4]Noclip [5]Shield [6]KillPlain',
                 r=0.72, g=0.84, b=0.98),
        hud_line(144,
                 '[7]CrashKill [8]Burn [L]+1Life [K]5Lives/5000pts [9]Driver [0]Mirror [P]Hooks',
                 r=0.72, g=0.84, b=0.98),
    ]

    hud_page_1 = [
        hud_line(40,
                 '4x10 Window Origin:({#player_place_int(0)}, {#player_place_int(1)})  Cursor:({#player_place_int(2)}, {#player_place_int(3)})  Size:{#player_place_int(4)}x{#player_place_int(5)}',
                 r=0.82, g=0.95, b=1.0),
        hud_line(60,
                 'Cell At Cursor: code={#player_place_offset_code(0, 0)} ("{$player_place_here_text()}")  Under Feet: code={#player_place_offset_code(0, 1)}',
                 r=0.95, g=0.90, b=0.74),
        hud_line(80,
                 'Ask -> Footing:{#player_ask(AMOD_PLAYER_ASK_FOOTING)}  Climb:{#player_ask(AMOD_PLAYER_ASK_CLIMB)}  Left:{#player_ask(AMOD_PLAYER_ASK_LEFT)}  Right:{#player_ask(AMOD_PLAYER_ASK_RIGHT)}  Pipe:{#player_ask(AMOD_PLAYER_ASK_PIPE)}',
                 r=0.78, g=0.96, b=0.84),
        hud_line(100,
                 'MoveIntent: dX={mv_dx} dY={mv_dy}  LastStepExit:{#player_last_step_exit()}  HooksPaused[P]:{hooks_paused}',
                 r=0.88, g=0.86, b=0.98),
        hud_line(124,
                 'Hook Frame -> dt:{hk_dt} Pos:({hk_x}, {hk_y}, {hk_z}) Fall:{hk_fall} St:{hk_st}/{hk_sub}',
                 r=0.78, g=0.88, b=0.98),
    ]

    hud_page_2 = [
        hud_line(40,
                 'Events -> Died:{ev_died_cnt} Revived:{ev_rev_cnt} LivesChg:{ev_lives_cnt} ScoreChg:{ev_score_cnt} ShieldSaved:{ev_blocked_cnt}',
                 r=0.82, g=0.95, b=1.0),
        hud_line(60,
                 'Last EventInfo -> kind:{last_ev_kind} lives:{last_ev_lives}(was {last_ev_was_lives}) score:{last_ev_score}(was {last_ev_was_score}) crash:{last_ev_crashed}',
                 r=0.95, g=0.90, b=0.74),
        hud_line(80,
                 'Last DeathHook -> kind:{last_d_kind} lives:{last_d_lives} at ({last_d_x}, {last_d_y}, {last_d_z})',
                 r=0.96, g=0.80, b=0.80),
        hud_line(100,
                 'Hook Ticks -> Drive:{ev_drive_ticks}  OnFootState:{ev_state_ticks}  MoveIntent:{ev_move_ticks}  CondMask:{cond_mask}',
                 r=0.78, g=0.96, b=0.84),
    ]

    hud_children = [
        rule(actions=[
            act('render.rect', x=8, y=8, w=620, h=190, r=0.04, g=0.06, b=0.11, a=0.82),
            act('render.outline', x=8, y=8, w=620, h=190, thickness=1, r=0.38, g=0.74, b=0.98, a=0.90),
            hud_line(16,
                     'PLAYER API TEST [{st_here}] Page {#hud_page + 1}/3 ([F1] HUD [F2] Page)  Here:{#player_is_here()}',
                     r=1.0, g=0.90, b=0.45),
            hud_line(170, 'Last: {last_cmd}', r=0.98, g=0.86, b=0.46),
        ]),
        rule(conditions=[cond('var.compare', name='hud_page', op='==', value='0')],
             actions=hud_page_0,
             name='HUD Page 1: Position, State, Lives, Score & Hotkeys'),
        rule(conditions=[cond('var.compare', name='hud_page', op='==', value='1')],
             actions=hud_page_1,
             name='HUD Page 2: 4x10 Cell Window, Ask Queries & Hooks'),
        rule(conditions=[cond('var.compare', name='hud_page', op='==', value='2')],
             actions=hud_page_2,
             name='HUD Page 3: Event Counters & Captured Payloads'),
    ]

    hotkeys = [
        rule(event='input.key', filters={'key': '112'},
             actions=[act('var.toggle', name='hud_on')],
             name='F1: Toggle HUD'),

        rule(event='input.key', filters={'key': '113'},
             actions=[act('var.set', name='hud_page', value='=(hud_page + 1) % 3')],
             name='F2: Cycle HUD Page'),

        rule(event='input.key', filters={'key': '49'},
             children=[
                 rule(conditions=[cond('player.isHere', inverted=True)],
                      actions=[
                          act('world.startLevel', level='levels/level_1.txt'),
                          set_status('1: Started levels/level_1.txt'),
                      ]),
                 rule(conditions=[cond('player.isHere')],
                      actions=[
                          act('var.set', name='cp_x', value='=player_position(0)'),
                          act('var.set', name='cp_y', value='=player_position(1)'),
                          act('var.set', name='cp_z', value='=player_position(2)'),
                          act('var.set', name='cp_saved', value='1'),
                          act('player.addScore', points='250', where='0'),
                          act('player.addScore', points='500', where='1',
                              x='=player_position(0)', y='=player_position(1) + 0.35f', z='=player_position(2)'),
                          set_status('1: Saved checkpoint & spawned +250 / +500 score popups'),
                      ]),
             ],
             name='1: Start Level 1 (in menu) or Save Checkpoint + Score Popups (in room)'),

        rule(event='input.key', filters={'key': '50'},
             conditions=[cond('player.isHere'),
                         cond('var.on', name='cp_saved')],
             actions=[
                 act('player.setPosition', x='=cp_x', y='=cp_y', z='=cp_z'),
                 act('player.setFacing', by='0', dir='AMOD_PLAYER_FACING_RIGHT'),
                 set_status('2: Teleported to checkpoint (SetPosition + SetFacing Right)'),
             ],
             name='2: Teleport to Saved Checkpoint'),

        rule(event='input.key', filters={'key': '51'},
             conditions=[cond('player.isHere')],
             actions=[
                 act('player.nudgePosition',
                     x='=player_position(0)', y='=player_position(1) + 0.30f', z='=player_position(2)'),
                 act('player.setFacing', by='1', angle='1.5707963'),
                 set_status('3: Nudged player Y +0.30 (SetPositionOnly) & faced Left'),
             ],
             name='3: Nudge Upward (SetPositionOnly) + SetFacingAngle'),

        rule(event='input.key', filters={'key': '52'},
             conditions=[cond('player.isHere')],
             actions=[
                 act('player.setCollides', mode='2'),
                 set_status('4: Toggled room box collisions (Noclip)'),
             ],
             children=[
                 rule(conditions=[cond('input.keyDown', key='16')],
                      actions=[act('player.setCollides', mode='1')]),
             ],
             name='4: Toggle Room Box Collisions (Noclip)'),

        rule(event='input.key', filters={'key': '53'},
             actions=[
                 act('var.toggle', name='shield_on'),
                 set_status('5: Toggled Immortal Death-Veto Shield'),
             ],
             name='5: Toggle Before-Death Veto Shield'),

        rule(event='input.key', filters={'key': '54'},
             conditions=[cond('player.isHere')],
             actions=[
                 act('player.kill'),
                 set_status('6: Triggered Player.Kill (plain death)'),
             ],
             name='6: Kill Plain'),

        rule(event='input.key', filters={'key': '55'},
             conditions=[cond('player.isHere')],
             actions=[
                 act('player.kill', crashed='1'),
                 set_status('7: Triggered fall-crash death (Player.Kill crashed=1)'),
             ],
             name='7: Kill with Crash Kind'),

        rule(event='input.key', filters={'key': '56'},
             conditions=[cond('player.isHere')],
             actions=[
                 act('player.burn'),
                 set_status('8: Ignited fire death (Player.Burn)'),
             ],
             name='8: Burn Player'),

        rule(event='input.key', filters={'key': '76'},
             actions=[
                 act('player.addLives', delta='1'),
                 set_status('L: Added +1 life'),
             ],
             name='L: +1 Life'),

        rule(event='input.key', filters={'key': '75'},
             actions=[
                 act('player.setLives', lives='5'),
                 act('player.setScore', score='5000'),
                 set_status('K: Set lives = 5 and raw score = 5000 (silent)'),
             ],
             name='K: Set Lives = 5 & Raw Score = 5000'),

        rule(event='input.key', filters={'key': '57'},
             conditions=[cond('player.isHere')],
             actions=[act('var.set', name='drv_cycle', value='=(drv_cycle + 1) % 4')],
             children=[
                 rule(conditions=[cond('var.compare', name='drv_cycle', op='==', value='0')],
                      actions=[
                          act('player.releaseDriver'),
                          set_status('9: Driver = GAME (normal Player_Update)'),
                      ]),
                 rule(conditions=[cond('var.compare', name='drv_cycle', op='==', value='1')],
                      actions=[
                          act('player.setDriver', mode='AMOD_PLAYER_DRIVER_BOTH'),
                          set_status('9: Driver = BOTH (hold W in air for Jetpack lift!)'),
                      ]),
                 rule(conditions=[cond('var.compare', name='drv_cycle', op='==', value='2')],
                      actions=[
                          act('player.setDriver', mode='AMOD_PLAYER_DRIVER_MOD'),
                          set_status('9: Driver = MOD (custom Body_Step via Arrows + Space)'),
                      ]),
                 rule(conditions=[cond('var.compare', name='drv_cycle', op='==', value='3')],
                      actions=[
                          act('player.setDriver', mode='AMOD_PLAYER_DRIVER_NOBODY'),
                          set_status('9: Driver = NOBODY (player frozen in mid-air, zero gravity)'),
                      ]),
             ],
             name='9: Cycle Driver Mode (GAME -> BOTH -> MOD -> NOBODY)'),

        rule(event='input.key', filters={'key': '48'},
             actions=[
                 act('var.toggle', name='mirror_on'),
                 set_status('0: Toggled Mirror Horizontal Controls in MoveHandler'),
             ],
             name='0: Toggle Mirror Controls in SetMoveHandler'),

        rule(event='input.key', filters={'key': '80'},
             actions=[act('var.toggle', name='hooks_paused')],
             children=[
                 rule(conditions=[cond('var.on', name='hooks_paused')],
                      actions=[
                          act('player.clearMoveHandler'),
                          act('player.clearDeathHandler'),
                          act('player.clearStateHandler', state='AMOD_PLAYER_STATE_ONFOOT'),
                          set_status('P: Cleared Move, Death & OnFoot State handlers'),
                      ]),
                 rule(conditions=[cond('var.on', name='hooks_paused', inverted=True)],
                      actions=[
                          act('player.rearmHooks', which='0'),
                          set_status('P: Re-enabled all Player hooks'),
                      ]),
             ],
             name='P: Clear / Re-enable Move, Death & State Handlers'),
    ]

    rules = [
        rule(event='system.modInit',
             actions=[act('player.releaseDriver')],
             name='Startup: keep normal GAME driver until [9] is pressed'),

        rule(event='render.frame',
             children=[
                 rule(group=True, name='Evaluate all 16 Player conditions', collapsed=True,
                      children=cond_probes),
                 rule(conditions=[cond('var.on', name='hud_on')],
                      children=hud_children,
                      name='Draw 3-page PlayerApi HUD'),
             ],
             name='Frame Update & 3-Page Telemetry HUD'),

        rule(event='player.died',
             actions=[
                 act('var.add', name='ev_died_cnt', value='1'),
                 act('var.set', name='last_ev_kind', value='=player_event_int(0)'),
                 act('var.set', name='last_ev_lives', value='=player_event_int(1)'),
                 act('var.set', name='last_ev_crashed', value='=player_event_int(5)'),
             ],
             children=[
                 rule(conditions=[cond('var.on', name='cancel_probe')],
                      actions=[act('player.cancel')]),
             ],
             name='On Player Died'),

        rule(event='player.revived',
             actions=[
                 act('var.add', name='ev_rev_cnt', value='1'),
                 act('var.set', name='last_ev_kind', value='=player_event_int(0)'),
                 act('var.set', name='last_ev_lives', value='=player_event_int(1)'),
             ],
             name='On Player Revived'),

        rule(event='player.livesChanged',
             actions=[
                 act('var.add', name='ev_lives_cnt', value='1'),
                 act('var.set', name='last_ev_lives', value='=player_event_int(1)'),
                 act('var.set', name='last_ev_was_lives', value='=player_event_int(2)'),
             ],
             name='On Player Lives Changed'),

        rule(event='player.scoreChanged',
             actions=[
                 act('var.add', name='ev_score_cnt', value='1'),
                 act('var.set', name='last_ev_score', value='=player_event_int(3)'),
                 act('var.set', name='last_ev_was_score', value='=player_event_int(4)'),
             ],
             name='On Player Score Changed'),

        rule(event='player.beforeDeath',
             veto=True,
             actions=[
                 act('var.set', name='last_d_kind', value='=player_death_int(0)'),
                 act('var.set', name='last_d_lives', value='=player_death_int(1)'),
                 act('var.set', name='last_d_x', value='=player_death_pos(0)'),
                 act('var.set', name='last_d_y', value='=player_death_pos(1)'),
                 act('var.set', name='last_d_z', value='=player_death_pos(2)'),
             ],
             conditions=[cond('var.on', name='shield_on')],
             children=[
                 rule(actions=[
                     act('var.add', name='ev_blocked_cnt', value='1'),
                     set_status('Shield [5] vetoed player death!'),
                 ]),
                 rule(conditions=[cond('player.deathKindIs', kind='AMOD_PLAYER_DEATH_CRASHED')],
                      actions=[set_status('Shield [5] vetoed a FALL CRASH death!')]),
             ],
             name='Before Death (Veto when shield_on = 1)'),

        rule(event='player.drive',
             filters={'mode': 'AMOD_PLAYER_DRIVER_BOTH'},
             actions=[
                 act('var.add', name='ev_drive_ticks', value='1'),
                 act('var.set', name='hk_dt', value='=player_frame_num(0)'),
                 act('var.set', name='hk_x', value='=player_frame_num(1)'),
                 act('var.set', name='hk_y', value='=player_frame_num(2)'),
                 act('var.set', name='hk_z', value='=player_frame_num(3)'),
                 act('var.set', name='hk_fall', value='=player_frame_num(4)'),
                 act('var.set', name='hk_st', value='=player_frame_int(0)'),
                 act('var.set', name='hk_sub', value='=player_frame_int(1)'),
             ],
             children=[
                 rule(conditions=[cond('player.driverIs', mode='AMOD_PLAYER_DRIVER_BOTH'),
                                  cond('input.keyDown', key='87')],
                      actions=[
                          act('player.nudgePosition',
                              x='=player_position(0)',
                              y='=player_position(1) + 1.8f * player_frame_num(0)',
                              z='=player_position(2)'),
                      ],
                      name='BOTH mode: hold W for Jetpack lift'),

                 rule(conditions=[cond('player.driverIs', mode='AMOD_PLAYER_DRIVER_MOD')],
                      actions=[
                          act('var.set', name='step_dx', value='0'),
                          act('var.set', name='step_dy', value='0'),
                          act('var.set', name='step_jmp', value='0'),
                      ],
                      children=[
                          rule(conditions=[cond('input.keyDown', key='37')],
                               actions=[act('var.set', name='step_dx', value='-1')]),
                          rule(conditions=[cond('input.keyDown', key='39')],
                               actions=[act('var.set', name='step_dx', value='1')]),
                          rule(conditions=[cond('input.keyDown', key='38')],
                               actions=[act('var.set', name='step_dy', value='1')]),
                          rule(conditions=[cond('input.keyDown', key='40')],
                               actions=[act('var.set', name='step_dy', value='-1')]),
                          rule(conditions=[cond('input.keyDown', key='32')],
                               actions=[act('var.set', name='step_jmp', value='1')]),
                          rule(actions=[
                              act('player.step', dirX='=step_dx', dirY='=step_dy',
                                  jump='=step_jmp', delta='=player_frame_num(0)'),
                          ]),
                          rule(conditions=[cond('expr', text='player_last_step_exit() > 0')],
                               actions=[act('world.crossRoom', side='=player_last_step_exit()')]),
                      ],
                      name='MOD mode: custom Body_Step + room border crossing'),
             ],
             name='Drive Hook (BOTH jetpack & MOD custom Body_Step)'),

        rule(event='player.onState',
             filters={'state': 'AMOD_PLAYER_STATE_ONFOOT'},
             actions=[
                 act('var.add', name='ev_state_ticks', value='1'),
                 act('var.set', name='hk_dt', value='=player_frame_num(0)'),
             ],
             name='OnFoot State Hook'),

        rule(event='player.onMove',
             actions=[
                 act('var.add', name='ev_move_ticks', value='1'),
                 act('var.set', name='mv_dx', value='=player_move_dir(0)'),
                 act('var.set', name='mv_dy', value='=player_move_dir(1)'),
             ],
             children=[
                 rule(conditions=[cond('var.on', name='mirror_on')],
                      actions=[
                          act('player.setMoveIntent',
                              dirX='=-player_move_dir(0)', dirY='99'),
                          act('var.set', name='mv_dx', value='=player_move_dir(0)'),
                      ],
                      name='Mirror horizontal input when mirror_on = 1'),
             ],
             name='Move Intent Hook'),
    ] + hotkeys

    return {'name': 'main', 'vars': variables, 'rules': rules}


def collect_used(rules, evs, conds, acts, blobs):
    for r in rules:
        if r.get('event'):
            evs.add(r['event'])
        for v in r.get('filters', {}).values():
            blobs.append(str(v))
        for c in r.get('conditions', []):
            conds.add(c['id'])
            for v in c.get('args', {}).values():
                blobs.append(str(v))
        for a in r.get('actions', []):
            acts.add(a['id'])
            for v in a.get('args', {}).values():
                blobs.append(str(v))
        collect_used(r.get('children', []), evs, conds, acts, blobs)


def verify_coverage(sheet):
    evs, conds, acts, blobs = set(), set(), set(), []
    collect_used(sheet['rules'], evs, conds, acts, blobs)
    joined = '\n'.join(blobs)

    cat_evs = {e['id'] for e in json.load(open(os.path.join(PLAYER_PKG, 'events.json'), encoding='utf-8'))}
    cat_conds = {c['id'] for c in json.load(open(os.path.join(PLAYER_PKG, 'conditions.json'), encoding='utf-8'))}
    cat_acts = {a['id'] for a in json.load(open(os.path.join(PLAYER_PKG, 'actions.json'), encoding='utf-8'))}
    cat_exprs = json.load(open(os.path.join(PLAYER_PKG, 'expressions.json'), encoding='utf-8'))

    miss_e = sorted(cat_evs - evs)
    miss_c = sorted(cat_conds - conds)
    miss_a = sorted(cat_acts - acts)
    miss_x = sorted(x['id'] for x in cat_exprs if x['text'] not in joined)

    if miss_e or miss_c or miss_a or miss_x:
        print('MISSING player coverage in playertest:')
        if miss_e:
            print('  events:', miss_e)
        if miss_c:
            print('  conditions:', miss_c)
        if miss_a:
            print('  actions:', miss_a)
        if miss_x:
            print('  expressions:', miss_x)
        sys.exit(1)

    print('playertest coverage 100%%: %d events, %d conditions, %d actions, %d expressions' % (
        len(cat_evs), len(cat_conds), len(cat_acts), len(cat_exprs)))


def main():
    sheet = build_sheet()
    verify_coverage(sheet)

    manifest = {
        'api_version': current_api_version(),
        'author': 'AFModConstructor',
        'capabilities': [],
        'description': 'Interactive PlayerApi showcase (state, position, noclip, shield veto, custom driver, 4x10 window)',
        'entry': 'bin/playertest.dll',
        'game_version': '1.0',
        'id': 'playertest',
        'name': 'Player Showcase',
        'requires': ['fonts', 'input', 'player', 'render', 'world'],
        'version': '1.0.0',
    }

    project = {
        '@project': {
            '@build': '0',
            '@last.file': '',
            '@last.proj': '',
            '@name': 'Player Showcase',
            '@name.game': '',
            '@name.pak': '',
            '@path.game': '',
            '@path.pak': '',
            '@tier': 'tcc',
        }
    }

    os.makedirs(os.path.join(PROJECT, 'logic'), exist_ok=True)
    with open(os.path.join(PROJECT, 'logic', 'main.json'), 'w', encoding='utf-8', newline='\n') as f:
        json.dump(sheet, f, ensure_ascii=False, indent=2)
    with open(os.path.join(PROJECT, 'mod.json'), 'w', encoding='utf-8', newline='\n') as f:
        json.dump(manifest, f, ensure_ascii=False, indent=2)
    with open(os.path.join(PROJECT, 'playertest.azproj'), 'w', encoding='utf-8', newline='\n') as f:
        json.dump(project, f, ensure_ascii=False, indent=2)

    print('wrote', PROJECT)


if __name__ == '__main__':
    main()
