"""Writes examples/cameratest: an interactive in-game test of the entire Camera package.

Every event (3), condition (8), action (25), and expression (40) of packages/camera is wired
into a live HUD + hotkey showcase so every CameraApi call can be inspected and triggered in-game.

    python tools/make_cameratest.py
"""
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROJECT = os.path.join(ROOT, 'examples', 'cameratest')
CAM_PKG = os.path.join(ROOT, 'packages', 'camera', 'catalog')
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


def hud_line(y, text, r=0.90, g=0.93, b=0.98):
    return act('render.text', text=text, x=16, y=y, font='', scale=0.56, r=r, g=g, b=b, a=1)


def set_status(text):
    return act('var.setText', name='last_cmd', value=text)


def build_sheet():
    variables = [
        {'name': 'hud_on', 'type': 'bool', 'value': '1'},
        {'name': 'orbit_on', 'type': 'bool', 'value': '0'},
        {'name': 'orbit_angle', 'type': 'float', 'value': '0.0'},
        {'name': 'view_on', 'type': 'bool', 'value': '0'},
        {'name': 'freeze_on', 'type': 'bool', 'value': '0'},
        {'name': 'far_prop', 'type': 'bool', 'value': '0'},
        {'name': 'upd_ticks', 'type': 'int', 'value': '0'},
        {'name': 'upd_dt', 'type': 'float', 'value': '0.0'},
        {'name': 'view_ticks', 'type': 'int', 'value': '0'},
        {'name': 'snap_ticks', 'type': 'int', 'value': '0'},
        {'name': 'snap_x', 'type': 'float', 'value': '0.0'},
        {'name': 'snap_y', 'type': 'float', 'value': '0.0'},
        {'name': 'snap_z', 'type': 'float', 'value': '0.0'},
        {'name': 'last_cmd', 'type': 'text', 'value': 'Ready - enter a level to test'},
        {'name': 'st_here', 'type': 'text', 'value': 'NO'},
        {'name': 'st_shake', 'type': 'text', 'value': 'no'},
        {'name': 'st_feye', 'type': 'text', 'value': 'ON'},
        {'name': 'st_ftgt', 'type': 'text', 'value': 'ON'},
        {'name': 'st_free', 'type': 'text', 'value': 'off'},
        {'name': 'st_fx', 'type': 'text', 'value': 'NONE'},
        {'name': 'st_drv', 'type': 'text', 'value': 'BOTH'},
        {'name': 'st_prop', 'type': 'text', 'value': '<=6'},
    ]

    # 1.1: Live evaluation of all 8 Camera conditions
    cond_checks = [
        rule(conditions=[cond('camera.isHere')],
             actions=[act('var.setText', name='st_here', value='YES')]),
        rule(conditions=[cond('camera.isHere', inverted=True)],
             actions=[act('var.setText', name='st_here', value='NO')]),

        rule(conditions=[cond('camera.isShaking')],
             actions=[act('var.setText', name='st_shake', value='YES')]),
        rule(conditions=[cond('camera.isShaking', inverted=True)],
             actions=[act('var.setText', name='st_shake', value='no')]),

        rule(conditions=[cond('camera.followsEye')],
             actions=[act('var.setText', name='st_feye', value='ON')]),
        rule(conditions=[cond('camera.followsEye', inverted=True)],
             actions=[act('var.setText', name='st_feye', value='FROZEN')]),

        rule(conditions=[cond('camera.followsTarget')],
             actions=[act('var.setText', name='st_ftgt', value='ON')]),
        rule(conditions=[cond('camera.followsTarget', inverted=True)],
             actions=[act('var.setText', name='st_ftgt', value='FROZEN')]),

        rule(conditions=[cond('camera.isFreeMode')],
             actions=[act('var.setText', name='st_free', value='ON')]),
        rule(conditions=[cond('camera.isFreeMode', inverted=True)],
             actions=[act('var.setText', name='st_free', value='off')]),

        rule(conditions=[cond('camera.effectIs', effect='AMOD_CAMERA_EFFECT_NONE')],
             actions=[act('var.setText', name='st_fx', value='NONE')]),
        rule(conditions=[cond('camera.effectIs', effect='AMOD_CAMERA_EFFECT_ZOOM')],
             actions=[act('var.setText', name='st_fx', value='ZOOM')]),
        rule(conditions=[cond('camera.effectIs', effect='AMOD_CAMERA_EFFECT_ROCKING')],
             actions=[act('var.setText', name='st_fx', value='ROCKING')]),
        rule(conditions=[cond('camera.effectIs', effect='AMOD_CAMERA_EFFECT_ROTATE')],
             actions=[act('var.setText', name='st_fx', value='ROTATE')]),

        rule(conditions=[cond('camera.driverIs', driver='AMOD_CAMERA_DRIVER_GAME')],
             actions=[act('var.setText', name='st_drv', value='GAME')]),
        rule(conditions=[cond('camera.driverIs', driver='AMOD_CAMERA_DRIVER_MOD')],
             actions=[act('var.setText', name='st_drv', value='MOD')]),
        rule(conditions=[cond('camera.driverIs', driver='AMOD_CAMERA_DRIVER_BOTH')],
             actions=[act('var.setText', name='st_drv', value='BOTH')]),
        rule(conditions=[cond('camera.driverIs', driver='AMOD_CAMERA_DRIVER_NOBODY')],
             actions=[act('var.setText', name='st_drv', value='NOBODY')]),

        rule(conditions=[cond('camera.propIs', key='room_dist', op='>', value='6')],
             actions=[act('var.setText', name='st_prop', value='>6(FAR)')]),
        rule(conditions=[cond('camera.propIs', inverted=True, key='room_dist', op='>', value='6')],
             actions=[act('var.setText', name='st_prop', value='<=6(DEF)')]),
    ]

    # 1.2: HUD panel showing all 40 Camera expressions + condition states + hotkey legend
    hud_actions = [
        act('render.rect', x=8, y=8, w=624, h=182, r=0.04, g=0.05, b=0.09, a=0.78),
        act('render.outline', x=8, y=8, w=624, h=182, thickness=1, r=0.42, g=0.52, b=0.85, a=0.85),
        hud_line(14, '[CAMERA API TEST] {last_cmd}   [Tab: HUD]', 0.62, 0.84, 1.0),
        hud_line(28,
                 'Conds: here={st_here}({#(API->camera ? API->camera->IsHere(SELF) : 0)}) '
                 'drv={st_drv}({#(API->camera ? (int)API->camera->GetDriver(SELF) : 0)}) '
                 'fx={st_fx}({#(API->camera ? API->camera->GetEffect(SELF) : 0)}) '
                 'shake={st_shake} free={st_free}({#(API->camera ? API->camera->GetFreeMode(SELF) : 0)})',
                 0.78, 0.95, 0.78),
        hud_line(41,
                 'Follow: eye={st_feye} target={st_ftgt}  roll={~cam_roll()}  '
                 'orbit(F7)={orbit_on}  cinemaView(F8)={view_on}'),
        hud_line(54,
                 'Eye: ({~cam_vec(0, 0)}, {~cam_vec(0, 1)}, {~cam_vec(0, 2)})  '
                 'Target: ({~cam_vec(1, 0)}, {~cam_vec(1, 1)}, {~cam_vec(1, 2)})'),
        hud_line(67,
                 'Up: ({~cam_vec(2, 0)}, {~cam_vec(2, 1)}, {~cam_vec(2, 2)})  '
                 'Followed: ({~cam_vec(3, 0)}, {~cam_vec(3, 1)}, {~cam_vec(3, 2)})'),
        hud_line(80,
                 'Focus(player): ({~cam_focus(0)}, {~cam_focus(1)}, {~cam_focus(2)})'),
        hud_line(93,
                 'ViewEye: ({~cam_view(0, 0)}, {~cam_view(0, 1)}, {~cam_view(0, 2)})  '
                 'ViewTgt: ({~cam_view(1, 0)}, {~cam_view(1, 1)}, {~cam_view(1, 2)})'),
        hud_line(106,
                 'ViewUp: ({~cam_view(2, 0)}, {~cam_view(2, 1)}, {~cam_view(2, 2)})  '
                 'Lens: fov={~cam_lens(0)} asp={~cam_lens(1)} {~cam_lens(2)}..{~cam_lens(3)}'),
        hud_line(119,
                 'Props({#(API->camera ? API->camera->PropertyCount(SELF) : 0)}): '
                 '[0]={$cam_prop_name(0)}  room_dist={~cam_prop("room_dist")} ({st_prop})'),
        hud_line(132,
                 'Handlers: upd={upd_ticks}(dt={upd_dt})  view={view_ticks}  '
                 'snap={snap_ticks} at ({snap_x}, {snap_y}, {snap_z})',
                 0.96, 0.88, 0.65),
        hud_line(150,
                 '1:Shake 2:StopShake 3:Zoom 4:Rock 5:Rotate180 6:StopFx 7:Wide+Roll 8:TeleLens',
                 0.72, 0.78, 0.90),
        hud_line(163,
                 '9:FarDist/Reload 0:ResetAll P:Place F6:FreezeFollow F7:Orbit F8:View Shift+F9:Unhook',
                 0.72, 0.78, 0.90),
    ]

    # Top-level Rule 1: render.frame
    r_render = rule(
        event='render.frame',
        children=[
            rule(group=True, name='Проверка всех 8 условий камеры', collapsed=True,
                 children=cond_checks),
            rule(conditions=[cond('var.on', name='hud_on')],
                 actions=hud_actions),
        ],
    )

    # Top-level Rule 2: camera.update (with driver = BOTH so game updates first, then mod tweaks)
    r_cam_update = rule(
        event='camera.update',
        filters={'driver': 'AMOD_CAMERA_DRIVER_BOTH'},
        actions=[
            act('var.add', name='upd_ticks', value='1'),
            act('var.set', name='upd_dt', value='=(camFrame ? camFrame->delta : 0.0f)'),
        ],
        children=[
            rule(
                conditions=[cond('camera.isHere'), cond('var.on', name='orbit_on')],
                actions=[
                    act('var.add', name='orbit_angle',
                        value='=(camFrame ? camFrame->delta : 0.0f) * 1.6f'),
                    act('camera.setEye',
                        x='=cam_focus(0) + sinf(orbit_angle) * 2.2f',
                        y='=cam_focus(1) + 1.0f',
                        z='=cam_focus(2) + 5.2f'),
                    act('camera.setTarget',
                        x='=cam_focus(0)',
                        y='=cam_focus(1)',
                        z='=cam_focus(2)'),
                    act('camera.setUp',
                        x='=sinf(orbit_angle) * 0.12f',
                        y='1',
                        z='0'),
                ],
            ),
        ],
    )

    # Top-level Rule 3: camera.view (overrides rendered view matrix without altering gameplay camera)
    r_cam_view = rule(
        event='camera.view',
        actions=[act('var.add', name='view_ticks', value='1')],
        children=[
            rule(
                conditions=[cond('camera.isHere'), cond('var.on', name='view_on')],
                actions=[
                    act('camera.viewSetEye',
                        x='=cam_focus(0)',
                        y='=cam_focus(1) + 3.0f',
                        z='=cam_focus(2) + 6.8f'),
                    act('camera.viewSetTarget',
                        x='=cam_focus(0)',
                        y='=cam_focus(1) - 0.2f',
                        z='=cam_focus(2)'),
                    act('camera.viewSetUp', x='0', y='1', z='0'),
                ],
            ),
        ],
    )

    # Top-level Rule 4: camera.placing (fires when level/room snaps camera to start point)
    r_cam_placing = rule(
        event='camera.placing',
        actions=[
            act('var.add', name='snap_ticks', value='1'),
            act('var.set', name='snap_x', value='=(info ? info->point[0] : 0.0f)'),
            act('var.set', name='snap_y', value='=(info ? info->point[1] : 0.0f)'),
            act('var.set', name='snap_z', value='=(info ? info->point[2] : 0.0f)'),
            set_status('Event: camera.placing fired'),
        ],
    )

    # Group 5: Interactive hotkeys exercising every Camera action
    hotkeys = [
        # Tab (9): toggle HUD
        rule(event='input.key', filters={'key': '9'},
             actions=[act('var.toggle', name='hud_on')]),

        # 1 (49): StartShake
        rule(event='input.key', filters={'key': '49'},
             conditions=[cond('camera.isHere')],
             actions=[act('camera.shake'), set_status('1: StartShake')]),

        # 2 (50): StopShake
        rule(event='input.key', filters={'key': '50'},
             conditions=[cond('camera.isHere')],
             actions=[act('camera.stopShake'), set_status('2: StopShake')]),

        # 3 (51): StartEffect(ZOOM)
        rule(event='input.key', filters={'key': '51'},
             conditions=[cond('camera.isHere')],
             actions=[act('camera.startEffect', effect='AMOD_CAMERA_EFFECT_ZOOM'),
                      set_status('3: StartEffect(ZOOM)')]),

        # 4 (52): StartEffect(ROCKING)
        rule(event='input.key', filters={'key': '52'},
             conditions=[cond('camera.isHere')],
             actions=[act('camera.startEffect', effect='AMOD_CAMERA_EFFECT_ROCKING'),
                      set_status('4: StartEffect(ROCKING)')]),

        # 5 (53): StartEffect(ROTATE)
        rule(event='input.key', filters={'key': '53'},
             conditions=[cond('camera.isHere')],
             actions=[act('camera.startEffect', effect='AMOD_CAMERA_EFFECT_ROTATE'),
                      set_status('5: StartEffect(ROTATE 180)')]),

        # 6 (54): StopEffect
        rule(event='input.key', filters={'key': '54'},
             conditions=[cond('camera.isHere')],
             actions=[act('camera.stopEffect'), set_status('6: StopEffect')]),

        # 7 (55): Wide FOV + clip planes + camera roll
        rule(event='input.key', filters={'key': '55'},
             conditions=[cond('camera.isHere')],
             actions=[
                 act('camera.setFov', fov='1.30'),
                 act('camera.setPlanes', near='0.05', far='18'),
                 act('camera.setRoll', angle='0.18'),
                 set_status('7: Wide FOV 1.30 + Planes 0.05..18 + Roll 0.18'),
             ]),

        # 8 (56): Full SetLens (telephoto)
        rule(event='input.key', filters={'key': '56'},
             conditions=[cond('camera.isHere')],
             actions=[
                 act('camera.setLens', fov='0.72', aspect='1.3333', near='0.1', far='12'),
                 set_status('8: SetLens(0.72, 1.333, 0.1..12)'),
             ]),

        # 9 (57): Toggle room_dist property (SetProperty vs Reload camera.txt)
        rule(event='input.key', filters={'key': '57'},
             conditions=[cond('camera.isHere')],
             actions=[act('var.toggle', name='far_prop')],
             children=[
                 rule(conditions=[cond('var.on', name='far_prop')],
                      actions=[act('camera.setProperty', key='room_dist', value='8.5'),
                               set_status('9: SetProperty room_dist = 8.5')]),
                 rule(conditions=[cond('var.on', name='far_prop', inverted=True)],
                      actions=[act('camera.reload'),
                               set_status('9: Reload scripts/camera.txt')]),
             ]),

        # 0 (48): Reset lens, roll, shake, effects, properties, follow & modes
        rule(event='input.key', filters={'key': '48'},
             actions=[
                 act('var.set', name='orbit_on', value='0'),
                 act('var.set', name='view_on', value='0'),
                 act('var.set', name='freeze_on', value='0'),
                 act('var.set', name='far_prop', value='0'),
                 act('camera.clearLens'),
                 act('camera.setRoll', angle='0'),
                 act('camera.setUp', x='0', y='1', z='0'),
                 act('camera.stopShake'),
                 act('camera.stopEffect'),
                 act('camera.reload'),
                 act('camera.setFollowEye', follows='1'),
                 act('camera.setFollowTarget', follows='1'),
                 act('camera.setDriver', driver='AMOD_CAMERA_DRIVER_BOTH'),
                 act('camera.follow',
                     x='=cam_focus(0)', y='=cam_focus(1)', z='=cam_focus(2)'),
                 set_status('0: Reset all camera overrides'),
             ]),

        # P (80): Instant snap camera onto player focus (Camera.Place)
        rule(event='input.key', filters={'key': '80'},
             conditions=[cond('camera.isHere')],
             actions=[
                 act('camera.place',
                     x='=cam_focus(0)', y='=cam_focus(1)', z='=cam_focus(2)'),
                 set_status('P: Place camera on player focus'),
             ]),

        # F6 (117): Freeze / unfreeze eye & target following
        rule(event='input.key', filters={'key': '117'},
             conditions=[cond('camera.isHere')],
             actions=[act('var.toggle', name='freeze_on')],
             children=[
                 rule(conditions=[cond('var.on', name='freeze_on')],
                      actions=[
                          act('camera.setFollowEye', follows='0'),
                          act('camera.setFollowTarget', follows='0'),
                          set_status('F6: Follow FROZEN (walk around)'),
                      ]),
                 rule(conditions=[cond('var.on', name='freeze_on', inverted=True)],
                      actions=[
                          act('camera.setFollowEye', follows='1'),
                          act('camera.setFollowTarget', follows='1'),
                          act('camera.follow',
                              x='=cam_focus(0)', y='=cam_focus(1)', z='=cam_focus(2)'),
                          set_status('F6: Follow RESTORED'),
                      ]),
             ]),

        # F7 (118): Toggle smooth orbit in camera.update
        rule(event='input.key', filters={'key': '118'},
             conditions=[cond('camera.isHere')],
             actions=[act('var.toggle', name='orbit_on')],
             children=[
                 rule(conditions=[cond('var.on', name='orbit_on')],
                      actions=[
                          act('camera.setDriver', driver='AMOD_CAMERA_DRIVER_BOTH'),
                          set_status('F7: Orbit in camera.update ON'),
                      ]),
                 rule(conditions=[cond('var.on', name='orbit_on', inverted=True)],
                      actions=[
                          act('camera.setUp', x='0', y='1', z='0'),
                          act('camera.follow',
                              x='=cam_focus(0)', y='=cam_focus(1)', z='=cam_focus(2)'),
                          set_status('F7: Orbit OFF'),
                      ]),
             ]),

        # F8 (119): Toggle cinema view override in camera.view
        rule(event='input.key', filters={'key': '119'},
             conditions=[cond('camera.isHere')],
             actions=[
                 act('var.toggle', name='view_on'),
                 set_status('F8: Toggled camera.view override'),
             ]),

        # Shift + F9 (120 with Shift 16 held): Clear all three camera handlers + set driver GAME
        rule(event='input.key', filters={'key': '120'},
             conditions=[cond('input.keyDown', key='16')],
             actions=[
                 act('camera.clearUpdateHandler'),
                 act('camera.clearViewHandler'),
                 act('camera.clearEventHandler'),
                 act('camera.setDriver', driver='AMOD_CAMERA_DRIVER_GAME'),
                 set_status('Shift+F9: Cleared all camera handlers'),
             ]),
    ]

    r_hotkeys = rule(
        group=True,
        name='Горячие клавиши управления камерой',
        collapsed=False,
        children=hotkeys,
    )

    return {
        'name': 'main',
        'vars': variables,
        'rules': [r_render, r_cam_update, r_cam_view, r_cam_placing, r_hotkeys],
    }


