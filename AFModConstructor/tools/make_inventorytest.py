"""Writes examples/inventorytest: an interactive in-game showcase of the entire Inventory package.

Every event (2), condition (15), action (13), and expression (47) of packages/inventory is wired
into a 3-page live HUD + hotkey showcase so all 29 InventoryApi calls can be tested in-game.

    python tools/make_inventorytest.py
"""
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROJECT = os.path.join(ROOT, 'examples', 'inventorytest')
INV_PKG = os.path.join(ROOT, 'packages', 'inventory', 'catalog')
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
        {'name': 'slot_cursor', 'type': 'int', 'value': '0'},
        {'name': 'timers_paused', 'type': 'bool', 'value': '0'},
        {'name': 'expanded_bag', 'type': 'bool', 'value': '0'},
        {'name': 'cancel_probe', 'type': 'bool', 'value': '0'},
        {'name': 'loop_used_cnt', 'type': 'int', 'value': '0'},
        {'name': 'first_used_slot', 'type': 'int', 'value': '-1'},
        {'name': 'cond_mask', 'type': 'int', 'value': '0'},
        {'name': 'ev_slot_cnt', 'type': 'int', 'value': '0'},
        {'name': 'ev_expired_cnt', 'type': 'int', 'value': '0'},
        {'name': 'ev_used_cnt', 'type': 'int', 'value': '0'},
        {'name': 'ev_chain_cnt', 'type': 'int', 'value': '0'},
        {'name': 'ev_chain_broke_cnt', 'type': 'int', 'value': '0'},
        {'name': 'last_ev_reason', 'type': 'int', 'value': '-1'},
        {'name': 'last_ev_slot', 'type': 'int', 'value': '-1'},
        {'name': 'last_ev_pts_out', 'type': 'int', 'value': '0'},
        {'name': 'last_ev_time', 'type': 'float', 'value': '0.0'},
        {'name': 'last_ev_name', 'type': 'text', 'value': '-'},
        {'name': 'last_ev_res', 'type': 'text', 'value': '-'},
        {'name': 'last_ch_len', 'type': 'int', 'value': '0'},
        {'name': 'last_ch_pts', 'type': 'int', 'value': '0'},
        {'name': 'last_ch_broke', 'type': 'int', 'value': '0'},
        {'name': 'st_here', 'type': 'text', 'value': 'MENU'},
        {'name': 'last_cmd', 'type': 'text', 'value': 'Ready: [1] Start Level 1 (menu) / Give Totem (room)  [F2] HUD Page'},
    ]

    cond_probes = [
        rule(actions=[
            act('var.set', name='cond_mask', value='0'),
            act('var.set', name='loop_used_cnt', value='0'),
            act('var.set', name='first_used_slot', value='-1'),
        ]),
        rule(conditions=[cond('inventory.isAvailable')],
             actions=[
                 act('var.setText', name='st_here', value='IN ROOM'),
                 act('var.add', name='cond_mask', value='1'),
             ]),
        rule(conditions=[cond('inventory.isAvailable', inverted=True)],
             actions=[act('var.setText', name='st_here', value='MENU')]),
        rule(conditions=[cond('inventory.isCarrying', name='ancient_amulet')],
             actions=[act('var.add', name='cond_mask', value='2')]),
        rule(conditions=[cond('inventory.isCarrying', name='totem')],
             actions=[act('var.add', name='cond_mask', value='4')]),
        rule(conditions=[cond('inventory.hasFreeSlot')],
             actions=[act('var.add', name='cond_mask', value='8')]),
        rule(conditions=[cond('inventory.countCompare', mode='0', op='>', count='0')],
             actions=[act('var.add', name='cond_mask', value='16')]),
        rule(conditions=[cond('inventory.slotHasItem', slot='0')],
             actions=[act('var.add', name='cond_mask', value='32')]),
        rule(conditions=[cond('inventory.slotItemIs', slot='0', name='totem')],
             actions=[act('var.add', name='cond_mask', value='64')]),
        rule(conditions=[cond('inventory.slotTimeCompare', slot='0', field='0', op='>', value='0')],
             actions=[act('var.add', name='cond_mask', value='128')]),
        rule(conditions=[cond('inventory.slotIsTicking', slot='0')],
             actions=[act('var.add', name='cond_mask', value='256')]),
        rule(conditions=[cond('inventory.hasItemProperty', slot='0', key='time_left')],
             actions=[act('var.add', name='cond_mask', value='512')]),
        rule(conditions=[cond('inventory.itemPropCompare', slot='0', key='pts_out', op='>=', value='100')],
             actions=[act('var.add', name='cond_mask', value='1024')]),
        rule(conditions=[cond('inventory.chainCompare', field='0', op='>=', value='2')],
             actions=[act('var.add', name='cond_mask', value='2048')]),
        rule(conditions=[cond('inventory.levelLeftCompare', which='0', op='>', count='0')],
             actions=[act('var.add', name='cond_mask', value='4096')]),
        rule(conditions=[cond('inventory.forEachSlot', var='s_idx')],
             actions=[act('var.add', name='loop_used_cnt', value='1')],
             children=[
                 rule(conditions=[cond('var.compare', name='first_used_slot', op='<', value='0')],
                      actions=[act('var.set', name='first_used_slot', value='=s_idx')]),
             ]),
        rule(actions=[act('inventory.selectSlot', slot='=slot_cursor')]),
    ]

    hud_page_0 = [
        hud_line(40,
                 'Slots -> Used:{#inv_used_count()}/{#inv_slot_count()} (Free:{#inv_free_count()}, NextFree:{#inv_free_slot()}, Loop:{loop_used_cnt})  TotemAt:{#inv_find_slot("totem")}  LastPut:{#inv_last_put_slot()}',
                 r=0.82, g=0.95, b=1.0),
        hud_line(60,
                 'Slot#0 -> "{$inv_slot_name(0, 0)}" (res:"{$inv_slot_name(0, 1)}")  Time:{~inv_slot_num(0, 0)}/{~inv_slot_num(0, 1)}s (frac:{~inv_slot_num(0, 2)})  Ticking:{#inv_is_ticking(0)}',
                 r=0.95, g=0.88, b=0.74),
        hud_line(80,
                 'Level Left -> Gems:{#inv_bonuses_left(0)}  Mega:{#inv_bonuses_left(1)}  Other:{#inv_bonuses_left(2)}  Total:{#inv_bonuses_left(3)}',
                 r=0.78, g=0.96, b=0.84),
        hud_line(100,
                 'Combo Chain -> Len:{#inv_chain_int(0)}  Pts:{#inv_chain_int(1)}  Timer:{~inv_chain_num(0)}/{~inv_chain_num(1)}s (frac:{~inv_chain_num(2)})',
                 r=0.88, g=0.90, b=1.0),
        hud_line(124,
                 '[1]Start/GiveTotem [2]GiveTorch+KeyA [3]GiveAmulet+Icon [4]PauseTimers:{timers_paused} [5]+5s/BoostSlot0',
                 r=0.72, g=0.84, b=0.98),
        hud_line(144,
                 '[6]Swap0<->1(Shift:Move0->2) [7]8-SlotBag:{expanded_bag} [8]ChainLink [9]ClearSlot(Shift:All) [0]NextSlot',
                 r=0.72, g=0.84, b=0.98),
    ]

    hud_page_1 = [
        hud_line(40,
                 'Selected Slot[#{#inv_sel_int(0)}] -> filled:{#inv_sel_int(1)}  name:"{$inv_sel_text(0)}"  res:"{$inv_sel_text(1)}"',
                 r=0.82, g=0.95, b=1.0),
        hud_line(60,
                 'Selected Timer -> left:{~inv_sel_num(0)}s  total:{~inv_sel_num(1)}s  frac:{~inv_sel_num(2)}  burn:{~inv_sel_num(3)}s',
                 r=0.95, g=0.90, b=0.74),
        hud_line(80,
                 'Selected Flags -> totem:{#inv_sel_int(2)}  torch:{#inv_sel_int(3)}  ptsOut:{#inv_sel_int(4)}',
                 r=0.78, g=0.96, b=0.84),
        hud_line(100,
                 'ItemProps({#inv_item_prop_count()}, [0]="{$inv_item_prop_name(0)}") -> Slot#0 pts_out={#inv_item_prop_int(0, "pts_out")} time_left={~inv_item_prop_num(0, "time_left")} name="{$inv_item_prop_text(0, "name")}"',
                 r=0.88, g=0.86, b=0.98),
        hud_line(124,
                 'Active Conditions Bitmask: {cond_mask}   (Press [0] to inspect slots 0..{#inv_slot_count() - 1})',
                 r=0.78, g=0.88, b=0.98),
    ]

    hud_page_2 = [
        hud_line(40,
                 'Slot Events -> Total:{ev_slot_cnt}  Expired:{ev_expired_cnt}  Used/Cleared:{ev_used_cnt}',
                 r=0.82, g=0.95, b=1.0),
        hud_line(60,
                 'Last Slot Emptied -> slot:#{last_ev_slot} reason:{last_ev_reason} "{$last_ev_res}" ("{$last_ev_name}") timeLeft:{last_ev_time}s ptsOut:{last_ev_pts_out}',
                 r=0.95, g=0.90, b=0.74),
        hud_line(80,
                 'Chain Events -> Total:{ev_chain_cnt}  Broken:{ev_chain_broke_cnt}',
                 r=0.78, g=0.96, b=0.84),
        hud_line(100,
                 'Last Chain Event -> len:{last_ch_len}  pts:{last_ch_pts}  broke:{last_ch_broke}   (Press [8] repeatedly then wait 0.5s!)',
                 r=0.96, g=0.84, b=0.78),
    ]

    hud_children = [
        rule(actions=[
            act('render.rect', x=8, y=8, w=624, h=190, r=0.04, g=0.06, b=0.11, a=0.84),
            act('render.outline', x=8, y=8, w=624, h=190, thickness=1, r=0.42, g=0.88, b=0.68, a=0.90),
            hud_line(16,
                     'INVENTORY API TEST [{st_here}] Page {#hud_page + 1}/3 ([F1] HUD [F2] Page)  Avail:{#inv_available()}',
                     r=0.55, g=0.98, b=0.78),
            hud_line(170, 'Last: {last_cmd}', r=0.98, g=0.86, b=0.46),
        ]),
        rule(conditions=[cond('var.compare', name='hud_page', op='==', value='0')],
             actions=hud_page_0,
             name='HUD Page 1: Slots, Level Counts, Combo & Hotkeys'),
        rule(conditions=[cond('var.compare', name='hud_page', op='==', value='1')],
             actions=hud_page_1,
             name='HUD Page 2: Selected Slot & Item Property Reflection'),
        rule(conditions=[cond('var.compare', name='hud_page', op='==', value='2')],
             actions=hud_page_2,
             name='HUD Page 3: Slot Emptied & Chain Event Telemetry'),
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
                 rule(conditions=[cond('inventory.isAvailable', inverted=True)],
                      actions=[
                          act('world.startLevel', level='levels/level_1.txt'),
                          set_status('1: Started levels/level_1.txt'),
                      ]),
                 rule(conditions=[cond('inventory.isAvailable')],
                      actions=[
                          act('inventory.give', name='totem', time='12', timerMode='AMOD_BONUS_TIMER_GAME'),
                          set_status('1: Gave Protection Totem (12s) into first free slot'),
                      ]),
             ],
             name='1: Start Level 1 (in menu) or Give Totem (in room)'),

        rule(event='input.key', filters={'key': '50'},
             conditions=[cond('inventory.isAvailable')],
             actions=[
                 act('inventory.give', name='torch', time='20', timerMode='AMOD_BONUS_TIMER_GAME'),
                 act('inventory.give', name='key_a', timerMode='AMOD_BONUS_TIMER_NONE'),
                 set_status('2: Gave Torch (20s) + White Key (no timer)'),
             ],
             name='2: Give Torch & White Key'),

        rule(event='input.key', filters={'key': '51'},
             conditions=[cond('inventory.isAvailable')],
             actions=[
                 act('inventory.give', name='ancient_amulet', time='15', timerMode='AMOD_BONUS_TIMER_GAME'),
                 set_status('3: Gave custom ancient_amulet (Shift+3 puts in Slot #0 with custom icon)'),
             ],
             children=[
                 rule(conditions=[cond('input.keyDown', key='16')],
                      actions=[
                          act('inventory.give', slot='0', name='ancient_amulet', time='18',
                              timerMode='AMOD_BONUS_TIMER_MOD'),
                          act('inventory.setSlotIcon', slot='0', icon='textures/bonus/fomka_icon.png'),
                      ]),
                 rule(conditions=[cond('expr', text='inv_last_put_slot() >= 0')],
                      actions=[
                          act('inventory.setSlotIcon', slot='=inv_last_put_slot()',
                              icon='textures/bonus/megakey_icon.png'),
                      ]),
             ],
             name='3: Give Custom Ancient Amulet + Per-Slot Icon Override'),

        rule(event='input.key', filters={'key': '52'},
             conditions=[cond('inventory.isAvailable')],
             actions=[act('var.toggle', name='timers_paused')],
             children=[
                 rule(conditions=[cond('var.on', name='timers_paused')],
                      actions=[
                          act('inventory.setTicking', mode='3'),
                          set_status('4: Froze all carried slot timers (rings stay visible)!'),
                      ]),
                 rule(conditions=[cond('var.on', name='timers_paused', inverted=True)],
                      actions=[
                          act('inventory.setTicking', mode='4'),
                          set_status('4: Resumed all carried slot timers!'),
                      ]),
                 rule(conditions=[cond('input.keyDown', key='16')],
                      actions=[
                          act('inventory.setTicking', mode='0', slot='0'),
                          act('inventory.setTicking', mode='2', slot='0'),
                      ]),
             ],
             name='4: Pause / Resume All Slot Timers'),

        rule(event='input.key', filters={'key': '53'},
             conditions=[cond('inventory.isAvailable'),
                         cond('inventory.slotHasItem', slot='0')],
             actions=[
                 act('inventory.addTimeLeft', slot='0', delta='5'),
                 act('inventory.setItemPropNumber', slot='0', key='pts_out', value='750'),
                 act('inventory.setItemPropAsset', slot='0', key='icon', path='textures/bonus/fomka_icon.png'),
                 set_status('5: Slot #0 +5s timer, pts_out=750, icon=crowbar (Shift+5 sets 3s left)'),
             ],
             children=[
                 rule(conditions=[cond('input.keyDown', key='16')],
                      actions=[act('inventory.setTimeLeft', slot='0', seconds='3')]),
             ],
             name='5: Add +5s Time, Boost pts_out & Swap Icon on Slot #0'),

        rule(event='input.key', filters={'key': '54'},
             conditions=[cond('inventory.isAvailable')],
             children=[
                 rule(conditions=[cond('input.keyDown', key='16', inverted=True)],
                      actions=[
                          act('inventory.swapItems', first='0', second='1'),
                          set_status('6: Swapped Slot #0 <-> Slot #1 (hold Shift+6 to Move #0 -> #2)'),
                      ]),
                 rule(conditions=[cond('input.keyDown', key='16')],
                      actions=[
                          act('inventory.moveItem', fromSlot='0', toSlot='2'),
                          set_status('Shift+6: Moved Slot #0 into empty Slot #2'),
                      ]),
             ],
             name='6: Swap Slots #0 <-> #1 (or Shift+6 Move #0 -> #2)'),

        rule(event='input.key', filters={'key': '55'},
             actions=[act('var.toggle', name='expanded_bag')],
             children=[
                 rule(conditions=[cond('var.on', name='expanded_bag')],
                      actions=[
                          act('inventory.setSlotCount', count='8'),
                          set_status('7: Expanded inventory capacity to 8 slots!'),
                      ]),
                 rule(conditions=[cond('var.on', name='expanded_bag', inverted=True)],
                      actions=[
                          act('inventory.setSlotCount', count='5'),
                          set_status('7: Restored standard 5-slot inventory capacity'),
                      ]),
             ],
             name='7: Toggle 8-Slot Expanded Inventory vs Standard 5 Slots'),

        rule(event='input.key', filters={'key': '56'},
             conditions=[cond('inventory.isAvailable')],
             actions=[
                 act('bonuses.takeChainLink', points='20', where='0'),
                 set_status('8: Added Combo Chain Link (+20) — watch Chain Len/Timer & Page 3!'),
             ],
             name='8: Advance Combo Chain Link'),

        rule(event='input.key', filters={'key': '57'},
             conditions=[cond('inventory.isAvailable')],
             children=[
                 rule(conditions=[cond('input.keyDown', key='16', inverted=True),
                                  cond('var.compare', name='first_used_slot', op='>=', value='0')],
                      actions=[
                          act('inventory.clear', by='0', slot='=first_used_slot'),
                          set_status('9: Cleared first occupied slot (Shift+9 clears all + key_a)'),
                      ]),
                 rule(conditions=[cond('input.keyDown', key='16')],
                      actions=[
                          act('inventory.clear', by='1', name='key_a'),
                          act('inventory.clear', by='2'),
                          set_status('Shift+9: Cleared all carried items!'),
                      ]),
             ],
             name='9: Clear First Occupied Slot (or Shift+9 Clear All)'),

        rule(event='input.key', filters={'key': '48'},
             conditions=[cond('expr', text='inv_slot_count() > 0')],
             actions=[
                 act('var.set', name='slot_cursor', value='=(slot_cursor + 1) % inv_slot_count()'),
                 act('inventory.selectSlot', slot='=slot_cursor'),
                 set_status('0: Cycled selected slot (check HUD Page 2 with F2)'),
             ],
             name='0: Cycle Selected Slot Index'),
    ]

    rules = [
        rule(event='system.modInit',
             actions=[
                 act('bonuses.beginType', id='ancient_amulet', basedOn='totem', behavesAs='',
                     pointsTake='150', pointsOut='500'),
                 act('bonuses.typeAssets',
                     model='models/bonus/corona.mmd',
                     texture='textures/bonus/cor_quest.jpg',
                     icon='textures/bonus/megakey_icon.png',
                     soundTake='sounds/totem_take.wav',
                     soundUse='sounds/totem_timeend.wav'),
                 act('bonuses.typeTimerAndTrain', time='15', timerMode='AMOD_BONUS_TIMER_GAME',
                     makesTrain='1', trainIndex='5'),
                 act('bonuses.commitType'),
             ],
             name='Mod Init: Define custom carried item "ancient_amulet"'),

        rule(event='render.frame',
             children=[
                 rule(group=True, name='Evaluate all Inventory conditions', collapsed=True,
                      children=cond_probes),
                 rule(conditions=[cond('var.on', name='hud_on')],
                      children=hud_children,
                      name='Draw 3-page InventoryApi HUD'),
             ],
             name='Frame Update & 3-Page Telemetry HUD'),

        rule(event='inventory.slotEmptied',
             actions=[
                 act('var.add', name='ev_slot_cnt', value='1'),
                 act('var.set', name='last_ev_reason', value='=inv_ev_int(0)'),
                 act('var.set', name='last_ev_slot', value='=inv_ev_int(1)'),
                 act('var.set', name='last_ev_pts_out', value='=inv_ev_int(2)'),
                 act('var.set', name='last_ev_time', value='=inv_ev_time_left()'),
                 act('var.setText', name='last_ev_name', value='{$inv_ev_text(0)}'),
                 act('var.setText', name='last_ev_res', value='{$inv_ev_text(1)}'),
             ],
             children=[
                 rule(conditions=[cond('inventory.eventReasonIs', reason='0')],
                      actions=[
                          act('var.add', name='ev_expired_cnt', value='1'),
                          set_status('Slot timer EXPIRED!'),
                      ]),
                 rule(conditions=[cond('inventory.eventReasonIs', reason='1')],
                      actions=[
                          act('var.add', name='ev_used_cnt', value='1'),
                          set_status('Slot item USED / CLEARED!'),
                      ]),
                 rule(conditions=[cond('var.on', name='cancel_probe')],
                      actions=[act('inventory.cancel')]),
             ],
             name='On Inventory Slot Emptied'),

        rule(event='inventory.chainChanged',
             actions=[
                 act('var.add', name='ev_chain_cnt', value='1'),
                 act('var.set', name='last_ch_len', value='=inv_chain_ev_int(0)'),
                 act('var.set', name='last_ch_pts', value='=inv_chain_ev_int(1)'),
                 act('var.set', name='last_ch_broke', value='=inv_chain_ev_int(2)'),
             ],
             children=[
                 rule(conditions=[cond('inventory.chainEvBroke')],
                      actions=[
                          act('var.add', name='ev_chain_broke_cnt', value='1'),
                          set_status('Combo chain window expired (broke = 1)'),
                      ]),
             ],
             name='On Combo Chain Changed'),
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

    cat_evs = {e['id'] for e in json.load(open(os.path.join(INV_PKG, 'events.json'), encoding='utf-8'))}
    cat_conds = {c['id'] for c in json.load(open(os.path.join(INV_PKG, 'conditions.json'), encoding='utf-8'))}
    cat_acts = {a['id'] for a in json.load(open(os.path.join(INV_PKG, 'actions.json'), encoding='utf-8'))}
    cat_exprs = json.load(open(os.path.join(INV_PKG, 'expressions.json'), encoding='utf-8'))

    miss_e = sorted(cat_evs - evs)
    miss_c = sorted(cat_conds - conds)
    miss_a = sorted(cat_acts - acts)
    miss_x = sorted(x['id'] for x in cat_exprs if x['text'] not in joined)

    if miss_e or miss_c or miss_a or miss_x:
        print('MISSING inventory coverage in inventorytest:')
        if miss_e:
            print('  events:', miss_e)
        if miss_c:
            print('  conditions:', miss_c)
        if miss_a:
            print('  actions:', miss_a)
        if miss_x:
            print('  expressions:', miss_x)
        sys.exit(1)

    print('inventorytest coverage 100%%: %d events, %d conditions, %d actions, %d expressions' % (
        len(cat_evs), len(cat_conds), len(cat_acts), len(cat_exprs)))


def main():
    sheet = build_sheet()
    verify_coverage(sheet)

    manifest = {
        'api_version': current_api_version(),
        'author': 'AFModConstructor',
        'capabilities': [],
        'description': 'Interactive InventoryApi showcase (carried items, timer pause, 8-slot bag, slot swap, combo chain)',
        'entry': 'bin/inventorytest.dll',
        'game_version': '1.0',
        'id': 'inventorytest',
        'name': 'Inventory Showcase',
        'requires': ['bonuses', 'fonts', 'input', 'inventory', 'render', 'world'],
        'version': '1.0.0',
    }

    project = {
        '@project': {
            '@build': '0',
            '@last.file': '',
            '@last.proj': '',
            '@name': 'Inventory Showcase',
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
    with open(os.path.join(PROJECT, 'inventorytest.azproj'), 'w', encoding='utf-8', newline='\n') as f:
        json.dump(project, f, ensure_ascii=False, indent=2)

    print('wrote', PROJECT)


if __name__ == '__main__':
    main()
