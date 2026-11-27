"""Writes examples/worldtest: an interactive in-game showcase of the entire World package.

Every event (4), condition (29), action (37), and expression (147) of packages/world is wired
into a 3-page live HUD + hotkey showcase so all 67 WorldApi calls can be inspected and tested.

    python tools/make_worldtest.py
"""
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROJECT = os.path.join(ROOT, 'examples', 'worldtest')
WORLD_PKG = os.path.join(ROOT, 'packages', 'world', 'catalog')
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
        {'name': 'frame_light_on', 'type': 'bool', 'value': '0'},
        {'name': 'plr_hidden', 'type': 'bool', 'value': '0'},
        {'name': 'plr_lantern', 'type': 'bool', 'value': '0'},
        {'name': 'pause_flag', 'type': 'bool', 'value': '0'},
        {'name': 'cancel_probe', 'type': 'bool', 'value': '0'},
        {'name': 'ev_loaded_cnt', 'type': 'int', 'value': '0'},
        {'name': 'ev_unloaded_cnt', 'type': 'int', 'value': '0'},
        {'name': 'ev_room_cnt', 'type': 'int', 'value': '0'},
        {'name': 'ev_timeup_cnt', 'type': 'int', 'value': '0'},
        {'name': 'ev_last_kind', 'type': 'int', 'value': '-1'},
        {'name': 'ev_last_ridx', 'type': 'int', 'value': '-1'},
        {'name': 'ev_last_rid', 'type': 'int', 'value': '0'},
        {'name': 'ev_last_prev', 'type': 'int', 'value': '-1'},
        {'name': 'ev_last_mx', 'type': 'int', 'value': '-1'},
        {'name': 'ev_last_my', 'type': 'int', 'value': '-1'},
        {'name': 'scan_rooms', 'type': 'int', 'value': '0'},
        {'name': 'scan_boxes', 'type': 'int', 'value': '0'},
        {'name': 'scan_monsters', 'type': 'int', 'value': '0'},
        {'name': 'scan_spawns', 'type': 'int', 'value': '0'},
        {'name': 'scan_lights', 'type': 'int', 'value': '0'},
        {'name': 'scan_tris', 'type': 'int', 'value': '0'},
        {'name': 'scan_statics', 'type': 'int', 'value': '0'},
        {'name': 'scan_things', 'type': 'int', 'value': '0'},
        {'name': 'cond_flags', 'type': 'int', 'value': '0'},
        {'name': 'px', 'type': 'float', 'value': '0.0'},
        {'name': 'py', 'type': 'float', 'value': '0.0'},
        {'name': 'pz', 'type': 'float', 'value': '0.0'},
        {'name': 'static_mat', 'type': 'list', 'value': '16'},
        {'name': 'st_lvl', 'type': 'text', 'value': 'MENU'},
        {'name': 'last_cmd', 'type': 'text', 'value': 'Ready: [1] Level 1  [2] Bonus Level  [F2] Cycle HUD pages'},
    ]

    cond_checks = [
        rule(actions=[act('var.set', name='cond_flags', value='0')]),

        rule(conditions=[cond('world.isLevelLoaded')],
             actions=[act('var.setText', name='st_lvl', value='LOADED'),
                      act('var.add', name='cond_flags', value='1')],
             children=[
                 rule(conditions=[cond('world.hasPlayerEntity')],
                      actions=[
                          act('world.selectPlayerEntity'),
                          act('var.set', name='px', value='=world_entity_num(0)'),
                          act('var.set', name='py', value='=world_entity_num(1)'),
                          act('var.set', name='pz', value='=world_entity_num(2)'),
                          act('world.selectCell', by='1', x='=px', y='=py'),
                      ]),
             ]),
        rule(conditions=[cond('world.isLevelLoaded', inverted=True)],
             actions=[act('var.setText', name='st_lvl', value='MENU')]),

        rule(conditions=[cond('world.isPaused')],
             actions=[act('var.add', name='cond_flags', value='2')]),
        rule(conditions=[cond('world.isBonusLevel')],
             actions=[act('var.add', name='cond_flags', value='4')]),
        rule(conditions=[cond('world.isDemoLevel')],
             actions=[act('var.add', name='cond_flags', value='8')]),
        rule(conditions=[cond('world.timeLeftBelow', seconds='15')],
             actions=[act('var.add', name='cond_flags', value='16')]),
        rule(conditions=[cond('world.elapsedIs', op='>=', seconds='5')],
             actions=[act('var.add', name='cond_flags', value='32')]),
        rule(conditions=[cond('world.levelStatIs', counter='0', half='1', op='>', value='0')],
             actions=[act('var.add', name='cond_flags', value='64')]),
        rule(conditions=[cond('world.room', room='1')],
             actions=[act('var.add', name='cond_flags', value='128')]),
        rule(conditions=[cond('world.roomCountIs', op='>', value='0'),
                         cond('world.roomExistsById', roomId='1'),
                         cond('world.mazeHasRoom', x='0', y='0')],
             actions=[act('var.add', name='cond_flags', value='256')]),
        rule(conditions=[cond('world.cellIs', room='-1', col='20', row='13', type='87')],
             actions=[act('var.add', name='cond_flags', value='512')]),
        rule(conditions=[cond('world.cellByteIs', room='-1', col='20', row='13', slot='0', op='==', value='42')],
             actions=[act('var.add', name='cond_flags', value='1024')]),
        rule(conditions=[cond('world.pointInBox', room='-1', box='0', x='=px', y='=py', z='=pz'),
                         cond('world.boxKindIs', room='-1', box='0', kind='AMOD_BOX_PUSHES')],
             actions=[act('var.add', name='cond_flags', value='2048')]),
        rule(conditions=[cond('world.entityIsHidden', room='-1', index='-1')],
             actions=[act('var.add', name='cond_flags', value='4096')]),
        rule(conditions=[cond('world.entityHasLight', room='-1', index='-1')],
             actions=[act('var.add', name='cond_flags', value='8192')]),
        rule(conditions=[cond('world.staticIsShown', room='-1', index='0')],
             actions=[act('var.add', name='cond_flags', value='16384')]),
        rule(conditions=[cond('world.roomIsSaved', room='-1')],
             actions=[act('var.add', name='cond_flags', value='32768')]),

        rule(conditions=[cond('var.on', name='frame_light_on'),
                         cond('world.isLevelLoaded')],
             actions=[act('world.addFrameLight', x='=px', y='=py + 0.2f', z='=pz',
                          r='0.3', g='0.85', b='1.0', range='1.4')]),
    ]

    page_0 = [
        hud_line(30,
                 'Lvl:{st_lvl}({#(API->world ? API->world->IsLevelLoaded(SELF) : 0)})  '
                 'Pause:{#(API->world && API->world->IsPaused ? API->world->IsPaused(SELF) : 0)}  '
                 'No:{#world_level_int(0)}  Bonus:{#world_level_int(1)}(cnt:{#world_level_int(2)}/{'
                 '~world_level_float(2)}s)  Demo:{#world_level_int(3)}/{#world_level_int(4)}'),
        hud_line(45,
                 'Script:"{$world_level_text(0)}"  Name:"{$world_level_text(1)}"  '
                 'Next:"{$world_level_text(2)}"'),
        hud_line(60,
                 'Bg:"{$world_level_text(3)}"  Mus:"{$world_level_text(4)}"(@{~world_level_float(1)}s)  '
                 'Castle:"{$world_level_text(8)}"'),
        hud_line(75,
                 'Titles:"{$world_level_text(5)}" / "{$world_level_text(6)}"  '
                 'StatMenu:"{$world_level_text(7)}"  TargetTime:{~world_level_float(0)}s'),
        hud_line(90,
                 'Elapsed:{~(API->world ? API->world->GetElapsed(SELF) : 0.0f)}s  '
                 'TimeLeft:{~(API->world ? API->world->GetTimeLeft(SELF) : 0.0f)}s  '
                 'StartMaze:({#world_level_int(7)},{#world_level_int(8)}) '
                 'StartCell:({~world_level_float(3)},{~world_level_float(4)})'),
        hud_line(105,
                 'Stats Crys:{#world_stat_val(0, 0)}/{#world_stat_val(0, 1)}  '
                 'Bon:{#world_stat_val(1, 0)}/{#world_stat_val(1, 1)}  '
                 'Sup:{#world_stat_val(2, 0)}/{#world_stat_val(2, 1)}  '
                 'Mon:{#world_stat_val(3, 0)}/{#world_stat_val(3, 1)}'),
        hud_line(120,
                 'RoomsSeen Light:{#world_stat_val(4, 0)}/{#world_stat_val(4, 1)}  '
                 'Dark:{#world_stat_val(5, 0)}/{#world_stat_val(5, 1)}  '
                 'Secret:{#world_stat_val(6, 0)}/{#world_stat_val(6, 1)}  '
                 '#8:{#world_stat_val(7, 0)}/{#world_stat_val(7, 1)}'),
        hud_line(135,
                 'CurRoom:{#(API->world ? API->world->GetCurrentRoom(SELF) : -1)} '
                 '(prev:{#(API->world ? API->world->GetPreviousRoom(SELF) : -1)} / '
                 'total:{#(API->world ? API->world->GetRoomCount(SELF) : 0)})  '
                 'SelRoom idx:{#world_room_int(0)} id:{#world_room_int(1)} '
                 'loaded:{#world_room_int(4)} backWall:{#world_room_int(5)}'),
        hud_line(150,
                 'RoomFile:"{$world_room_text(0)}"  RoomMus:"{$world_room_text(1)}"  '
                 'Tex(LM:{#world_room_tex(0)} Wall:{#world_room_tex(1)} Flr:{#world_room_tex(2)})'),
        hud_line(165,
                 'Maze rows:{#world_level_int(6)} cur:({#world_maze_cursor(0)},{#world_maze_cursor(1)}) '
                 'rowW:{#world_maze_row_width(-1)}  '
                 'RoomMaze:({#world_room_int(2)},{#world_room_int(3)})  '
                 'SelMaze id:{#world_maze_room_id(-1, -1)}->idx:{#world_maze_room_index(-1, -1)}'),
        hud_line(180,
                 'Events L:{ev_loaded_cnt} U:{ev_unloaded_cnt} R:{ev_room_cnt} T:{ev_timeup_cnt}  '
                 'Last(k:{ev_last_kind} idx:{ev_last_ridx} id:{ev_last_rid} '
                 'prev:{ev_last_prev} m:{ev_last_mx},{ev_last_my})'),
    ]

    page_1 = [
        hud_line(30,
                 'SelCell:({#world_sel_col()}, {#world_sel_row()}) -> '
                 'World:({~world_cell_x(-1)}, {~world_cell_y(-1)})  '
                 'Code:{#world_cell_code(world_sel_room_idx(), -1, -1)} ("{$world_cell_text()}")'),
        hud_line(45,
                 'Grid Totals  Walls("W"):{#world_count_cells(world_sel_room_idx(), 87)}  '
                 'Empty(" "):{#world_count_cells(world_sel_room_idx(), 32)}  '
                 'ModBytes[0..1]:({#world_cell_free_byte(world_sel_room_idx(), -1, -1, 0)}, '
                 '{#world_cell_free_byte(world_sel_room_idx(), -1, -1, 1)})'),
        hud_line(60,
                 'Boxes:{#world_room_int(6)} (lastAdd:{#world_last_box()})  '
                 'SelBox kind:{#world_box_kind(world_sel_room_idx(), -1)}'),
        hud_line(75,
                 'SelBox Min:({~world_box_num(0)}, {~world_box_num(1)}, {~world_box_num(2)})  '
                 'Max:({~world_box_num(3)}, {~world_box_num(4)}, {~world_box_num(5)})'),
        hud_line(90,
                 'Entities:{#world_entity_count(world_sel_room_idx())}  '
                 'PlayerIdx:{#world_player_entity()}  '
                 'SelEnt kind:{#world_entity_int(0)} name:"{$world_entity_name()}"'),
        hud_line(105,
                 'SelEnt Pos:({~world_entity_num(0)}, {~world_entity_num(1)}, {~world_entity_num(2)})  '
                 'Prev:({~world_entity_num(3)}, {~world_entity_num(4)}, {~world_entity_num(5)})'),
        hud_line(120,
                 'SelEnt State:{#world_entity_int(1)}/{#world_entity_int(2)}  '
                 'Flags:{#world_entity_int(3)}  Dead:{#world_entity_int(4)}  '
                 'Hidden:{#world_is_entity_hidden(world_sel_room_idx(), -1)}'),
        hud_line(135,
                 'Spawns:{#world_spawn_count(world_sel_room_idx())}  '
                 'SelSpawn kind:{#world_spawn_int(0)} live:{#world_spawn_int(1)} '
                 'name:"{$world_spawn_name()}"'),
        hud_line(150,
                 'SelSpawn Pos:({~world_spawn_num(0)}, {~world_spawn_num(1)}, {~world_spawn_num(2)})'),
        hud_line(165,
                 'Loop Scan [M] -> Rooms:{scan_rooms} Boxes:{scan_boxes} Monsters:{scan_monsters} '
                 'Spawns:{scan_spawns} Lights:{scan_lights}'),
        hud_line(180,
                 'Loop Scan [M] -> LightTris:{scan_tris} Statics:{scan_statics} '
                 'SavedThings:{scan_things}  CondMask:{cond_flags}'),
    ]

    page_2 = [
        hud_line(30,
                 'RoomLights:{#world_light_count(world_sel_room_idx())} '
                 '(lastAdd:{#world_last_light()})  FrameLight[9]:{frame_light_on}'),
        hud_line(45,
                 'SelLight Pos:({~world_light_num(0)}, {~world_light_num(1)}, {~world_light_num(2)})  '
                 'RGB:({~world_light_num(3)}, {~world_light_num(4)}, {~world_light_num(5)})  '
                 'Range:{~world_light_num(6)}'),
        hud_line(60,
                 'EntLight On:{#world_entity_has_light(world_sel_room_idx(), -1)}  '
                 'RGB:({~world_entity_light_num(0)}, {~world_entity_light_num(1)}, '
                 '{~world_entity_light_num(2)})  Range:{~world_entity_light_num(3)}'),
        hud_line(75,
                 'EntLight Offset:({~world_entity_light_num(4)}, {~world_entity_light_num(5)}, '
                 '{~world_entity_light_num(6)})'),
        hud_line(90,
                 'LightTris:{#world_room_int(7)} (lastAdd:{#world_last_tri()})  '
                 'P1:({~world_tri_num(0)}, {~world_tri_num(1)}, {~world_tri_num(2)})'),
        hud_line(105,
                 'Tri P2:({~world_tri_num(3)}, {~world_tri_num(4)}, {~world_tri_num(5)})  '
                 'P3:({~world_tri_num(6)}, {~world_tri_num(7)}, {~world_tri_num(8)})'),
        hud_line(120,
                 'Statics:{#world_static_count(world_sel_room_idx())} (lastAdd:{#world_last_static()})  '
                 'Shown:{#world_static_int(0)} Lit:{#world_static_int(1)} '
                 'Trans:{#world_static_int(2)} Blend:{#world_static_int(3)}'),
        hud_line(135,
                 'Static Model:{#world_static_int(4)} Tex:{#world_static_int(5)}  '
                 'Pos:({~world_static_pos(0)}, {~world_static_pos(1)}, {~world_static_pos(2)})'),
        hud_line(150,
                 'RoomThings:{#world_thing_count(world_sel_room_idx())}  '
                 'SelThing tag:{#world_thing_int(0)} bytes:{#world_thing_int(1)} '
                 'vals:({#world_thing_int(2)}, {#world_thing_int(3)}, {#world_thing_int(4)})'),
        hud_line(165,
                 'RoomMem saved:{#world_mem_int(world_sel_room_idx(), 0)} '
                 'state:{#world_mem_int(world_sel_room_idx(), 1)} '
                 'hasExtra:{#world_mem_int(world_sel_room_idx(), 2)} '
                 'extra:{#world_mem_int(world_sel_room_idx(), 3)}  '
                 'TotalSaveBytes:{#world_room_saved_size(world_sel_room_idx())}'),
        hud_line(180,
                 'Hotkeys: [7]AddBox+Static [8]RemBox+Static/HidePlr [9]Lights+Tri [M]ScanLoops+SaveMem'),
    ]

    hud_children = [
        rule(actions=[
            act('render.rect', x=10, y=8, w=620, h=222, r=0.05, g=0.07, b=0.11, a=0.82),
            act('render.outline', x=10, y=8, w=620, h=222, thickness=1, r=0.35, g=0.72, b=0.96, a=0.92),
            hud_line(13,
                     'WORLD API TEST [F1 HUD] [F2 Page {#hud_page + 1}/3]  '
                     '[1]Lvl1 [2]Bonus [3]Time/Stat [4/P]Pause [5/6]Grid [0]Cross [Bksp]Leave',
                     0.45, 0.88, 1.0),
        ]),
        rule(conditions=[cond('var.compare', name='hud_page', op='==', value='0')],
             actions=page_0, name='HUD Page 1: Level, Stats, Rooms & Maze'),
        rule(conditions=[cond('var.compare', name='hud_page', op='==', value='1')],
             actions=page_1, name='HUD Page 2: Grid, Boxes, Entities & Spawns'),
        rule(conditions=[cond('var.compare', name='hud_page', op='==', value='2')],
             actions=page_2, name='HUD Page 3: Lights, Triangles, Statics & Save'),
        rule(actions=[
            hud_line(198, 'Status: {last_cmd}', 1.0, 0.86, 0.42),
            hud_line(213, 'PlayerPos:({px}, {py}, {pz})  CondFlags:{cond_flags}', 0.68, 0.82, 0.95),
        ]),
    ]

    hotkeys = [
        rule(event='input.key', filters={'key': '112'},
             actions=[act('var.toggle', name='hud_on')],
             name='F1: Toggle HUD'),

        rule(event='input.key', filters={'key': '113'},
             actions=[act('var.set', name='hud_page', value='=(hud_page + 1) % 3')],
             name='F2: Cycle HUD Page (1..3)'),

        rule(event='input.key', filters={'key': '49'},
             actions=[
                 act('world.startLevel', level='levels/level_1.txt'),
                 set_status('1: Starting levels/level_1.txt'),
             ],
             name='1: Start Level 1'),

        rule(event='input.key', filters={'key': '50'},
             actions=[
                 act('world.startLevel', level='levels/level_2_1_b.txt'),
                 set_status('2: Starting bonus level levels/level_2_1_b.txt'),
             ],
             name='2: Start Bonus Level'),

        rule(event='input.key', filters={'key': '51'},
             actions=[
                 act('world.setTimeLeft', seconds='90'),
                 act('world.setElapsed', seconds='0'),
                 act('world.addLevelStat', counter='0', delta='5'),
                 act('world.setLevelStat', counter='7', taken='3', total='10'),
                 set_status('3: Set TimeLeft=90s, Elapsed=0s, +5 crystals, stat#8=3/10'),
             ],
             name='3: Set TimeLeft, Reset Stopwatch & Update Level Stats'),

        rule(event='input.key', filters={'key': '52'},
             actions=[
                 act('world.setPaused', mode='2'),
                 set_status('4: Toggled game pause via world.setPaused(toggle)'),
             ],
             name='4: Toggle Game Pause'),

        rule(event='input.key', filters={'key': '80'},
             actions=[act('var.toggle', name='pause_flag')],
             children=[
                 rule(conditions=[cond('var.on', name='pause_flag')],
                      actions=[act('world.setPaused', mode='1'),
                               set_status('P: Explicit world.setPaused = Pause')]),
                 rule(conditions=[cond('var.on', name='pause_flag', inverted=True)],
                      actions=[act('world.setPaused', mode='0'),
                               set_status('P: Explicit world.setPaused = Resume')]),
             ],
             name='P: Explicit SetPaused Pause/Resume'),

        rule(event='input.key', filters={'key': '53'},
             actions=[
                 act('world.snapshotCells', slot='0'),
                 act('world.fillCellsRect', col='18', row='14', w='4', h='1', type='87'),
                 act('world.setCell', col='20', row='13', type='87'),
                 act('world.setCellByte', col='20', row='13', slot='0', value='42'),
                 act('world.setCellByte', col='20', row='13', slot='1', value='7'),
                 act('world.selectCell', by='0', col='20', row='13'),
                 set_status('5: Saved grid[0], built wall rect (18..21,14) + cell(20,13) bytes 42,7'),
             ],
             name='5: Snapshot Grid, Build Wall Cells & Write Cell Mod Bytes'),

        rule(event='input.key', filters={'key': '54'},
             actions=[
                 act('world.restoreCells', slot='0'),
                 act('world.clearCellBytes', col='20', row='13'),
                 act('world.selectCell', by='0', col='20', row='13'),
                 set_status('6: Restored room grid from slot 0 & cleared cell(20,13) mod bytes'),
             ],
             name='6: Restore Room Grid & Clear Cell Mod Bytes'),

        rule(event='input.key', filters={'key': '55'},
             actions=[
                 act('world.addBox', room='-1', kind='AMOD_BOX_PUSHES',
                     minX='=px - 0.25f', minY='=py - 0.25f', minZ='-0.3',
                     maxX='=px + 0.25f', maxY='=py - 0.12f', maxZ='0.3'),
                 act('world.setBox', room='-1', box='-1', kind='AMOD_BOX_PUSHES',
                     minX='=px - 0.30f', minY='=py - 0.22f', minZ='-0.3',
                     maxX='=px + 0.30f', maxY='=py - 0.10f', maxZ='0.3'),
                 act('world.addStatic', room='-1', model='0', texture='0',
                     x='=px', y='=py + 0.25f', z='=pz', scale='1.0',
                     lit='1', translucent='1', blend='AMOD_STATIC_BLEND_ADD'),
                 act('world.setStaticStyle', room='-1', index='-1',
                     lit='1', translucent='1', blend='AMOD_STATIC_BLEND_ADD',
                     model='0', texture='0'),
                 act('world.setStaticPos', room='-1', index='-1',
                     x='=px', y='=py + 0.30f', z='=pz', scale='1.15'),
                 act('world.setStaticVisible', room='-1', index='-1', shown='1'),
                 act('world.readStaticMatrix', room='-1', index='-1', list='static_mat'),
                 act('world.setStaticMatrix', room='-1', index='-1', list='static_mat'),
                 set_status('7: Spawned platform box under player & cloned glowing static above'),
             ],
             name='7: Add/Edit Collision Box & Clone/Transform Static Decoration'),

        rule(event='input.key', filters={'key': '56'},
             actions=[
                 act('world.removeBox', room='-1', box='-1'),
                 act('world.removeStatic', room='-1', index='-1'),
                 act('var.toggle', name='plr_hidden'),
                 act('world.selectPlayerEntity'),
             ],
             children=[
                 rule(conditions=[cond('var.on', name='plr_hidden')],
                      actions=[act('world.setEntityHidden', room='-1', index='-1', hidden='1'),
                               set_status('8: Removed added box/static & hid player entity')]),
                 rule(conditions=[cond('var.on', name='plr_hidden', inverted=True)],
                      actions=[act('world.setEntityHidden', room='-1', index='-1', hidden='0'),
                               set_status('8: Removed added box/static & unhid player entity')]),
             ],
             name='8: Remove Added Box/Static & Toggle Player Hidden'),

        rule(event='input.key', filters={'key': '57'},
             actions=[
                 act('var.toggle', name='frame_light_on'),
                 act('var.toggle', name='plr_lantern'),
                 act('world.selectPlayerEntity'),
                 act('world.addRoomLight', room='-1', x='=px', y='=py + 0.3f', z='0',
                     r='1.0', g='0.75', b='0.25', range='1.6'),
                 act('world.setRoomLight', room='-1', index='-1', x='=px', y='=py + 0.35f', z='0',
                     r='1.0', g='0.82', b='0.30', range='1.8'),
                 act('world.addLightTriangle', room='-1',
                     x1='=px - 0.6f', y1='=py - 0.4f', z1='0',
                     x2='=px + 0.6f', y2='=py - 0.4f', z2='0',
                     x3='=px', y3='=py + 0.6f', z3='0'),
                 act('world.setLightTriangle', room='-1', index='-1',
                     x1='=px - 0.7f', y1='=py - 0.4f', z1='0',
                     x2='=px + 0.7f', y2='=py - 0.4f', z2='0',
                     x3='=px', y3='=py + 0.7f', z3='0'),
             ],
             children=[
                 rule(conditions=[cond('var.on', name='plr_lantern')],
                      actions=[act('world.setEntityLight', room='-1', index='-1', lit='1',
                                   offX='0', offY='0.15', offZ='0',
                                   r='1.0', g='0.9', b='0.45', range='1.5'),
                               set_status('9: Added room light + light tri + player lantern ON')]),
                 rule(conditions=[cond('var.on', name='plr_lantern', inverted=True)],
                      actions=[act('world.setEntityLight', room='-1', index='-1', lit='0',
                                   offX='0', offY='0', offZ='0',
                                   r='0', g='0', b='0', range='0'),
                               set_status('9: Added room light + light tri + player lantern OFF')]),
             ],
             name='9: Add Room Light, Light Triangle, Frame Light & Player Lantern'),

        rule(event='input.key', filters={'key': '48'},
             actions=[
                 act('world.crossRoom', side='1'),
                 set_status('0: Crossed into adjacent room on right (+X)'),
             ],
             name='0: Cross Room Right (+X)'),

        rule(event='input.key', filters={'key': '8'},
             actions=[
                 act('world.leaveLevel'),
                 set_status('Backspace: Leaving level back to main menu'),
             ],
             name='Backspace: Leave Current Level'),

        rule(event='input.key', filters={'key': '77'},
             actions=[
                 act('var.set', name='scan_rooms', value='0'),
                 act('var.set', name='scan_boxes', value='0'),
                 act('var.set', name='scan_monsters', value='0'),
                 act('var.set', name='scan_spawns', value='0'),
                 act('var.set', name='scan_lights', value='0'),
                 act('var.set', name='scan_tris', value='0'),
                 act('var.set', name='scan_statics', value='0'),
                 act('var.set', name='scan_things', value='0'),
             ],
             children=[
                 rule(conditions=[cond('world.forEachRoom', var='r_idx')],
                      actions=[act('var.add', name='scan_rooms', value='1')]),
                 rule(conditions=[cond('world.forEachBox', room='-1', var='box_idx')],
                      actions=[act('var.add', name='scan_boxes', value='1')]),
                 rule(conditions=[cond('world.forEachEntity', room='-1', var='ent_idx'),
                                  cond('world.entityKindIs', kind='AMOD_ENTITY_MONSTER'),
                                  cond('world.entityIsDead', inverted=True)],
                      actions=[act('var.add', name='scan_monsters', value='1')]),
                 rule(conditions=[cond('world.forEachSpawn', room='-1', var='spw_idx')],
                      actions=[act('var.add', name='scan_spawns', value='1')]),
                 rule(conditions=[cond('world.forEachLight', room='-1', var='lgt_idx')],
                      actions=[act('var.add', name='scan_lights', value='1')]),
                 rule(conditions=[cond('world.forEachLightTri', room='-1', var='tri_idx')],
                      actions=[act('var.add', name='scan_tris', value='1')]),
                 rule(conditions=[cond('world.forEachStatic', room='-1', var='stc_idx')],
                      actions=[act('var.add', name='scan_statics', value='1')]),
                 rule(conditions=[cond('world.forEachThing', room='-1', var='thg_idx')],
                      actions=[act('var.add', name='scan_things', value='1')],
                      children=[
                          rule(conditions=[cond('world.thingTagIs', tag='9')],
                               actions=[act('world.setRoomThing', room='-1', index='-1',
                                            v0='=world_thing_int(2)',
                                            v1='=world_thing_int(3)',
                                            v2='=world_thing_int(4)')]),
                      ]),
                 rule(conditions=[cond('world.roomIsSaved', room='-1')],
                      actions=[act('world.setRoomMemory', room='-1',
                                   state='=world_mem_int(world_sel_room_idx(), 1)',
                                   hasExtra='1',
                                   extra='=world_mem_int(world_sel_room_idx(), 3)')]),
                 rule(actions=[
                     act('world.selectRoom', by='1', roomId='1'),
                     act('world.selectRoom', by='0', room='-1'),
                     act('world.selectMazeCell', x='-1', y='-1'),
                     act('world.selectBox', box='0'),
                     act('world.selectEntity', index='0'),
                     act('world.selectPlayerEntity'),
                     act('world.selectSpawn', index='0'),
                     act('world.selectLight', index='0'),
                     act('world.selectLightTriangle', index='0'),
                     act('world.selectStatic', index='0'),
                     act('world.selectThing', index='0'),
                     set_status('M: Scanned all 8 room/level loops & reset cursors to current room'),
                 ]),
             ],
             name='M: Scan All 8 World Loops, Refresh Cursors & Probe Save State'),
    ]

    rules = [
        rule(event='render.frame', name='Frame update & 3-page HUD', children=[
            rule(group=True, name='Evaluate conditions & per-frame light', collapsed=True, children=cond_checks),
            rule(conditions=[cond('var.on', name='hud_on')],
                 children=hud_children,
                 name='Draw HUD when enabled'),
        ]),

        rule(event='world.levelLoaded', name='Track level loaded', actions=[
            act('var.add', name='ev_loaded_cnt', value='1'),
            act('var.set', name='ev_last_kind', value='=world_event_int(0)'),
            set_status('Event: LEVEL_LOADED fired'),
        ]),

        rule(event='world.levelUnloaded', name='Track level unloaded', actions=[
            act('var.add', name='ev_unloaded_cnt', value='1'),
            act('var.set', name='ev_last_kind', value='=world_event_int(0)'),
            set_status('Event: LEVEL_UNLOADED fired'),
        ]),

        rule(event='world.roomChanged', name='Track room changes & event info', actions=[
            act('var.add', name='ev_room_cnt', value='1'),
            act('var.set', name='ev_last_kind', value='=world_event_int(0)'),
            act('var.set', name='ev_last_ridx', value='=world_event_int(1)'),
            act('var.set', name='ev_last_rid', value='=world_event_int(2)'),
            act('var.set', name='ev_last_prev', value='=world_event_int(3)'),
            act('var.set', name='ev_last_mx', value='=world_event_int(4)'),
            act('var.set', name='ev_last_my', value='=world_event_int(5)'),
            set_status('Event: ROOM_CHANGED -> room #{ev_last_ridx} (id {ev_last_rid})'),
        ]),

        rule(event='world.timeUp', name='Track bonus countdown time up', actions=[
            act('var.add', name='ev_timeup_cnt', value='1'),
            set_status('Event: TIME_UP fired!'),
        ], children=[
            rule(conditions=[cond('var.on', name='cancel_probe')],
                 actions=[act('world.cancel')],
                 name='Optional unsubscribe probe'),
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

    for item in json.load(open(os.path.join(WORLD_PKG, 'events.json'), encoding='utf-8')):
        if item['id'] not in events:
            missing.append('event ' + item['id'])
    for item in json.load(open(os.path.join(WORLD_PKG, 'conditions.json'), encoding='utf-8')):
        if item['id'] not in conditions:
            missing.append('condition ' + item['id'])
    for item in json.load(open(os.path.join(WORLD_PKG, 'actions.json'), encoding='utf-8')):
        if item['id'] not in actions:
            missing.append('action ' + item['id'])
    for item in json.load(open(os.path.join(WORLD_PKG, 'expressions.json'), encoding='utf-8')):
        if item['text'] not in text_blob:
            missing.append('expression %s (%s)' % (item['id'], item['text']))

    return missing


def main():
    sheet = build_sheet()
    missing = verify_coverage(sheet)
    if missing:
        print('MISSING IN WORLDTEST:')
        for m in missing:
            print('  -', m)
        return 1

    manifest = {
        'api_version': current_api_version(),
        'author': 'AFModConstructor',
        'capabilities': [],
        'description': 'Interactive 3-page showcase of all 67 WorldApi calls',
        'entry': 'bin/worldtest.dll',
        'game_version': '1.0',
        'id': 'worldtest',
        'name': 'worldtest',
        'requires': ['input', 'render', 'world'],
        'version': '1.0.0',
    }
    project = {
        '@project': {
            '@build': '0',
            '@last.file': '',
            '@last.proj': '',
            '@name': 'worldtest',
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

    ev_n = len(json.load(open(os.path.join(WORLD_PKG, 'events.json'), encoding='utf-8')))
    cd_n = len(json.load(open(os.path.join(WORLD_PKG, 'conditions.json'), encoding='utf-8')))
    ac_n = len(json.load(open(os.path.join(WORLD_PKG, 'actions.json'), encoding='utf-8')))
    ex_n = len(json.load(open(os.path.join(WORLD_PKG, 'expressions.json'), encoding='utf-8')))
    print('worldtest ready: events %d/%d, conditions %d/%d, actions %d/%d, expressions %d/%d'
          % (ev_n, ev_n, cd_n, cd_n, ac_n, ac_n, ex_n, ex_n))
    return 0


if __name__ == '__main__':
    sys.exit(main())