def collect_used(sheet):
    events, conditions, actions = set(), set(), set()
    blobs = []

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

    for item in json.load(open(os.path.join(CAM_PKG, 'events.json'), encoding='utf-8')):
        if item['id'] not in events:
            missing.append('event ' + item['id'])
    for item in json.load(open(os.path.join(CAM_PKG, 'conditions.json'), encoding='utf-8')):
        if item['id'] not in conditions:
            missing.append('condition ' + item['id'])
    for item in json.load(open(os.path.join(CAM_PKG, 'actions.json'), encoding='utf-8')):
        if item['id'] not in actions:
            missing.append('action ' + item['id'])
    for item in json.load(open(os.path.join(CAM_PKG, 'expressions.json'), encoding='utf-8')):
        if item['text'] not in text_blob:
            missing.append('expression %s (%s)' % (item['id'], item['text']))

    return missing


def main():
    sheet = build_sheet()
    missing = verify_coverage(sheet)
    if missing:
        print('НЕ ПОКРЫТО В CAMERATEST:')
        for m in missing:
            print('  -', m)
        return 1

    manifest = {
        'api_version': current_api_version(),
        'author': 'AFModConstructor',
        'capabilities': [],
        'description': 'Interactive showcase of all CameraApi events, conditions, actions and expressions',
        'entry': 'bin/cameratest.dll',
        'game_version': '1.0',
        'id': 'cameratest',
        'name': 'cameratest',
        'requires': ['camera', 'input', 'render'],
        'version': '1.0.0',
    }
    project = {
        '@project': {
            '@build': '0',
            '@last.file': '',
            '@last.proj': '',
            '@name': 'cameratest',
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

    ev_n = len(json.load(open(os.path.join(CAM_PKG, 'events.json'), encoding='utf-8')))
    cd_n = len(json.load(open(os.path.join(CAM_PKG, 'conditions.json'), encoding='utf-8')))
    ac_n = len(json.load(open(os.path.join(CAM_PKG, 'actions.json'), encoding='utf-8')))
    ex_n = len(json.load(open(os.path.join(CAM_PKG, 'expressions.json'), encoding='utf-8')))
    print('cameratest готов: событий %d/%d, условий %d/%d, действий %d/%d, выражений %d/%d'
          % (ev_n, ev_n, cd_n, cd_n, ac_n, ac_n, ex_n, ex_n))
    return 0


if __name__ == '__main__':
    sys.exit(main())
