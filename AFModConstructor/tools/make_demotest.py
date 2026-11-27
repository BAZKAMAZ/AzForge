"""Writes examples/demotest: an interactive in-game showcase of the entire Demo package.

Every event (3), condition (8), action (20), and expression (55) of packages/demo is wired
into a live HUD + hotkey showcase so every DemoApi call can be inspected and triggered in-game.

    python tools/make_demotest.py
"""
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROJECT = os.path.join(ROOT, 'examples', 'demotest')
DEMO_PKG = os.path.join(ROOT, 'packages', 'demo', 'catalog')
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
    return act('render.textAligned', text=text, x=18, y=y, font='', scale=0.45, align='0x08', r=r, g=g, b=b, a=1)


def set_status(text):
    return act('var.setText', name='last_cmd', value=text)


def build_sheet():
    variables = [
        {'name': 'hud_on', 'type': 'bool', 'value': '1'},
        {'name': 'override_in', 'type': 'bool', 'value': '0'},
        {'name': 'suppress_on', 'type': 'bool', 'value': '0'},
        {'name': 'cancel_probe', 'type': 'bool', 'value': '0'},
        {'name': 'ev_state_cnt', 'type': 'int', 'value': '0'},
        {'name': 'ev_last_kind', 'type': 'int', 'value': '0'},
        {'name': 'ev_last_scr', 'type': 'int', 'value': '-1'},
        {'name': 'ev_move_cnt', 'type': 'int', 'value': '0'},
        {'name': 'ev_last_mv', 'type': 'int', 'value': '-1'},
        {'name': 'ev_in_cnt', 'type': 'int', 'value': '0'},
        {'name': 'in_x', 'type': 'float', 'value': '0.0'},
        {'name': 'in_y', 'type': 'float', 'value': '0.0'},
        {'name': 'in_z', 'type': 'float', 'value': '0.0'},
        {'name': 'in_h', 'type': 'int', 'value': '0'},
        {'name': 'in_v', 'type': 'int', 'value': '0'},
        {'name': 'in_j', 'type': 'int', 'value': '0'},
        {'name': 'in_dt', 'type': 'float', 'value': '0.0'},
        {'name': 'st_play', 'type': 'text', 'value': 'NO'},
        {'name': 'st_has_cam', 'type': 'text', 'value': 'no'},
        {'name': 'st_has_plc', 'type': 'text', 'value': 'no'},
        {'name': 'st_elap', 'type': 'text', 'value': '<=1s'},
        {'name': 'last_cmd', 'type': 'text', 'value': 'Ready - press 1 to author & play a demo'},
    ]

    cond_checks = [
        rule(conditions=[cond('demo.isPlaying')],
             actions=[act('var.setText', name='st_play', value='PLAYING')]),
        rule(conditions=[cond('demo.isPlaying', inverted=True)],
             actions=[act('var.setText', name='st_play', value='idle')]),

        rule(conditions=[cond('demo.countIs', op='>', value='0'),
                         cond('demo.hasCameraStart', script='-1')],
             actions=[act('var.setText', name='st_has_cam', value='YES')]),
        rule(conditions=[cond('demo.hasCameraStart', inverted=True, script='-1')],
             actions=[act('var.setText', name='st_has_cam', value='no')]),

        rule(conditions=[cond('demo.countIs', op='>', value='0'),
                         cond('demo.hasPlace', script='-1')],
             actions=[act('var.setText', name='st_has_plc', value='YES')]),
        rule(conditions=[cond('demo.hasPlace', inverted=True, script='-1')],
             actions=[act('var.setText', name='st_has_plc', value='no')]),

        rule(conditions=[cond('demo.currentIs', op='>=', value='0'),
                         cond('demo.moveCountIs', script='-1', op='>', value='0'),
                         cond('demo.moveIndexIs', op='>=', value='0'),
                         cond('demo.elapsedIs', op='>', seconds='1')],
             actions=[act('var.setText', name='st_elap', value='>1s')]),
        rule(conditions=[cond('demo.elapsedIs', inverted=True, op='>', seconds='1')],
             actions=[act('var.setText', name='st_elap', value='<=1s')]),
    ]

    hud_children = [
        rule(actions=[
            act('render.rect', x=10, y=10, w=620, h=222, r=0.05, g=0.07, b=0.11, a=0.78),
            act('render.outline', x=10, y=10, w=620, h=222, thickness=1, r=0.35, g=0.68, b=0.95, a=0.90),
            hud_line(15,
                     'DEMO API TEST [F1 HUD]  State:{st_play}({#(API->demo ? API->demo->IsPlaying(SELF) : 0)})  '
                     'Scripts:{#(API->demo ? API->demo->Count(SELF) : 0)}  '
                     'Cur:{#(API->demo ? API->demo->Current(SELF) : -1)}  '
                     'LastAdd:{#demo_last_added()}',
                     0.45, 0.86, 1.0),
            hud_line(31,
                     'Elapsed:{~(API->demo ? API->demo->Elapsed(SELF) : 0.0f)}({st_elap})  '
                     'Break:{~demo_break_time(-1)}s  '
                     'Move:{#(API->demo ? API->demo->MoveIndex(SELF) : -1)}/{#demo_move_count(-1)}'),
            hud_line(47,
                     'MoveCmd pause:{~demo_move_num(0)} end:{~demo_move_num(1)} '
                     'H:{#demo_move_int(0)} V:{#demo_move_int(1)} J:{#demo_move_int(2)} '
                     'lists(T:{#demo_move_int(3)} V:{#demo_move_int(4)} C:{#demo_move_int(5)})'),
            hud_line(63,
                     'CamCmd pos:({~demo_cam_num(0, 0)}, {~demo_cam_num(0, 1)}, {~demo_cam_num(0, 2)}) '
                     'spd:({~demo_cam_num(0, 3)}, {~demo_cam_num(0, 4)}, {~demo_cam_num(0, 5)}) '
                     't:{~demo_cam_num(0, 6)} auto:{#demo_cam_auto(0)}'),
            hud_line(79,
                     'ViewCmd pos:({~demo_cam_num(1, 0)}, {~demo_cam_num(1, 1)}, {~demo_cam_num(1, 2)}) '
                     'spd:({~demo_cam_num(1, 3)}, {~demo_cam_num(1, 4)}, {~demo_cam_num(1, 5)}) '
                     't:{~demo_cam_num(1, 6)} auto:{#demo_cam_auto(1)}'),
            hud_line(95,
                     'TimeCmd pause:{~demo_time_num(0)} factor:{~demo_time_num(1)} dur:{~demo_time_num(2)}  '
                     'CamStart({st_has_cam}):({~demo_cam_start_num(0, 0)}, {~demo_cam_start_num(0, 1)}, {~demo_cam_start_num(0, 2)})'),
            hud_line(111,
                     'StartView:({~demo_cam_start_num(1, 0)}, {~demo_cam_start_num(1, 1)}, {~demo_cam_start_num(1, 2)})  '
                     'Place({st_has_plc}):plr({#demo_place_int(0)}, {#demo_place_int(1)}) '
                     'rm({#demo_place_int(2)}, {#demo_place_int(3)})'),
            hud_line(127,
                     'Callbacks State:{ev_state_cnt}(kind:{ev_last_kind} scr:{ev_last_scr})  '
                     'Move:{ev_move_cnt}(mv:{ev_last_mv})  Input:{ev_in_cnt}'),
            hud_line(143,
                     'StepInput pos:({in_x}, {in_y}, {in_z}) H:{in_h} V:{in_v} J:{in_j} dt:{in_dt}  '
                     'Override[7]:{override_in} Suppress[8]:{suppress_on}'),
            hud_line(163,
                     '[1]Build+PlayDemo [2]Load demo_003 [3]PlayLast [4]Next [5]Stop',
                     0.75, 0.92, 0.78),
            hud_line(179,
                     '[6]EditScript0 [7]OverrideInput [8]SuppressToggle [9]RemoveAdded [0]Cursor0',
                     0.75, 0.92, 0.78),
            hud_line(213, 'Last: {last_cmd}', 1.0, 0.88, 0.45),
        ], name='Draw telemetry HUD'),
    ]

    hotkeys = [
        rule(event='input.key', filters={'key': '112'},
             actions=[act('var.toggle', name='hud_on')],
             name='F1: Toggle HUD'),

        rule(event='input.key', filters={'key': '49'},
             actions=[
                 act('demo.create'),
                 act('demo.setBreakTime', script='-1', seconds='0.25'),
                 act('demo.setCameraStart', script='-1',
                     cx='=player_position(0)',
                     cy='=player_position(1) + 0.8f',
                     cz='=player_position(2) + 4.2f',
                     vx='=player_position(0)',
                     vy='=player_position(1)',
                     vz='=player_position(2)'),
                 act('demo.addMove', script='-1', pause='0.4',
                     horz='1', vert='0', jump='0',
                     endCoord='=player_position(0) + 0.65f'),
                 act('demo.addCamera', script='-1', move='-1',
                     x='=player_position(0) + 0.6f',
                     y='=player_position(1) + 1.1f',
                     z='=player_position(2) + 3.8f',
                     sx='0', sy='0', sz='0', time='1.8', giveBack='1'),
                 act('demo.addView', script='-1', move='-1',
                     x='=player_position(0) + 0.3f',
                     y='=player_position(1) + 0.1f',
                     z='=player_position(2)',
                     sx='0', sy='0', sz='0', time='1.8', giveBack='1'),
                 act('demo.addTime', script='-1', move='-1',
                     pause='0.0', factor='0.75', time='1.2'),
                 act('demo.addMove', script='-1', pause='0.2',
                     horz='-1', vert='0', jump='1',
                     endCoord='=player_position(0) - 0.35f'),
                 act('demo.play', script='-1'),
                 set_status('1: Created runtime demo (2 moves + cam/view/time) & started'),
             ],
             name='1: Author Runtime Demo & Play'),

        rule(event='input.key', filters={'key': '50'},
             actions=[
                 act('demo.add', path='levels/rooms_0/demo_003.txt'),
                 set_status('2: Added levels/rooms_0/demo_003.txt'),
             ],
             name='2: Load Built-in demo_003.txt'),

        rule(event='input.key', filters={'key': '51'},
             actions=[
                 act('demo.play', script='-1'),
                 set_status('3: Playing selected / last-added demo script'),
             ],
             name='3: Play Selected / Last-Added Script'),

        rule(event='input.key', filters={'key': '52'},
             actions=[
                 act('demo.next'),
                 set_status('4: Advanced to next demo script'),
             ],
             name='4: Next Demo Script'),

        rule(event='input.key', filters={'key': '53'},
             actions=[
                 act('demo.stop'),
                 set_status('5: Stopped running demo'),
             ],
             name='5: Stop Running Demo'),

        rule(event='input.key', filters={'key': '54'},
             conditions=[cond('demo.countIs', op='>', value='0')],
             actions=[
                 act('demo.setMove', script='-1', move='0', pause='0.3',
                     horz='1', vert='0', jump='1',
                     endCoord='=player_position(0) + 0.5f'),
                 act('demo.setCamera', script='-1', move='0', index='0',
                     x='=player_position(0)',
                     y='=player_position(1) + 1.2f',
                     z='=player_position(2) + 4.5f',
                     sx='0', sy='0', sz='0', time='2.0', giveBack='1'),
                 act('demo.setView', script='-1', move='0', index='0',
                     x='=player_position(0)',
                     y='=player_position(1)',
                     z='=player_position(2)',
                     sx='0', sy='0', sz='0', time='2.0', giveBack='1'),
                 act('demo.setTime', script='-1', move='0', index='0',
                     pause='0', factor='0.6', time='1.2'),
                 set_status('6: Edited move[0] + cam[0]/view[0]/time[0] of selected script'),
             ],
             name='6: Edit Move/Camera/View/Time Records in Place'),

        rule(event='input.key', filters={'key': '55'},
             actions=[
                 act('var.toggle', name='override_in'),
                 set_status('7: Toggled live OnInput override (jump + move right)'),
             ],
             name='7: Toggle OnInput Override'),

        rule(event='input.key', filters={'key': '56'},
             actions=[
                 act('var.toggle', name='suppress_on'),
             ],
             children=[
                 rule(conditions=[cond('var.on', name='suppress_on')],
                      actions=[act('demo.suppressLevelDemos', suppress='1'),
                               set_status('8: SuppressLevelDemos = ON (applies on next level load)')]),
                 rule(conditions=[cond('var.on', name='suppress_on', inverted=True)],
                      actions=[act('demo.suppressLevelDemos', suppress='0'),
                               set_status('8: SuppressLevelDemos = OFF')]),
             ],
             name='8: Toggle SuppressLevelDemos'),

        rule(event='input.key', filters={'key': '57'},
             actions=[
                 act('demo.remove', script='-1'),
                 set_status('9: Removed last mod-added demo script'),
             ],
             name='9: Remove Last Mod-Added Demo Script'),

        rule(event='input.key', filters={'key': '48'},
             actions=[
                 act('demo.selectCursor', script='0', move='0', index='0'),
                 set_status('0: Selected cursor (script 0, move 0, item 0)'),
             ],
             name='0: Reset Inspection Cursor to Script 0'),
    ]

    rules = [
        rule(event='render.frame', name='Frame update & HUD', children=[
            rule(group=True, name='Evaluate conditions', collapsed=True, children=cond_checks),
            rule(conditions=[cond('var.on', name='hud_on')],
                 children=hud_children,
                 name='HUD when enabled'),
        ]),

        rule(event='demo.stateChanged', filters={'which': '0'},
             name='Track demo started / ended / broken',
             actions=[
                 act('var.add', name='ev_state_cnt', value='1'),
                 act('var.set', name='ev_last_kind', value='=demo_ev_int(0)'),
                 act('var.set', name='ev_last_scr', value='=demo_ev_int(1)'),
             ],
             children=[
                 rule(conditions=[cond('var.on', name='cancel_probe')],
                      actions=[act('demo.cancel')],
                      name='Optional unsubscribe probe'),
             ]),

        rule(event='demo.moveStarted', name='Track $move_cmd transitions', actions=[
            act('var.add', name='ev_move_cnt', value='1'),
            act('var.set', name='ev_last_mv', value='=demo_ev_int(2)'),
        ]),

        rule(event='demo.input', name='Inspect & optionally override demo step input', actions=[
            act('var.add', name='ev_in_cnt', value='1'),
            act('var.set', name='in_x', value='=demo_in_num(0)'),
            act('var.set', name='in_y', value='=demo_in_num(1)'),
            act('var.set', name='in_z', value='=demo_in_num(2)'),
            act('var.set', name='in_h', value='=demo_in_int(0)'),
            act('var.set', name='in_v', value='=demo_in_int(1)'),
            act('var.set', name='in_j', value='=demo_in_int(2)'),
            act('var.set', name='in_dt', value='=demo_in_num(3)'),
        ], children=[
            rule(conditions=[cond('var.on', name='override_in')],
                 actions=[act('demo.setInput', horz='1', vert='0', jump='1')],
                 name='Override step input when toggle 7 is on'),
        ]),
    ] + hotkeys

    return {'name': 'main', 'vars': variables, 'rules': rules}


