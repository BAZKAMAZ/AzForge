"""Writes examples/monsterstest: an interactive in-game showcase of the entire Monsters package.

Every event (10), condition (15), action (39), and expression (134) of packages/monsters is wired
into a 3-page live HUD + hotkey showcase so all 59 MonstersApi calls can be tested in-game.

    python tools/make_monsterstest.py
"""
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROJECT = os.path.join(ROOT, 'examples', 'monsterstest')
MONSTERS_PKG = os.path.join(ROOT, 'packages', 'monsters', 'catalog')
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
        {'name': 'type_cursor', 'type': 'int', 'value': '0'},
        {'name': 'kill_mode', 'type': 'int', 'value': '0'},
        {'name': 'touch_mode', 'type': 'int', 'value': '0'},
        {'name': 'behav_active', 'type': 'bool', 'value': '1'},
        {'name': 'block_place', 'type': 'bool', 'value': '0'},
        {'name': 'cancel_probe', 'type': 'bool', 'value': '0'},
        {'name': 'remove_toggle', 'type': 'bool', 'value': '0'},
        {'name': 'kill_key_toggle', 'type': 'bool', 'value': '0'},
        {'name': 'p_col', 'type': 'int', 'value': '14'},
        {'name': 'p_row', 'type': 'int', 'value': '10'},
        {'name': 'sp_col', 'type': 'int', 'value': '16'},
        {'name': 'cond_mask', 'type': 'int', 'value': '0'},
        {'name': 'loop_live_cnt', 'type': 'int', 'value': '0'},
        {'name': 'loop_pl_cnt', 'type': 'int', 'value': '0'},
        {'name': 'first_id', 'type': 'int', 'value': '0'},
        {'name': 'ev_kill_cnt', 'type': 'int', 'value': '0'},
        {'name': 'ev_touch_cnt', 'type': 'int', 'value': '0'},
        {'name': 'ev_spawn_cnt', 'type': 'int', 'value': '0'},
        {'name': 'ev_place_cnt', 'type': 'int', 'value': '0'},
        {'name': 'ev_def_cnt', 'type': 'int', 'value': '0'},
        {'name': 'ev_behav_cnt', 'type': 'int', 'value': '0'},
        {'name': 'ev_borrow_cnt', 'type': 'int', 'value': '0'},
        {'name': 'ev_kbuild_cnt', 'type': 'int', 'value': '0'},
        {'name': 'ev_kupdate_cnt', 'type': 'int', 'value': '0'},
        {'name': 'ev_ktouch_cnt', 'type': 'int', 'value': '0'},
        {'name': 'last_kill_info', 'type': 'text', 'value': '-'},
        {'name': 'last_touch_info', 'type': 'text', 'value': '-'},
        {'name': 'last_spawn_info', 'type': 'text', 'value': '-'},
        {'name': 'last_place_info', 'type': 'text', 'value': '-'},
        {'name': 'last_think_info', 'type': 'text', 'value': '-'},
        {'name': 'last_body_info', 'type': 'text', 'value': '-'},
        {'name': 'probe_misc', 'type': 'text', 'value': '-'},
        {'name': 'st_here', 'type': 'text', 'value': 'MENU'},
        {'name': 'last_cmd', 'type': 'text', 'value': 'Ready: [1] Start Level 1 (menu) / Spawn Spider (room)  [F2] HUD Page'},
    ]

    init_rule = rule(
        event='system.modInit',
        actions=[
            act('monsters.defineType', name='frost_skull', basedOn='skull',
                texEasy='textures/monster/cherv_hard.jpg', texHard='textures/monster/cherv_hard.jpg',
                speed='0.75', speedUp='0.75'),
            act('monsters.setTypeNumberProp', name='frost_skull', key='speed', value='0.80'),
            act('monsters.setTypeColorProp', name='frost_skull', key='part_color',
                r='0.25', g='0.85', b='1.0'),
            act('monsters.setTypeAssetProp', name='frost_skull', key='tex_easy',
                path='textures/monster/cherv_hard.jpg'),
            act('monsters.setTypeBox', name='frost_skull', which='AMOD_MONSTER_BOX_OBJECT',
                minX='-0.06', minY='0.0', minZ='-0.06', maxX='0.06', maxY='0.15', maxZ='0.06'),
            act('monsters.defineKind', name='frost_skull', movesLike='AMOD_MONSTER_MOVES_SKULL'),
        ],
        name='Init: Define frost_skull type, box & stock movement',
    )

    probes = [
        rule(actions=[
            act('var.set', name='cond_mask', value='0'),
            act('var.set', name='loop_live_cnt', value='0'),
            act('var.set', name='loop_pl_cnt', value='0'),
            act('var.set', name='first_id', value='=(int)monsters_id_at(0)'),
        ]),
        rule(conditions=[cond('player.isHere')],
             actions=[
                 act('var.setText', name='st_here', value='IN ROOM'),
                 act('var.set', name='p_col', value='=(int)((player_position(0) + 2.0f) / 0.1f)'),
                 act('var.set', name='p_row', value='=(int)((1.45f - player_position(1)) / 0.1f)'),
                 act('var.set', name='sp_col', value='=(p_col < 35 ? p_col + 2 : p_col - 2)'),
             ]),
        rule(conditions=[cond('player.isHere', inverted=True)],
             actions=[act('var.setText', name='st_here', value='MENU')]),
        rule(conditions=[cond('monsters.countCompare', op='>', count='0')],
             actions=[act('var.add', name='cond_mask', value='1')]),
        rule(conditions=[cond('monsters.killedCountCompare', op='>', count='0')],
             actions=[act('var.add', name='cond_mask', value='2')]),
        rule(conditions=[cond('monsters.typeCountCompare', op='>=', count='10')],
             actions=[act('var.add', name='cond_mask', value='4')]),
        rule(conditions=[cond('monsters.typeExists', name='frost_skull')],
             actions=[act('var.add', name='cond_mask', value='8')]),
        rule(conditions=[cond('monsters.typeHasProp', name='skull', key='speed')],
             actions=[act('var.add', name='cond_mask', value='16')]),
        rule(conditions=[cond('monsters.liveAlive', by='0', index='0')],
             actions=[act('var.add', name='cond_mask', value='32')]),
        rule(conditions=[cond('monsters.liveAlive', by='1', id='=first_id')],
             actions=[act('var.add', name='cond_mask', value='64')]),
        rule(conditions=[cond('monsters.liveAlive', by='2', key='skull')],
             actions=[act('var.add', name='cond_mask', value='128')]),
        rule(conditions=[cond('monsters.liveHasProp', index='0', key='speed')],
             actions=[act('var.add', name='cond_mask', value='256')]),
        rule(conditions=[cond('monsters.liveNumPropCompare', index='0', key='speed', op='>=', value='0')],
             actions=[act('var.add', name='cond_mask', value='512')]),
        rule(conditions=[cond('monsters.placedCountCompare', op='>', count='0')],
             actions=[act('var.add', name='cond_mask', value='1024')]),
        rule(conditions=[cond('monsters.placedKeyExists', key='skull')],
             actions=[act('var.add', name='cond_mask', value='2048')]),
        rule(conditions=[cond('monsters.forEachLive', var='m_lv_i')],
             actions=[act('var.add', name='loop_live_cnt', value='1')]),
        rule(conditions=[cond('monsters.forEachPlaced', var='m_pl_i')],
             actions=[act('var.add', name='loop_pl_cnt', value='1')]),
        rule(actions=[
            act('monsters.selectType', by='0', name='frost_skull'),
            act('monsters.selectType', by='1', index='=type_cursor'),
            act('monsters.selectTypeBox', name='skull', which='AMOD_MONSTER_BOX_MAIN'),
            act('monsters.selectLive', by='2', key='frost_skull'),
            act('monsters.selectLive', by='1', id='=first_id'),
            act('monsters.selectLive', by='0', index='0'),
            act('monsters.selectLiveBox', index='0', which='AMOD_MONSTER_BOX_OBJECT'),
            act('monsters.selectPlaced', index='0'),
            act('var.setText', name='probe_misc',
                value='fnd:{#(int)monsters_find_id("skull")} idx:{#monsters_index_of(1)} '
                      'fpl:{#monsters_find_placed(-1, "skull")} drw:{#(int)monsters_id_of_draw(0)} '
                      'kid:{#monsters_kind_id_of("sentinel")}'),
        ]),
        rule(conditions=[cond('monsters.boxLoaded')],
             actions=[act('var.add', name='cond_mask', value='4096')]),
    ]

    hud_page_0 = rule(
        conditions=[cond('var.compare', name='hud_page', op='==', value='0')],
        actions=[
            hud_line(14, '=== MONSTERS SHOWCASE [Page 1/3: Live Room & Spawner] ([F1] HUD [F2] Page [F3] Type) ===', 1.0, 0.88, 0.40),
            hud_line(34, 'State: {st_here} | Cell:({p_col},{p_row}) | Live:{#monsters_live_count()} (loop:{loop_live_cnt}) | '
                         'Killed:{#monsters_killed_count()} | Placed:{#monsters_placed_count(-1)} (loop:{loop_pl_cnt}) | Mask:{cond_mask}'),
            hud_line(52, 'Live #0 ok={#monsters_sel_exists()} id={#monsters_sel_id()} idx={#monsters_sel_index()} pl={#monsters_sel_placed()} '
                         'name={$monsters_sel_name()} tag={$monsters_sel_tag()} moves={$monsters_sel_moves()}', 0.75, 0.95, 1.0),
            hud_line(70, 'Pos:({~monsters_sel_pos(0)}, {~monsters_sel_pos(1)}, {~monsters_sel_pos(2)}) '
                         'lvl={#monsters_sel_level()} dir={#monsters_sel_facing()} spd={~monsters_sel_speed()} '
                         'hard={#monsters_sel_is_hard()} decor={#monsters_sel_is_decor()}'),
            hud_line(88, 'Cycle: has={#monsters_sel_has_state()} st={#monsters_sel_state()} left={~monsters_sel_time_left()} | '
                         'Anim id={#monsters_sel_anim_id()} set={$monsters_sel_frameset()} '
                         'fr={#monsters_sel_frame_a()}->{#monsters_sel_frame_b()} b={~monsters_sel_anim_blend()}'),
            hud_line(106, 'Anim spd={~monsters_sel_anim_speed()} rev={#monsters_sel_anim_backwards()} '
                          'stop={#monsters_sel_anim_stopped()} fin={#monsters_sel_anim_finished()} | '
                          'Box:({~monsters_box_min(0)},{~monsters_box_min(1)},{~monsters_box_min(2)})..'
                          '({~monsters_box_max(0)},{~monsters_box_max(1)},{~monsters_box_max(2)})'),
            hud_line(126, '[1] Level1/Spider [2] frost_skull [3] sentinel Kind [4] mummy2 (+opts) [5] Mutate #0', 0.72, 1.0, 0.78),
            hud_line(144, '[6] Borrow #0 [7] Return #0 [8] KillMode:{kill_mode} [9] TouchMode:{touch_mode} [0] Kill #0 [K] KillKey/ID [R] Rm/KillIn', 0.72, 1.0, 0.78),
            hud_line(164, 'Last: {last_cmd}', 1.0, 0.95, 0.65),
        ],
        name='HUD Page 0',
    )

    hud_page_1 = rule(
        conditions=[cond('var.compare', name='hud_page', op='==', value='1')],
        actions=[
            hud_line(14, '=== MONSTERS SHOWCASE [Page 2/3: Types, Properties & Placements] ([F3] Cursor #{type_cursor}) ===', 1.0, 0.88, 0.40),
            hud_line(34, 'Types:{#monsters_type_count()} | TypeProps:{#monsters_type_prop_count()} [0]={$monsters_type_prop_name_at(0)} | '
                         'LiveProps:{#monsters_prop_count()} [0]={$monsters_prop_name_at(0)}'),
            hud_line(52, 'Type[{type_cursor}] ok={#monsters_sel_type_ok()} name={$monsters_sel_type_name()} '
                         '(prop={$monsters_get_type_name_prop("skull")}) mdl={$monsters_sel_type_model()}', 0.75, 0.95, 1.0),
            hud_line(70, 'Speeds: floor={~monsters_sel_type_speed()} up={~monsters_sel_type_speed_up()} vert={~monsters_sel_type_vert_speed()} '
                         'propSpd={~monsters_get_type_num("skull", "speed")} colR={~monsters_get_type_color("skull", "part_color", 0)}'),
            hud_line(88, 'Blast Easy:({~monsters_sel_type_part(0)},{~monsters_sel_type_part(1)},{~monsters_sel_type_part(2)},{~monsters_sel_type_part(3)}) '
                         'Hard:({~monsters_sel_type_part_hard(0)},{~monsters_sel_type_part_hard(1)},{~monsters_sel_type_part_hard(2)},{~monsters_sel_type_part_hard(3)})'),
            hud_line(106, 'LiveProp #0: spd={~monsters_get_live_num(0, "speed")} sclX={~monsters_get_live_vec(0, "scale", 0)} '
                          'tag={$monsters_get_live_tag_prop(0)} | {probe_misc}'),
            hud_line(124, 'Placed[0] ok={#monsters_pl_exists()} rm={#monsters_pl_room()} idx={#monsters_pl_index()} '
                          'live={#monsters_pl_live_index()} {$monsters_pl_name()} tag={$monsters_pl_tag()}', 0.75, 0.95, 1.0),
            hud_line(142, 'Placed Pos:({~monsters_pl_pos(0)},{~monsters_pl_pos(1)},{~monsters_pl_pos(2)}) '
                          'lvl={#monsters_pl_level()} dir={#monsters_pl_facing()} spd={~monsters_pl_speed()} '
                          'hard={#monsters_pl_is_hard()} dec={#monsters_pl_is_decor()}'),
            hud_line(160, 'PlacedProp[0]: spd={~monsters_get_placed_num(-1, 0, "spawn_speed")} '
                          'wlk0={~monsters_get_placed_vec(-1, 0, "walk_time", 0)} tag={$monsters_get_placed_tag(-1, 0)} | {last_cmd}', 1.0, 0.95, 0.65),
        ],
        name='HUD Page 1',
    )

    hud_page_2 = rule(
        conditions=[cond('var.compare', name='hud_page', op='==', value='2')],
        actions=[
            hud_line(14, '=== MONSTERS SHOWCASE [Page 3/3: Events, Behaviours & Custom Kind] ===', 1.0, 0.88, 0.40),
            hud_line(34, 'Counts -> Kill:{ev_kill_cnt} Touch:{ev_touch_cnt} Spawn:{ev_spawn_cnt} Place:{ev_place_cnt} '
                         'DefAI:{ev_def_cnt} Behav:{ev_behav_cnt} Borrow:{ev_borrow_cnt}'),
            hud_line(52, 'Custom Kind "sentinel" -> Build:{ev_kbuild_cnt} Update:{ev_kupdate_cnt} Touch:{ev_ktouch_cnt} | '
                         '[B] Behav:{behav_active} [V] BlockPlace:{block_place} [U] UnsubProbe:{cancel_probe}', 0.75, 0.95, 1.0),
            hud_line(70, 'Kill Ev: {last_kill_info}'),
            hud_line(88, 'Touch Ev: {last_touch_info}'),
            hud_line(106, 'Spawn Ev: {last_spawn_info}'),
            hud_line(124, 'Place Ev: {last_place_info}'),
            hud_line(142, 'Think / Body: {last_think_info} | {last_body_info}'),
            hud_line(162, 'Last: {last_cmd}', 1.0, 0.95, 0.65),
        ],
        name='HUD Page 2',
    )

    frame_rule = rule(
        event='render.frame',
        children=probes + [
            rule(conditions=[cond('var.on', name='hud_on')],
                 children=[hud_page_0, hud_page_1, hud_page_2],
                 name='Draw HUD'),
        ],
        name='Every Frame Probes & HUD',
    )

    event_rules = [
        rule(
            event='monsters.beforeKill',
            actions=[
                act('var.add', name='ev_kill_cnt', value='1'),
                act('var.setText', name='last_kill_info',
                    value='id={#monsters_kill_id()} {$monsters_kill_name()} tag={$monsters_kill_tag()} '
                          'lvl={#monsters_kill_level()} dec={#monsters_kill_is_decor()} '
                          '@({~monsters_kill_pos(0)},{~monsters_kill_pos(1)},{~monsters_kill_pos(2)})'),
            ],
            children=[
                rule(conditions=[cond('var.compare', name='kill_mode', op='==', value='0')],
                     actions=[act('monsters.setKillResult', result='AMOD_MONSTER_KILL_DEFAULT')]),
                rule(conditions=[cond('var.compare', name='kill_mode', op='==', value='1')],
                     actions=[act('monsters.setKillResult', result='AMOD_MONSTER_KILL_HANDLED')]),
                rule(conditions=[cond('var.compare', name='kill_mode', op='==', value='2')],
                     actions=[act('monsters.setKillResult', result='AMOD_MONSTER_KILL_REFUSE')]),
            ],
            name='Before Monster Dies',
        ),
        rule(
            event='monsters.beforeTouch',
            actions=[
                act('var.add', name='ev_touch_cnt', value='1'),
                act('var.setText', name='last_touch_info',
                    value='id={#monsters_touch_id()} #{#monsters_touch_index()} {$monsters_touch_name()} '
                          'tag={$monsters_touch_tag()} pl={#monsters_touch_is_player()} '
                          'kind={#monsters_toucher_kind()} side={#monsters_touch_side()} '
                          '@({~monsters_touch_pos(0)},{~monsters_touch_pos(1)},{~monsters_touch_pos(2)})'),
            ],
            children=[
                rule(conditions=[
                    cond('monsters.touchIsPlayer'),
                    cond('monsters.touchSideIs', op='>=', side='1'),
                    cond('var.compare', name='touch_mode', op='==', value='1'),
                ], actions=[act('monsters.setTouchResult', result='AMOD_MONSTER_TOUCH_HANDLED')]),
                rule(conditions=[
                    cond('monsters.touchIsPlayer'),
                    cond('var.compare', name='touch_mode', op='==', value='2'),
                ], actions=[act('monsters.setTouchResult', result='AMOD_MONSTER_TOUCH_REFUSE')]),
            ],
            name='Before Monster Contact',
        ),
        rule(
            event='monsters.spawned',
            actions=[
                act('var.add', name='ev_spawn_cnt', value='1'),
                act('var.setText', name='last_spawn_info',
                    value='id={#monsters_spawn_id()} #{#monsters_spawn_index()} pl={#monsters_spawn_placed()} '
                          '{$monsters_spawn_name()} tag={$monsters_spawn_tag()} '
                          'lvl={#monsters_spawn_level()} dec={#monsters_spawn_is_decor()} '
                          '@({~monsters_spawn_pos(0)},{~monsters_spawn_pos(1)},{~monsters_spawn_pos(2)})'),
            ],
            name='Monster Spawned',
        ),
        rule(
            event='monsters.spawned',
            conditions=[cond('var.on', name='cancel_probe')],
            actions=[
                act('monsters.unsubscribe'),
                set_status('Unsubscribed auxiliary spawn listener via monsters.unsubscribe!'),
            ],
            name='Auxiliary Spawn Listener (Unsubscribe Probe)',
        ),
        rule(
            event='monsters.beforePlace',
            veto=True,
            conditions=[cond('var.on', name='block_place')],
            actions=[
                act('var.add', name='ev_place_cnt', value='1'),
                act('var.setText', name='last_place_info',
                    value='BLOCKED rm={#monsters_place_room()} {$monsters_place_name()} tag={$monsters_place_tag()} '
                          'lvl={#monsters_place_level()} dec={#monsters_place_is_decor()} '
                          '@({~monsters_place_pos(0)},{~monsters_place_pos(1)},{~monsters_place_pos(2)})'),
            ],
            name='Before Place (Veto When block_place=1)',
        ),
        rule(
            event='monsters.beforePlace',
            conditions=[cond('var.on', name='block_place', inverted=True)],
            actions=[
                act('var.add', name='ev_place_cnt', value='1'),
                act('var.setText', name='last_place_info',
                    value='OK rm={#monsters_place_room()} {$monsters_place_name()} tag={$monsters_place_tag()} '
                          'lvl={#monsters_place_level()} dec={#monsters_place_is_decor()} '
                          '@({~monsters_place_pos(0)},{~monsters_place_pos(1)},{~monsters_place_pos(2)})'),
            ],
            name='Before Place (Log When Allowed)',
        ),
        rule(
            event='monsters.definedThink',
            filters={'monsterName': 'frost_skull'},
            actions=[
                act('var.add', name='ev_def_cnt', value='1'),
                act('monsters.moveStep', index='=monsters_think_index()',
                    dirX='=monsters_think_facing()', dirY='0'),
                act('var.setText', name='last_think_info',
                    value='def id={#monsters_think_id()} #{#monsters_think_index()} dt={~monsters_think_delta()} '
                          'dir={#monsters_think_facing()} @({~monsters_think_pos(0)},{~monsters_think_pos(1)},{~monsters_think_pos(2)})'),
            ],
            name='Defined Think for frost_skull',
        ),
        rule(
            event='monsters.behaviourTick',
            filters={'monsterName': 'skull'},
            actions=[
                act('var.add', name='ev_behav_cnt', value='1'),
                act('monsters.setThinkMode', mode='AMOD_MONSTER_ALSO_GAME'),
            ],
            name='Behaviour Hook on skull (Also Game)',
        ),
        rule(
            event='monsters.borrowedTick',
            actions=[
                act('var.add', name='ev_borrow_cnt', value='1'),
                act('monsters.setThinkMode', mode='AMOD_MONSTER_INSTEAD_OF_GAME'),
                act('monsters.moveStep', index='=monsters_think_index()',
                    dirX='=(player_position(0) >= monsters_think_pos(0) ? 1 : -1)', dirY='0'),
                act('var.setText', name='last_think_info',
                    value='borrow id={#monsters_think_id()} #{#monsters_think_index()} dt={~monsters_think_delta()} '
                          'dir={#monsters_think_facing()} @({~monsters_think_pos(0)},{~monsters_think_pos(1)},{~monsters_think_pos(2)})'),
            ],
            name='Borrowed Monster Tick (Follows Player X)',
        ),
        rule(
            event='monsters.kindBuild',
            filters={'kindName': 'sentinel'},
            actions=[
                act('var.add', name='ev_kbuild_cnt', value='1'),
                act('monsters.bodySpecies', actsLike='spider', animatedLike='spider', frameset='move'),
                act('monsters.bodyHandles', modelId='0'),
                act('monsters.bodyPose', x='=monsters_body_pos(0)', y='=monsters_body_pos(1)',
                    z='=monsters_body_pos(2)', facing='1'),
                act('monsters.bodyBox', minX='-0.05', minY='0.0', minZ='-0.05',
                    maxX='0.05', maxY='0.12', maxZ='0.05'),
                act('monsters.bodyHitBox', minX='-0.06', minY='0.0', minZ='-0.06',
                    maxX='0.06', maxY='0.14', maxZ='0.06', keepHitBox='1'),
                act('var.setText', name='last_body_info',
                    value='kind={#monsters_body_kind_id()} {$monsters_body_name()} tag={$monsters_body_tag()} '
                          'dir={#monsters_body_facing()} @({~monsters_body_pos(0)},{~monsters_body_pos(1)},{~monsters_body_pos(2)})'),
            ],
            name='Build Custom Kind "sentinel"',
        ),
        rule(
            event='monsters.kindUpdate',
            filters={'kindName': 'sentinel'},
            actions=[
                act('var.add', name='ev_kupdate_cnt', value='1'),
                act('monsters.setLiveNumProp', index='=monsters_think_index()',
                    key='yaw', value='=mod_elapsed * 2.5f'),
            ],
            name='Update Custom Kind "sentinel" (Spin Yaw)',
        ),
        rule(
            event='monsters.kindTouch',
            filters={'kindName': 'sentinel'},
            conditions=[cond('monsters.touchIsPlayer')],
            actions=[
                act('var.add', name='ev_ktouch_cnt', value='1'),
                act('monsters.setTouchResult', result='AMOD_MONSTER_TOUCH_REFUSE'),
                set_status('Touched friendly custom kind "sentinel" (contact refused)!'),
            ],
            name='Touch Custom Kind "sentinel"',
        ),
    ]

    hotkeys = [
        rule(event='input.key', filters={'key': '112'},
             actions=[act('var.toggle', name='hud_on')], name='[F1] Toggle HUD'),
        rule(event='input.key', filters={'key': '113'},
             actions=[act('var.set', name='hud_page', value='=(hud_page + 1) % 3')], name='[F2] Cycle HUD Page'),
        rule(event='input.key', filters={'key': '114'},
             actions=[act('var.set', name='type_cursor',
                          value='=(monsters_type_count() > 0 ? (type_cursor + 1) % monsters_type_count() : 0)')],
             name='[F3] Cycle Monster Type Cursor'),
        rule(event='input.key', filters={'key': '49'},
             children=[
                 rule(conditions=[cond('player.isHere', inverted=True)],
                      actions=[
                          act('world.startLevel', level='levels/level_1.txt'),
                          set_status('[1] Starting levels/level_1.txt...'),
                      ]),
                 rule(conditions=[cond('player.isHere')],
                      actions=[
                          act('monsters.place', name='spider', col='=sp_col', row='=p_row'),
                          set_status('[1] Spawned spider (minimal 3-param form)!'),
                      ]),
             ], name='[1] Start Level 1 / Spawn Spider'),
        rule(event='input.key', filters={'key': '50'},
             conditions=[cond('player.isHere')],
             actions=[
                 act('monsters.place', name='frost_skull', col='=sp_col', row='=p_row',
                     pos3='3', pos4='3', facing='1', level='1', tag='frosty'),
                 set_status('[2] Spawned custom frost_skull with patrol pos3/pos4 & +tag!'),
             ], name='[2] Spawn Custom frost_skull'),
        rule(event='input.key', filters={'key': '51'},
             conditions=[cond('player.isHere')],
             actions=[
                 act('monsters.placeKind', kindName='sentinel', col='=sp_col', row='=p_row', tag='guard_orb'),
                 set_status('[3] Spawned custom RegisterKind "sentinel"!'),
             ], name='[3] Spawn Custom Kind sentinel'),
        rule(event='input.key', filters={'key': '52'},
             conditions=[cond('player.isHere')],
             actions=[
                 act('monsters.place', name='mummy2', col='=sp_col', row='=p_row',
                     pos3='2', pos4='2', facing='-1', room='-1', tag='room_mummy'),
                 act('monsters.setPlacedNumProp', index='0', key='spawn_speed', value='0.85'),
                 act('monsters.setPlacedRangeProp', index='0', key='walk_time', minVal='4', maxVal='8'),
                 act('monsters.setPlacedAxis', index='0', x='1', y='0', z='0'),
                 set_status('[4] Spawned mummy2 with dynamic patrol & tuned placement #0!'),
             ], name='[4] Spawn mummy2 & Tune Placement'),
        rule(event='input.key', filters={'key': '53'},
             conditions=[cond('monsters.liveAlive', by='0', index='0')],
             actions=[
                 act('monsters.setMovement', index='0', movesLike='AMOD_MONSTER_MOVES_JUMP_SKULL'),
                 act('monsters.setLiveNumProp', index='0', key='speed', value='1.15'),
                 act('monsters.setLiveRangeProp', index='0', key='walk_time', minVal='3', maxVal='6'),
                 act('monsters.setLiveVec3Prop', index='0', key='scale', x='1.3', y='1.3', z='1.3'),
                 act('monsters.setLiveAssetProp', index='0', key='texture', path='textures/monster/cherv_hard.jpg'),
                 act('monsters.setLiveBox', index='0', which='AMOD_MONSTER_BOX_OBJECT',
                     minX='-0.08', minY='0.0', minZ='-0.08', maxX='0.08', maxY='0.18', maxZ='0.08'),
                 act('monsters.playAnim', index='0', frameset='move'),
                 set_status('[5] Mutated live monster #0 (j_skull movement, 1.3x scale, speed, box, anim)!'),
             ], name='[5] Mutate Live Monster #0'),
        rule(event='input.key', filters={'key': '54'},
             conditions=[cond('monsters.liveAlive', by='0', index='0')],
             actions=[
                 act('monsters.borrow', index='0'),
                 set_status('[6] Borrowed monster #0 (now follows player X via BorrowedTick)!'),
             ], name='[6] Borrow Monster #0'),
        rule(event='input.key', filters={'key': '55'},
             conditions=[cond('monsters.liveAlive', by='0', index='0')],
             actions=[
                 act('monsters.returnMonster', index='0'),
                 set_status('[7] Returned monster #0 to game AI!'),
             ], name='[7] Return Monster #0'),
        rule(event='input.key', filters={'key': '56'},
             actions=[
                 act('var.set', name='kill_mode', value='=(kill_mode + 1) % 3'),
                 set_status('[8] Cycled kill_mode (0=Default, 1=Silent Handled, 2=Refuse/Immortal)!'),
             ], name='[8] Cycle Kill Verdict Mode'),
        rule(event='input.key', filters={'key': '57'},
             actions=[
                 act('var.set', name='touch_mode', value='=(touch_mode + 1) % 3'),
                 set_status('[9] Cycled touch_mode (0=Default, 1=Handled, 2=Refuse/Godmode vs Monsters)!'),
             ], name='[9] Cycle Touch Verdict Mode'),
        rule(event='input.key', filters={'key': '48'},
             conditions=[cond('monsters.liveAlive', by='0', index='0')],
             actions=[
                 act('monsters.kill', by='0', index='0'),
                 set_status('[0] Killed live monster #0!'),
             ], name='[0] Kill Live Monster #0'),
        rule(event='input.key', filters={'key': '75'},
             actions=[act('var.toggle', name='kill_key_toggle')],
             children=[
                 rule(conditions=[cond('var.on', name='kill_key_toggle')],
                      actions=[
                          act('monsters.kill', by='2', key='frost_skull'),
                          set_status('[K] Killed monster by key "frost_skull"!'),
                      ]),
                 rule(conditions=[cond('var.on', name='kill_key_toggle', inverted=True)],
                      actions=[
                          act('monsters.kill', by='1', id='=first_id'),
                          set_status('[K] Killed monster by ID (first_id)!'),
                      ]),
             ], name='[K] Kill By Key / By ID'),
        rule(event='input.key', filters={'key': '82'},
             actions=[act('var.toggle', name='remove_toggle')],
             children=[
                 rule(conditions=[cond('var.on', name='remove_toggle')],
                      actions=[
                          act('monsters.kill', by='3', room='-1', index='0'),
                          set_status('[R] Killed placement #0 in room -1!'),
                      ]),
                 rule(conditions=[cond('var.on', name='remove_toggle', inverted=True)],
                      actions=[
                          act('monsters.removePlaced', index='0'),
                          set_status('[R] Removed placement #0 silently!'),
                      ]),
             ], name='[R] Kill Placement / RemovePlaced #0'),
        rule(event='input.key', filters={'key': '66'},
             actions=[act('var.toggle', name='behav_active')],
             children=[
                 rule(conditions=[cond('var.on', name='behav_active')],
                      actions=[
                          act('monsters.enableBehaviour', name='skull'),
                          set_status('[B] Enabled SetBehaviour("skull")!'),
                      ]),
                 rule(conditions=[cond('var.on', name='behav_active', inverted=True)],
                      actions=[
                          act('monsters.clearBehaviour', name='skull'),
                          set_status('[B] Cleared SetBehaviour("skull")!'),
                      ]),
             ], name='[B] Toggle Species Behaviour on skull'),
        rule(event='input.key', filters={'key': '86'},
             actions=[
                 act('var.toggle', name='block_place'),
                 set_status('[V] Toggled BeforePlace veto (block_place)!'),
             ], name='[V] Toggle Placement Veto'),
        rule(event='input.key', filters={'key': '85'},
             actions=[
                 act('var.set', name='cancel_probe', value='1'),
                 set_status('[U] Armed cancel_probe (next spawn unsubscribes auxiliary listener)!'),
             ], name='[U] Arm Unsubscribe Probe'),
    ]

    rules = [init_rule, frame_rule] + event_rules + hotkeys
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

    cat_evs = {e['id'] for e in json.load(open(os.path.join(MONSTERS_PKG, 'events.json'), encoding='utf-8'))}
    cat_conds = {c['id'] for c in json.load(open(os.path.join(MONSTERS_PKG, 'conditions.json'), encoding='utf-8'))}
    cat_acts = {a['id'] for a in json.load(open(os.path.join(MONSTERS_PKG, 'actions.json'), encoding='utf-8'))}
    cat_exprs = json.load(open(os.path.join(MONSTERS_PKG, 'expressions.json'), encoding='utf-8'))

    miss_e = sorted(cat_evs - evs)
    miss_c = sorted(cat_conds - conds)
    miss_a = sorted(cat_acts - acts)
    miss_x = sorted(x['id'] for x in cat_exprs if x['text'] not in joined)

    unused_vars = [v['name'] for v in sheet['vars'] if v['name'] not in joined]

    if miss_e or miss_c or miss_a or miss_x or unused_vars:
        print('MISSING monsters coverage or unused vars in monsterstest:')
        if miss_e:
            print('  events:', miss_e)
        if miss_c:
            print('  conditions:', miss_c)
        if miss_a:
            print('  actions:', miss_a)
        if miss_x:
            print('  expressions:', miss_x)
        if unused_vars:
            print('  unused_vars:', unused_vars)
        sys.exit(1)

    print('monsterstest coverage 100%%: %d events, %d conditions, %d actions, %d expressions' % (
        len(cat_evs), len(cat_conds), len(cat_acts), len(cat_exprs)))


def main():
    sheet = build_sheet()
    verify_coverage(sheet)

    manifest = {
        'api_version': current_api_version(),
        'author': 'AFModConstructor',
        'capabilities': [],
        'description': 'Interactive MonstersApi showcase (types, behaviours, borrow, custom sentinel kind, boxes, rooms)',
        'entry': 'bin/monsterstest.dll',
        'game_version': '1.0',
        'id': 'monsterstest',
        'name': 'Monsters Showcase',
        'requires': ['input', 'monsters', 'player', 'render', 'world'],
        'version': '1.0.0',
    }

    project = {
        '@project': {
            '@build': '0',
            '@last.file': '',
            '@last.proj': '',
            '@name': 'Monsters Showcase',
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
    with open(os.path.join(PROJECT, 'monsterstest.azproj'), 'w', encoding='utf-8', newline='\n') as f:
        json.dump(project, f, ensure_ascii=False, indent=2)

    print('wrote', PROJECT)


if __name__ == '__main__':
    main()
