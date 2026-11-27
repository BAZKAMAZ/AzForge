"""Writes examples/bonusestest: an interactive in-game showcase of the entire Bonuses package.

Every event (5), condition (16), action (30), and expression (79) of packages/bonuses is wired
into a 3-page live HUD + hotkey showcase so all 36 BonusesApi calls can be tested in-game.

    python tools/make_bonusestest.py
"""
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROJECT = os.path.join(ROOT, 'examples', 'bonusestest')
BONUSES_PKG = os.path.join(ROOT, 'packages', 'bonuses', 'catalog')
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
        {'name': 'take_mode', 'type': 'int', 'value': '0'},
        {'name': 'magnet_on', 'type': 'bool', 'value': '0'},
        {'name': 'cancel_probe', 'type': 'bool', 'value': '0'},
        {'name': 'skin_toggle', 'type': 'bool', 'value': '0'},
        {'name': 'loop_live_cnt', 'type': 'int', 'value': '0'},
        {'name': 'first_live_idx', 'type': 'int', 'value': '-1'},
        {'name': 'cond_mask', 'type': 'int', 'value': '0'},
        {'name': 'ev_take_cnt', 'type': 'int', 'value': '0'},
        {'name': 'ev_spawn_cnt', 'type': 'int', 'value': '0'},
        {'name': 'ev_kbuild_cnt', 'type': 'int', 'value': '0'},
        {'name': 'ev_kupdate_cnt', 'type': 'int', 'value': '0'},
        {'name': 'ev_ktake_cnt', 'type': 'int', 'value': '0'},
        {'name': 'last_take_name', 'type': 'text', 'value': '-'},
        {'name': 'last_take_res', 'type': 'text', 'value': '-'},
        {'name': 'last_take_tag', 'type': 'text', 'value': '-'},
        {'name': 'last_take_col', 'type': 'int', 'value': '0'},
        {'name': 'last_take_row', 'type': 'int', 'value': '0'},
        {'name': 'last_take_pts', 'type': 'int', 'value': '0'},
        {'name': 'last_take_decor', 'type': 'int', 'value': '0'},
        {'name': 'last_take_x', 'type': 'float', 'value': '0.0'},
        {'name': 'last_take_y', 'type': 'float', 'value': '0.0'},
        {'name': 'last_take_z', 'type': 'float', 'value': '0.0'},
        {'name': 'last_sp_name', 'type': 'text', 'value': '-'},
        {'name': 'last_sp_res', 'type': 'text', 'value': '-'},
        {'name': 'last_sp_tag', 'type': 'text', 'value': '-'},
        {'name': 'last_sp_x', 'type': 'float', 'value': '0.0'},
        {'name': 'last_sp_y', 'type': 'float', 'value': '0.0'},
        {'name': 'last_sp_z', 'type': 'float', 'value': '0.0'},
        {'name': 'last_sp_hastype', 'type': 'int', 'value': '0'},
        {'name': 'last_sp_live', 'type': 'int', 'value': '0'},
        {'name': 'last_sp_kind', 'type': 'int', 'value': '0'},
        {'name': 'last_bd_name', 'type': 'text', 'value': '-'},
        {'name': 'last_bd_tag', 'type': 'text', 'value': '-'},
        {'name': 'last_bd_x', 'type': 'float', 'value': '0.0'},
        {'name': 'last_bd_y', 'type': 'float', 'value': '0.0'},
        {'name': 'last_bd_z', 'type': 'float', 'value': '0.0'},
        {'name': 'last_bd_amp', 'type': 'float', 'value': '0.0'},
        {'name': 'last_bd_spd', 'type': 'float', 'value': '0.0'},
        {'name': 'last_bd_hid', 'type': 'int', 'value': '0'},
        {'name': 'last_bd_pts', 'type': 'int', 'value': '0'},
        {'name': 'last_bd_mdl', 'type': 'int', 'value': '0'},
        {'name': 'last_bd_tex', 'type': 'int', 'value': '0'},
        {'name': 'last_k_dt', 'type': 'float', 'value': '0.0'},
        {'name': 'p_col', 'type': 'int', 'value': '20'},
        {'name': 'p_row', 'type': 'int', 'value': '14'},
        {'name': 'dx', 'type': 'float', 'value': '0.0'},
        {'name': 'dy', 'type': 'float', 'value': '0.0'},
        {'name': 'st_here', 'type': 'text', 'value': 'MENU'},
        {'name': 'last_cmd', 'type': 'text', 'value': 'Ready: [1] Start Level 1 (in menu) / Place Totem (in room)  [F2] HUD Page'},
    ]

    cond_probes = [
        rule(actions=[
            act('var.set', name='cond_mask', value='0'),
            act('var.set', name='loop_live_cnt', value='0'),
            act('var.set', name='first_live_idx', value='-1'),
        ]),
        rule(conditions=[cond('player.isHere')],
             actions=[
                 act('var.setText', name='st_here', value='IN ROOM'),
                 act('var.set', name='p_col', value='=(int)((player_position(0) + 2.0f) / 0.1f)'),
                 act('var.set', name='p_row', value='=(int)((1.45f - player_position(1)) / 0.1f)'),
             ]),
        rule(conditions=[cond('player.isHere', inverted=True)],
             actions=[act('var.setText', name='st_here', value='MENU')]),
        rule(conditions=[cond('bonuses.typeExists', name='solar_relic')],
             actions=[act('var.add', name='cond_mask', value='1')]),
        rule(conditions=[cond('bonuses.typeFlagIs', name='totem', flag='0')],
             actions=[act('var.add', name='cond_mask', value='2')]),
        rule(conditions=[cond('bonuses.typeCompare', name='totem', field='0', op='>=', value='50')],
             actions=[act('var.add', name='cond_mask', value='4')]),
        rule(conditions=[cond('bonuses.hasTypeProperty', name='torch', key='light_radius')],
             actions=[act('var.add', name='cond_mask', value='8')]),
        rule(conditions=[cond('bonuses.roomCountCompare', mode='0', op='>', count='0')],
             actions=[act('var.add', name='cond_mask', value='16')]),
        rule(conditions=[cond('bonuses.namedCountCompare', name='bonus', op='>', count='0')],
             actions=[act('var.add', name='cond_mask', value='32')]),
        rule(conditions=[cond('bonuses.taggedExists', tag='gift')],
             actions=[act('var.add', name='cond_mask', value='64')]),
        rule(conditions=[cond('bonuses.cellHasBonus', col='=p_col', row='=p_row')],
             actions=[act('var.add', name='cond_mask', value='128')]),
        rule(conditions=[cond('bonuses.placementIsLive', index='0')],
             actions=[act('var.add', name='cond_mask', value='256')]),
        rule(conditions=[cond('bonuses.placementMatches', index='0', text='bonus')],
             actions=[act('var.add', name='cond_mask', value='512')]),
        rule(conditions=[cond('bonuses.placementPropCompare', index='0', key='is_live', op='==', value='1')],
             actions=[act('var.add', name='cond_mask', value='1024')]),
        rule(conditions=[cond('bonuses.forEachPlacement', var='b_idx')],
             actions=[act('var.add', name='loop_live_cnt', value='1')],
             children=[
                 rule(conditions=[cond('var.compare', name='first_live_idx', op='<', value='0')],
                      actions=[act('var.set', name='first_live_idx', value='=b_idx')]),
             ]),
        rule(conditions=[cond('var.compare', name='first_live_idx', op='>=', value='0')],
             actions=[act('bonuses.selectPlacement', index='=first_live_idx')]),
        rule(actions=[act('bonuses.selectType', by='1', index='=type_cursor')]),
    ]

    hud_page_0 = [
        hud_line(40,
                 'Room Bonuses -> Live:{#bonuses_room_count(-1, 0)} (loop:{loop_live_cnt})  Total:{#bonuses_room_count(-1, 1)}  Slots:{#bonuses_placement_slots(-1)}  Types:{#bonuses_type_count()}',
                 r=0.82, g=0.95, b=1.0),
        hud_line(60,
                 'Lookups -> Tag"gift"Idx:{#bonuses_index_of_tag("gift", 0)}  At(20,14):{#bonuses_index_at_cell(20, 14)}  OrbKindId:{#bonuses_find_kind_id("orb")}  LastChainPts:{#bonuses_last_chain_points()}',
                 r=0.88, g=0.92, b=0.98),
        hud_line(80,
                 'Selected Pl #{#bonuses_sel_pl_int(0)} -> name:"{$bonuses_sel_pl_text(0)}" res:"{$bonuses_sel_pl_text(1)}" tag:"{$bonuses_sel_pl_text(2)}" cell:({#bonuses_sel_pl_int(1)},{#bonuses_sel_pl_int(2)})',
                 r=0.95, g=0.88, b=0.74),
        hud_line(100,
                 'Pl Pos:({~bonuses_sel_pl_num(0)}, {~bonuses_sel_pl_num(1)}, {~bonuses_sel_pl_num(2)})  live:{#bonuses_sel_pl_int(3)} hasType:{#bonuses_sel_pl_int(4)} kind:{#bonuses_sel_pl_int(5)}',
                 r=0.78, g=0.96, b=0.84),
        hud_line(124,
                 '[1]Start/PlaceTotem [2]PlaceCustom(ruby+solar) [3]MutateTypeSkin [4]SpawnOrbKind [5]MagnetBorrow:{magnet_on}',
                 r=0.72, g=0.84, b=0.98),
        hud_line(144,
                 '[6]ComboChain+Award [7]TakeMode:{take_mode}(0=Def,1=2x,2=Lock) [8]EditPl0 [9]CleanGifts [0]NextType',
                 r=0.72, g=0.84, b=0.98),
    ]

    hud_page_1 = [
        hud_line(40,
                 'Type[#{type_cursor}] -> res:"{$bonuses_sel_type_text(1)}" behaves:"{$bonuses_sel_type_text(0)}" pts:{#bonuses_sel_type_int(0)}/{#bonuses_sel_type_int(1)} time:{~bonuses_sel_type_num(0)}s',
                 r=0.82, g=0.95, b=1.0),
        hud_line(60,
                 'Flags -> totem:{#bonuses_sel_type_int(2)} torch:{#bonuses_sel_type_int(3)} train:{#bonuses_sel_type_int(4)}(#{#bonuses_sel_type_int(5)}) burn:{~bonuses_sel_type_num(1)}s lightR:{~bonuses_sel_type_num(2)}/{~bonuses_sel_type_num(3)}',
                 r=0.95, g=0.90, b=0.74),
        hud_line(80,
                 'IDs -> mdl:{#bonuses_sel_type_int(6)} tex:{#bonuses_sel_type_int(7)} icon:{#bonuses_sel_type_int(8)}  Glow:({~bonuses_sel_type_num(4)}, {~bonuses_sel_type_num(5)}, {~bonuses_sel_type_num(6)}, {~bonuses_sel_type_num(7)})',
                 r=0.78, g=0.96, b=0.84),
        hud_line(100,
                 'TypeProps({#bonuses_type_prop_count()}, [0]="{$bonuses_type_prop_name(0)}") -> totem pts={#bonuses_type_prop_int("totem", "pts_take")} time={~bonuses_type_prop_num("totem", "time", 0)} name="{$bonuses_type_prop_text("totem", "name")}"',
                 r=0.88, g=0.86, b=0.98),
        hud_line(124,
                 'PlProps({#bonuses_pl_prop_count()}, [0]="{$bonuses_pl_prop_name(0)}") -> Pl#0 time={~bonuses_pl_prop_num(-1, 0, "time", 0)} tag="{$bonuses_pl_prop_text(-1, 0, "tag")}"  CondMask:{cond_mask}',
                 r=0.78, g=0.88, b=0.98),
    ]

    hud_page_2 = [
        hud_line(40,
                 'Event Counts -> Take:{ev_take_cnt}  Spawn:{ev_spawn_cnt}  KindBuild:{ev_kbuild_cnt}  KindUpdate:{ev_kupdate_cnt}  KindTake:{ev_ktake_cnt}',
                 r=0.82, g=0.95, b=1.0),
        hud_line(60,
                 'Last Take -> "{$last_take_res}" ("{$last_take_name}", tag:"{$last_take_tag}") cell:({last_take_col},{last_take_row}) pts:{last_take_pts} decor:{last_take_decor} ({last_take_x},{last_take_y},{last_take_z})',
                 r=0.95, g=0.90, b=0.74),
        hud_line(80,
                 'Last Spawn -> "{$last_sp_res}" ("{$last_sp_name}", tag:"{$last_sp_tag}") pos:({last_sp_x},{last_sp_y},{last_sp_z}) type:{last_sp_hastype} live:{last_sp_live} kind:{last_sp_kind}',
                 r=0.96, g=0.84, b=0.78),
        hud_line(100,
                 'Orb Body -> "{$last_bd_name}" tag:"{$last_bd_tag}" pos:({last_bd_x},{last_bd_y},{last_bd_z}) sway:({last_bd_amp},{last_bd_spd})',
                 r=0.78, g=0.96, b=0.84),
        hud_line(124,
                 'Orb IDs -> hidden:{last_bd_hid} pts:{last_bd_pts} mdl:{last_bd_mdl} tex:{last_bd_tex} dt:{last_k_dt}',
                 r=0.78, g=0.88, b=0.98),
    ]

    hud_children = [
        rule(actions=[
            act('render.rect', x=8, y=8, w=624, h=190, r=0.04, g=0.06, b=0.11, a=0.84),
            act('render.outline', x=8, y=8, w=624, h=190, thickness=1, r=0.96, g=0.76, b=0.32, a=0.90),
            hud_line(16,
                     'BONUSES API TEST [{st_here}] Page {#hud_page + 1}/3 ([F1] HUD [F2] Page)',
                     r=1.0, g=0.90, b=0.45),
            hud_line(170, 'Last: {last_cmd}', r=0.98, g=0.86, b=0.46),
        ]),
        rule(conditions=[cond('var.compare', name='hud_page', op='==', value='0')],
             actions=hud_page_0,
             name='HUD Page 1: Room Counts, Placements & Hotkeys'),
        rule(conditions=[cond('var.compare', name='hud_page', op='==', value='1')],
             actions=hud_page_1,
             name='HUD Page 2: Selected Type & Property Reflection'),
        rule(conditions=[cond('var.compare', name='hud_page', op='==', value='2')],
             actions=hud_page_2,
             name='HUD Page 3: Take, Spawn & Custom Kind Telemetry'),
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
                          act('bonuses.place', name='totem', col='=p_col + 1', row='=p_row'),
                          act('bonuses.selectType', by='0', name='totem'),
                          set_status('1: Placed Protection Totem at player cell + 1'),
                      ]),
             ],
             name='1: Start Level 1 (in menu) or Place Built-in Totem (in room)'),

        rule(event='input.key', filters={'key': '50'},
             conditions=[cond('player.isHere')],
             actions=[
                 act('bonuses.place', name='ruby_gem', col='=p_col - 1', row='=p_row'),
                 act('bonuses.place', name='solar_relic', col='=p_col + 2', row='=p_row',
                     tag='gift', time='15', decor='1'),
                 act('bonuses.place', room='1', name='ruby_gem', col='20', row='14',
                     tag='room1_gift', decor='1'),
                 set_status('2: Placed custom ruby_gem + solar_relic (tag "gift") near player'),
             ],
             name='2: Place Custom Defined Bonus Types (ruby_gem & solar_relic)'),

        rule(event='input.key', filters={'key': '51'},
             actions=[act('var.toggle', name='skin_toggle')],
             children=[
                 rule(conditions=[cond('var.on', name='skin_toggle')],
                      actions=[
                          act('bonuses.setTypeTexture', id='ruby_gem', texture='textures/bonus/d_8.jpg'),
                          act('bonuses.setTypeIcon', id='solar_relic', icon='textures/bonus/megakey_icon.png'),
                          act('bonuses.setTypePropNumber', id='ruby_gem', key='pts_take', value='777'),
                          act('bonuses.setTypePropColor', id='ruby_gem', key='glow_color',
                              r='1.0', g='0.35', b='0.85', a='1.0'),
                          set_status('3: Mutated ruby_gem -> white brilliant texture + 777 pts + magenta glow'),
                      ]),
                 rule(conditions=[cond('var.on', name='skin_toggle', inverted=True)],
                      actions=[
                          act('bonuses.setTypePropAsset', id='ruby_gem', key='texture', path='textures/bonus/d_6.jpg'),
                          act('bonuses.setTypePropNumber', id='ruby_gem', key='pts_take', value='250'),
                          act('bonuses.setTypePropColor', id='ruby_gem', key='glow_color',
                              r='0.3', g='1.0', b='0.6', a='1.0'),
                          set_status('3: Mutated ruby_gem -> emerald texture + 250 pts + mint glow'),
                      ]),
             ],
             name='3: Live-Mutate Mod-Defined Bonus Type Texture, Icon, Points & Glow'),

        rule(event='input.key', filters={'key': '52'},
             conditions=[cond('player.isHere')],
             actions=[
                 act('bonuses.placeKind', kindName='orb', col='=p_col', row='=p_row - 1',
                     tag='orb_1', decor='1'),
                 set_status('4: Spawned custom "orb" bonus kind above player'),
             ],
             name='4: Spawn Custom Registered Bonus Kind ("orb")'),

        rule(event='input.key', filters={'key': '53'},
             conditions=[cond('player.isHere')],
             actions=[act('var.toggle', name='magnet_on')],
             children=[
                 rule(conditions=[cond('var.on', name='magnet_on')],
                      actions=[
                          act('bonuses.borrowPlacement', kindName='orb', index='=first_live_idx'),
                          act('bonuses.borrowAllInRoom', kindName='orb'),
                          set_status('5: Borrowed all room bonuses into Magnet "orb" kind! Walk near gems!'),
                      ]),
                 rule(conditions=[cond('var.on', name='magnet_on', inverted=True)],
                      actions=[
                          act('bonuses.returnPlacement', index='0'),
                          act('bonuses.returnAllInRoom'),
                          set_status('5: Returned all borrowed bonuses back to normal game control'),
                      ]),
             ],
             name='5: Toggle Borrow / Return Room Bonuses into Magnet "orb" Kind'),

        rule(event='input.key', filters={'key': '54'},
             conditions=[cond('player.isHere')],
             actions=[
                 act('bonuses.takeChainLink', points='25', where='0'),
                 act('bonuses.takeChainLink', points='25', where='1',
                     x='=player_position(0)', y='=player_position(1) + 0.28f', z='=player_position(2)'),
                 act('bonuses.awardPoints', points='100', where='0'),
                 act('bonuses.awardPoints', points='150', where='1',
                     x='=player_position(0)', y='=player_position(1) + 0.42f', z='=player_position(2)'),
                 set_status('6: Triggered 2x TakeChainLink + AwardPoints popups! Press 6 fast for Combo!'),
             ],
             name='6: Trigger Combo Chain Links & Floating Bonus Points'),

        rule(event='input.key', filters={'key': '55'},
             actions=[
                 act('var.set', name='take_mode', value='=(take_mode + 1) % 3'),
                 set_status('7: Cycled Take Mode (0=Default, 1=Custom 2x HANDLED, 2=Locked REFUSE)'),
             ],
             name='7: Cycle Before-Take Verdict Mode (DEFAULT -> HANDLED -> REFUSE)'),

        rule(event='input.key', filters={'key': '56'},
             conditions=[cond('player.isHere'),
                         cond('var.compare', name='first_live_idx', op='>=', value='0')],
             actions=[
                 act('bonuses.selectPlacement', index='=first_live_idx'),
                 act('bonuses.setPlacementText', index='=first_live_idx', key='tag', text='gift'),
                 act('bonuses.setPlacementNumber', index='=first_live_idx', key='decor', value='1'),
                 act('bonuses.setPlacementPos', index='=first_live_idx',
                     x='=bonuses_sel_pl_num(0)', y='=bonuses_sel_pl_num(1) + 0.06f', z='=bonuses_sel_pl_num(2)'),
                 set_status('8: Tagged first live bonus as "gift", marked decor=1, nudged Y +0.06'),
             ],
             name='8: Edit First Live Bonus Placement Properties'),

        rule(event='input.key', filters={'key': '57'},
             conditions=[cond('player.isHere')],
             actions=[
                 act('bonuses.remove', by='1', tag='gift'),
                 act('bonuses.remove', by='2', name='ruby_gem'),
                 set_status('9: Removed live "gift"-tagged & ruby_gem bonuses (Shift+9 removes first live)'),
             ],
             children=[
                 rule(conditions=[cond('input.keyDown', key='16'),
                                  cond('var.compare', name='first_live_idx', op='>=', value='0')],
                      actions=[act('bonuses.remove', by='0', index='=first_live_idx')]),
             ],
             name='9: Remove Tagged / Named / Indexed Bonuses'),

        rule(event='input.key', filters={'key': '48'},
             conditions=[cond('expr', text='bonuses_type_count() > 0')],
             actions=[
                 act('var.set', name='type_cursor', value='=(type_cursor + 1) % bonuses_type_count()'),
                 act('bonuses.selectType', by='1', index='=type_cursor'),
                 set_status('0: Selected next bonus type (check HUD Page 2 with F2)'),
             ],
             name='0: Cycle Selected Bonus Type Index'),
    ]

    rules = [
        rule(event='system.modInit',
             actions=[
                 act('bonuses.beginType', id='ruby_gem', basedOn='bonus_1', behavesAs='bonus', pointsTake='250'),
                 act('bonuses.commitType'),
                 act('bonuses.beginType', id='solar_relic', basedOn='totem', behavesAs='bonus',
                     pointsTake='500', pointsOut='100'),
                 act('bonuses.typeAssets',
                     model='models/bonus/corona.mmd',
                     texture='textures/bonus/cor_quest.jpg',
                     icon='textures/bonus/totem_icon.png',
                     soundTake='sounds/mega_take.wav'),
                 act('bonuses.typeTimerAndTrain', time='12', timerMode='AMOD_BONUS_TIMER_GAME',
                     makesTrain='1', trainIndex='6'),
                 act('bonuses.typeGlow',
                     r='1.0', g='0.9', b='0.3', a='1.0',
                     r2='1.0', g2='0.35', b2='0.1', a2='1.0'),
                 act('bonuses.typeRockingAndLight',
                     amp='0.5', speed='4.0', axisX='1.0', axisY='1.0', axisZ='0.0',
                     lr='1.0', lg='0.8', lb='0.4', radius='0.5'),
                 act('bonuses.commitType'),
                 act('bonuses.registerKind', kindName='orb'),
             ],
             name='Mod Init: Define ruby_gem & solar_relic types + register "orb" kind'),

        rule(event='render.frame',
             children=[
                 rule(group=True, name='Evaluate all Bonuses room & type conditions', collapsed=True,
                      children=cond_probes),
                 rule(conditions=[cond('var.on', name='hud_on')],
                      children=hud_children,
                      name='Draw 3-page BonusesApi HUD'),
             ],
             name='Frame Update & 3-Page Telemetry HUD'),

        rule(event='bonuses.beforeTake',
             actions=[
                 act('var.add', name='ev_take_cnt', value='1'),
                 act('var.setText', name='last_take_name', value='{$bonuses_take_text(0)}'),
                 act('var.setText', name='last_take_res', value='{$bonuses_take_text(1)}'),
                 act('var.setText', name='last_take_tag', value='{$bonuses_take_text(2)}'),
                 act('var.set', name='last_take_col', value='=bonuses_take_int(0)'),
                 act('var.set', name='last_take_row', value='=bonuses_take_int(1)'),
                 act('var.set', name='last_take_pts', value='=bonuses_take_int(2)'),
                 act('var.set', name='last_take_decor', value='=bonuses_take_int(3)'),
                 act('var.set', name='last_take_x', value='=bonuses_take_num(0)'),
                 act('var.set', name='last_take_y', value='=bonuses_take_num(1)'),
                 act('var.set', name='last_take_z', value='=bonuses_take_num(2)'),
             ],
             children=[
                 rule(conditions=[cond('var.compare', name='take_mode', op='==', value='1'),
                                  cond('bonuses.takeMatches', text='bonus')],
                      actions=[
                          act('bonuses.awardPoints', points='200', where='1',
                              x='=bonuses_take_num(0)', y='=bonuses_take_num(1)', z='=bonuses_take_num(2)'),
                          act('bonuses.takeChainLink', points='20', where='0'),
                          act('bonuses.setTakeResult', result='AMOD_BONUS_TAKE_HANDLED'),
                          set_status('TakeMode 1 (HANDLED): Custom +200 + ChainLink for diamond!'),
                      ]),
                 rule(conditions=[cond('var.compare', name='take_mode', op='==', value='2')],
                      actions=[
                          act('bonuses.setTakeResult', result='AMOD_BONUS_TAKE_REFUSE'),
                          set_status('TakeMode 2 (REFUSE): Bonus pickup refused — press 7 to unlock!'),
                      ]),
                 rule(conditions=[cond('bonuses.takeIsDecor')],
                      actions=[set_status('Picked up a decorative ($decor) bonus!')]),
                 rule(conditions=[cond('var.on', name='cancel_probe')],
                      actions=[act('bonuses.cancel')]),
             ],
             name='Before Bonus Taken'),

        rule(event='bonuses.beforeSpawn',
             actions=[
                 act('var.add', name='ev_spawn_cnt', value='1'),
                 act('var.setText', name='last_sp_name', value='{$bonuses_spawn_text(0)}'),
                 act('var.setText', name='last_sp_res', value='{$bonuses_spawn_text(1)}'),
                 act('var.setText', name='last_sp_tag', value='{$bonuses_spawn_text(2)}'),
                 act('var.set', name='last_sp_x', value='=bonuses_spawn_num(0)'),
                 act('var.set', name='last_sp_y', value='=bonuses_spawn_num(1)'),
                 act('var.set', name='last_sp_z', value='=bonuses_spawn_num(2)'),
                 act('var.set', name='last_sp_hastype', value='=bonuses_spawn_int(0)'),
                 act('var.set', name='last_sp_live', value='=bonuses_spawn_int(1)'),
                 act('var.set', name='last_sp_kind', value='=bonuses_spawn_int(2)'),
             ],
             children=[
                 rule(conditions=[cond('bonuses.spawnIsLive'),
                                  cond('bonuses.spawnMatches', text='solar_relic')],
                      actions=[set_status('Spawned custom solar_relic in room!')]),
             ],
             name='Before Bonus Spawns in Room'),

        rule(event='bonuses.kindBuild',
             filters={'kindName': 'orb'},
             actions=[
                 act('var.add', name='ev_kbuild_cnt', value='1'),
                 act('bonuses.bodySetLook',
                     model='models/bonus/corona.mmd',
                     texture='textures/bonus/cor_quest.jpg'),
                 act('bonuses.bodySetGlow', r='0.4', g='0.95', b='1.0', a='1.0'),
                 act('bonuses.bodySetGlow2', r='1.0', g='0.5', b='0.9', a='1.0'),
                 act('bonuses.bodySetRocking', amp='0.6', speed='5.0', axisX='0.0', axisY='1.0', axisZ='0.0'),
                 act('bonuses.bodySetLight', r='0.4', g='0.9', b='1.0', radius='0.5'),
                 act('bonuses.bodySetBox',
                     minX='-0.09', minY='-0.09', minZ='-0.10',
                     maxX='0.09', maxY='0.09', maxZ='0.10'),
                 act('bonuses.bodySetPoints', points='350'),
                 act('bonuses.bodySetHidden', hidden='0'),
             ],
             name='Kind "orb" Build'),

        rule(event='bonuses.kindUpdate',
             filters={'kindName': 'orb'},
             actions=[
                 act('var.add', name='ev_kupdate_cnt', value='1'),
                 act('var.set', name='last_k_dt', value='=bonuses_kind_dt()'),
                 act('var.setText', name='last_bd_name', value='{$bonuses_body_text(0)}'),
                 act('var.setText', name='last_bd_tag', value='{$bonuses_body_text(1)}'),
                 act('var.set', name='last_bd_x', value='=bonuses_body_num(0)'),
                 act('var.set', name='last_bd_y', value='=bonuses_body_num(1)'),
                 act('var.set', name='last_bd_z', value='=bonuses_body_num(2)'),
                 act('var.set', name='last_bd_amp', value='=bonuses_body_num(3)'),
                 act('var.set', name='last_bd_spd', value='=bonuses_body_num(4)'),
                 act('var.set', name='last_bd_hid', value='=bonuses_body_int(0)'),
                 act('var.set', name='last_bd_pts', value='=bonuses_body_int(1)'),
                 act('var.set', name='last_bd_mdl', value='=bonuses_body_int(2)'),
                 act('var.set', name='last_bd_tex', value='=bonuses_body_int(3)'),
                 act('bonuses.bodySetIds', modelId='0', textureId='0'),
                 act('bonuses.bodySetGlow',
                     r='=0.55f + 0.45f * sinf(mod_elapsed * 4.0f)',
                     g='=0.75f + 0.25f * cosf(mod_elapsed * 3.0f)',
                     b='1.0', a='1.0'),
             ],
             children=[
                 rule(conditions=[cond('player.isHere')],
                      actions=[
                          act('var.set', name='dx', value='=player_position(0) - bonuses_body_num(0)'),
                          act('var.set', name='dy', value='=player_position(1) - bonuses_body_num(1)'),
                      ],
                      children=[
                          rule(conditions=[cond('expr', text='(dx * dx + dy * dy) < 0.64f')],
                               actions=[
                                   act('bonuses.bodySetPos',
                                       x='=bonuses_body_num(0) + dx * 3.2f * bonuses_kind_dt()',
                                       y='=bonuses_body_num(1) + dy * 3.2f * bonuses_kind_dt()',
                                       z='=bonuses_body_num(2)'),
                               ]),
                      ],
                      name='Magnetize toward player within 0.8 world units'),
             ],
             name='Kind "orb" Per-Frame Update (Rainbow Glow + Magnet Pull)'),

        rule(event='bonuses.kindTake',
             filters={'kindName': 'orb'},
             actions=[
                 act('var.add', name='ev_ktake_cnt', value='1'),
                 act('bonuses.takeChainLink', points='30', where='1',
                     x='=bonuses_take_num(0)', y='=bonuses_take_num(1)', z='=bonuses_take_num(2)'),
                 set_status('Collected an "orb" / magnetized bonus (+ChainLink)!'),
             ],
             name='Kind "orb" Take'),
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

    cat_evs = {e['id'] for e in json.load(open(os.path.join(BONUSES_PKG, 'events.json'), encoding='utf-8'))}
    cat_conds = {c['id'] for c in json.load(open(os.path.join(BONUSES_PKG, 'conditions.json'), encoding='utf-8'))}
    cat_acts = {a['id'] for a in json.load(open(os.path.join(BONUSES_PKG, 'actions.json'), encoding='utf-8'))}
    cat_exprs = json.load(open(os.path.join(BONUSES_PKG, 'expressions.json'), encoding='utf-8'))

    miss_e = sorted(cat_evs - evs)
    miss_c = sorted(cat_conds - conds)
    miss_a = sorted(cat_acts - acts)
    miss_x = sorted(x['id'] for x in cat_exprs if x['text'] not in joined)

    if miss_e or miss_c or miss_a or miss_x:
        print('MISSING bonuses coverage in bonusestest:')
        if miss_e:
            print('  events:', miss_e)
        if miss_c:
            print('  conditions:', miss_c)
        if miss_a:
            print('  actions:', miss_a)
        if miss_x:
            print('  expressions:', miss_x)
        sys.exit(1)

    print('bonusestest coverage 100%%: %d events, %d conditions, %d actions, %d expressions' % (
        len(cat_evs), len(cat_conds), len(cat_acts), len(cat_exprs)))


def main():
    sheet = build_sheet()
    verify_coverage(sheet)

    manifest = {
        'api_version': current_api_version(),
        'author': 'AFModConstructor',
        'capabilities': [],
        'description': 'Interactive BonusesApi showcase (custom types, combo chain, magnet borrow, custom orb kind)',
        'entry': 'bin/bonusestest.dll',
        'game_version': '1.0',
        'id': 'bonusestest',
        'name': 'Bonuses Showcase',
        'requires': ['bonuses', 'fonts', 'input', 'player', 'render', 'world'],
        'version': '1.0.0',
    }

    project = {
        '@project': {
            '@build': '0',
            '@last.file': '',
            '@last.proj': '',
            '@name': 'Bonuses Showcase',
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
    with open(os.path.join(PROJECT, 'bonusestest.azproj'), 'w', encoding='utf-8', newline='\n') as f:
        json.dump(project, f, ensure_ascii=False, indent=2)

    print('wrote', PROJECT)


if __name__ == '__main__':
    main()