def collect_used(sheet):
    events, conditions, actions, blobs = set(), set(), set(), []

    def walk(r):
        if r.get('event'):
            events.add(r['event'])
        for c in r.get('conditions', []):
            conditions.add(c['id'])
            blobs.extend(c.get('args', {}).values())
        for a in r.get('actions', []):
            actions.add(a['id'])
            blobs.extend(a.get('args', {}).values())
        for child in r.get('children', []):
            walk(child)

    for top in sheet['rules']:
        walk(top)
    return events, conditions, actions, '\n'.join(blobs)


def verify_coverage(sheet):
    events, conditions, actions, text_blob = collect_used(sheet)
    missing = []

    for item in json.load(open(os.path.join(DEMO_PKG, 'events.json'), encoding='utf-8')):
        if item['id'] not in events:
            missing.append('event ' + item['id'])
    for item in json.load(open(os.path.join(DEMO_PKG, 'conditions.json'), encoding='utf-8')):
        if item['id'] not in conditions:
            missing.append('condition ' + item['id'])
    for item in json.load(open(os.path.join(DEMO_PKG, 'actions.json'), encoding='utf-8')):
        if item['id'] not in actions:
            missing.append('action ' + item['id'])
    for item in json.load(open(os.path.join(DEMO_PKG, 'expressions.json'), encoding='utf-8')):
        if item['text'] not in text_blob:
            missing.append('expression %s (%s)' % (item['id'], item['text']))

    return missing


def main():
    sheet = build_sheet()
    missing = verify_coverage(sheet)
    if missing:
        print('MISSING IN DEMOTEST:')
        for m in missing:
            print('  -', m)
        return 1

    manifest = {
        'api_version': current_api_version(),
        'author': 'AFModConstructor',
        'capabilities': [],
        'description': 'Interactive showcase of all DemoApi events, conditions, actions and expressions',
        'entry': 'bin/demotest.dll',
        'game_version': '1.0',
        'id': 'demotest',
        'name': 'demotest',
        'requires': ['demo', 'input', 'player', 'render'],
        'version': '1.0.0',
    }
    project = {
        '@project': {
            '@build': '0',
            '@last.file': '',
            '@last.proj': '',
            '@name': 'demotest',
            '@name.game': '',
            '@name.pak': '',
            '@path.game': '',
            '@proj.dir': '',
            '@type': '1',
        }
    }

    os.makedirs(os.path.join(PROJECT, 'logic'), exist_ok=True)
    for path, data in (
        (os.path.join(PROJECT, 'logic', 'main.json'), sheet),
        (os.path.join(PROJECT, 'mod.json'), manifest),
        (os.path.join(PROJECT, 'proj.json'), project),
    ):
        with open(path, 'w', encoding='utf-8', newline='') as f:
            json.dump(data, f, ensure_ascii=False, indent=4)
            f.write('\n')

    ev_n = len(json.load(open(os.path.join(DEMO_PKG, 'events.json'), encoding='utf-8')))
    cd_n = len(json.load(open(os.path.join(DEMO_PKG, 'conditions.json'), encoding='utf-8')))
    ac_n = len(json.load(open(os.path.join(DEMO_PKG, 'actions.json'), encoding='utf-8')))
    ex_n = len(json.load(open(os.path.join(DEMO_PKG, 'expressions.json'), encoding='utf-8')))
    print('demotest ready: events %d/%d, conditions %d/%d, actions %d/%d, expressions %d/%d'
          % (ev_n, ev_n, cd_n, cd_n, ac_n, ac_n, ex_n, ex_n))
    return 0


if __name__ == '__main__':
    sys.exit(main())
