"""Writes examples/apitest: a sheet that touches every entry of the catalog.

The point is not that a person would write this sheet, but that every entry gets built into a
mod at least once - with the presets it ships with, in a place where its context holds. A
subsystem that gains entries and is never tried out is how a broken one sits unnoticed.

Three tiers, because "try everything" and "do not wreck the player's game" both have to hold:

  runs        the mod's own scratch, reading, and drawing - every frame
  on a key    visible or audible, and reversible: sounds, a camera shake, shader draws
  never       anything that writes to the player's files, menus or world; compiled, and gated
              behind a variable that stays off unless a person turns it on

    python tools/make_apitest.py
"""
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROJECT = os.path.join(ROOT, 'examples', 'apitest')
PACKAGES = os.path.join(ROOT, 'packages')
SDK = os.path.join(os.path.dirname(ROOT), 'sdk', 'include')


def current_api_version():
    path = os.path.join(SDK, 'ForgeTypes.h')
    if os.path.exists(path):
        m = re.search(r'^\s*#\s*define\s+AMOD_API_VERSION\s+(\d+)',
                      open(path, encoding='utf-8', errors='ignore').read(), re.M)
        if m:
            return int(m.group(1))
    return 1

# What may run every frame: it touches nothing but the mod's own scratch, or only reads
SAFE = {
    'texture.canvas', 'texture.pixel', 'texture.fill', 'texture.make', 'texture.read',
    'texture.override', 'texture.clearOverride', 'texture.release',
    'font.create', 'font.grid', 'font.glyph', 'font.lineHeight', 'font.draw',
    'block.storeSetNumber', 'block.storeSetText', 'block.storeRemove', 'block.storeClear',
    'block.readNumber', 'block.forget', 'block.objectSetNumber', 'block.objectForget',
    'sav.remember', 'sav.forget',
    'var.set', 'var.add', 'var.setText', 'var.toggle',
    'log.write', 'log.warning', 'console.print',
    'shader.fromSource', 'shader.pixelFromSource', 'shader.fromTokens', 'shader.builtin',
    'shader.registers', 'shader.shapeBegin', 'shader.shapePoint', 'shader.batchBegin',
    'shader.batchAdd', 'shader.drop',
    'files.sizeToVar', 'config.toVar', 'interop.toVar',
    'world.selectRoom', 'world.selectMazeCell',
    'world.selectCell', 'world.snapshotCells',
    'world.selectBox', 'world.selectEntity', 'world.selectPlayerEntity',
    'world.selectSpawn', 'world.selectLight', 'world.selectLightTriangle',
    'world.selectStatic', 'world.readStaticMatrix', 'world.selectThing',
    'bonuses.selectType', 'bonuses.selectPlacement',
    'bonuses.beginType', 'bonuses.typeAssets', 'bonuses.typeTimerAndTrain',
    'bonuses.typeGlow', 'bonuses.typeRockingAndLight',
    'inventory.selectSlot',
    'monsters.selectType', 'monsters.selectTypeBox',
    'monsters.selectLive',
    'monsters.selectLiveBox', 'monsters.selectPlaced',
    'particles.select',
}

# Visible or audible, so they wait for a key rather than firing sixty times a second
ON_KEY = {
    'sound.play', 'sound.music', 'sound.playAt', 'sound.move', 'sound.stop', 'sound.stopMusic',
    'camera.shake', 'shader.use', 'shader.shapeDraw', 'shader.drawModel', 'shader.batchDraw',
    'shader.constant', 'texture.save',
}

# Events whose handler is the only place some entries make sense
CONTEXT_EVENT = {
    'render': 'render.frame',
    'item_style': 'render.itemStyle',
    'slot_save': 'sav.slotSaving',
    'slot_load': 'sav.slotLoading',
    'camera_view': 'camera.view',
    'particle_trail': 'particles.trailSpawning',
    'demo_input': 'demo.input',
    'player_move': 'player.onMove',
    'player_death': 'player.beforeDeath',
    'bonus_take': 'bonuses.beforeTake',
    'bonus_spawn': 'bonuses.beforeSpawn',
    'bonus_body': 'bonuses.kindBuild',
    'bonus_kind_update': 'bonuses.kindUpdate',
    'inv_slot_event': 'inventory.slotEmptied',
    'inv_chain_event': 'inventory.chainChanged',
    'monster_kill': 'monsters.beforeKill',
    'monster_touch': 'monsters.beforeTouch',
    'monster_spawn': 'monsters.spawned',
    'monster_place': 'monsters.beforePlace',
    'monster_think': 'monsters.behaviourTick',
    'monster_body': 'monsters.kindBuild',
}

# A loop variable of its own for each looping condition, so two of them never collide
LOOP_VAR = {
    'system.repeat': 'i',
    'system.for': 'j',
    'system.while': 'k',
    'particles.forEach': 'p_idx',
    'world.forEachRoom': 'w_room_i',
    'world.forEachBox': 'w_box_i',
    'world.forEachEntity': 'w_ent_i',
    'world.forEachSpawn': 'w_spw_i',
    'world.forEachLight': 'w_lgt_i',
    'world.forEachLightTri': 'w_tri_i',
    'world.forEachStatic': 'w_stc_i',
    'world.forEachThing': 'w_thg_i',
    'bonuses.forEachPlacement': 'b_pl_i',
    'inventory.forEachSlot': 'inv_s_i',
    'monsters.forEachLive': 'm_lv_i',
    'monsters.forEachPlaced': 'm_pl_i',
}

# An expression that reads a value of its event only stands inside that event
EVENT_EXPRESSION = {
    'sav.slotOfEvent': 'sav.slotSaving',
    'camera.updateDelta': 'camera.update',
    'camera.placeX': 'camera.placing',
    'camera.placeY': 'camera.placing',
    'camera.placeZ': 'camera.placing',
    'particles.birthKind': 'particles.birth',
    'particles.thinkDelta': 'particles.think',
    'particles.trailX': 'particles.trailSpawning',
    'particles.trailY': 'particles.trailSpawning',
    'particles.trailZ': 'particles.trailSpawning',
    'particles.trailWaypoint': 'particles.trailSpawning',
    'demo.eventKind': 'demo.stateChanged',
    'demo.eventScript': 'demo.stateChanged',
    'demo.eventMove': 'demo.moveStarted',
    'demo.inputX': 'demo.input',
    'demo.inputY': 'demo.input',
    'demo.inputZ': 'demo.input',
    'demo.inputHorz': 'demo.input',
    'demo.inputVert': 'demo.input',
    'demo.inputJump': 'demo.input',
    'demo.inputSeconds': 'demo.input',
    'world.eventKind': 'world.roomChanged',
    'world.eventRoomIndex': 'world.roomChanged',
    'world.eventRoomId': 'world.roomChanged',
    'world.eventPrevRoom': 'world.roomChanged',
    'world.eventMazeX': 'world.roomChanged',
    'world.eventMazeY': 'world.roomChanged',
    'player.eventKind': 'player.died',
    'player.eventLives': 'player.livesChanged',
    'player.eventWasLives': 'player.livesChanged',
    'player.eventScore': 'player.scoreChanged',
    'player.eventWasScore': 'player.scoreChanged',
    'player.eventCrashed': 'player.died',
    'player.deathKind': 'player.beforeDeath',
    'player.deathLives': 'player.beforeDeath',
    'player.deathX': 'player.beforeDeath',
    'player.deathY': 'player.beforeDeath',
    'player.deathZ': 'player.beforeDeath',
    'player.frameDelta': 'player.drive',
    'player.frameX': 'player.drive',
    'player.frameY': 'player.drive',
    'player.frameZ': 'player.drive',
    'player.frameFall': 'player.drive',
    'player.frameState': 'player.drive',
    'player.frameSubState': 'player.drive',
    'player.moveDirX': 'player.onMove',
    'player.moveDirY': 'player.onMove',
}

KEY = '84'   # T, as the catalog itself lists the keys: by number


def read(package, name):
    path = os.path.join(PACKAGES, package, 'catalog', name)
    return json.load(open(path, encoding='utf-8')) if os.path.exists(path) else []


def catalog():
    out = {kind: [] for kind in ('events', 'conditions', 'actions', 'expressions')}
    for package in sorted(os.listdir(PACKAGES)):
        if not os.path.isdir(os.path.join(PACKAGES, package)):
            continue
        for kind in out:
            for item in read(package, kind + '.json'):
                item['_package'] = package
                out[kind].append(item)
    return out


def rule(event='', conditions=(), actions=(), children=(), name='', group=False):
    return {'event': event, 'filters': {}, 'conditions': list(conditions),
            'actions': list(actions), 'children': list(children), 'veto': False,
            'enabled': True, 'group': group, 'name': name, 'collapsed': True}


def action(entry_id, **args):
    return {'id': entry_id, 'args': {k: str(v) for k, v in args.items()}}


def condition(entry_id, inverted=False, **args):
    out = {'id': entry_id, 'args': {k: str(v) for k, v in args.items()}}
    if inverted:
        out['inverted'] = True
    return out


def count(name):
    return action('var.add', name=name, value='1')


def loader_subsystems(book):
    """What the mod must ask the loader for: the packages that are subsystems of AzForge.

    The studio's own package of maths, variables and loops is not one, and a manifest naming it
    is refused before the mod is mapped.
    """
    out = set()
    for kind in book:
        for item in book[kind]:
            package = item['_package']
            path = os.path.join(PACKAGES, package, 'package.json')
            manifest = json.load(open(path, encoding='utf-8')) if os.path.exists(path) else {}
            if manifest.get('loader', True):
                out.add(package)
    return out


def main():
    book = catalog()
    variables = [
        {'name': 'writes_on', 'type': 'bool', 'value': '0'},
        {'name': 'checks', 'type': 'int', 'value': '0'},
        {'name': 'probe_i', 'type': 'int', 'value': '0'},
        {'name': 'probe_f', 'type': 'float', 'value': '0.0'},
        {'name': 'probe_t', 'type': 'text', 'value': ''},
        {'name': 'value', 'type': 'float', 'value': '0.0'},
        {'name': 'probe_list', 'type': 'list', 'value': '64'},
        {'name': 'mylist', 'type': 'list', 'value': '16'},
    ]

    # ---- expressions: each one assigned into a variable of its kind --------------------
    expression_rules = []
    event_expressions = {}
    for item in book['expressions']:
        text = item['text']
        if item.get('kind') == 'text':
            step = action('var.setText', name='probe_t', value='{$' + text + '}')
        else:
            name = 'probe_f' if item.get('kind') == 'float' else 'probe_i'
            step = action('var.set', name=name, value='=' + text)
        where = EVENT_EXPRESSION.get(item['id']) or CONTEXT_EVENT.get(item.get('needs', ''))
        if where:
            event_expressions.setdefault(where, []).append(rule(actions=[step], name=item['id']))
        else:
            expression_rules.append(rule(actions=[step], name=item['id']))

    # ---- conditions: each one alone in a rule that counts when it holds ----------------
    by_context = {}
    condition_rules = []
    for item in book['conditions']:
        args = {}
        for param in item.get('params', []):
            if param['key'] == 'var':
                args[param['key']] = LOOP_VAR.get(item['id'], 'i')
            # A variable field is empty in the catalog: the sheet says which one it means
            elif param.get('label') == 'mod.p.varName':
                args[param['key']] = 'probe_t' if item['id'] == 'var.textIs' else 'value'
            if param.get('label') in ('mod.p.listName', 'mod.p.matrixList', 'mod.p.vertexList'):
                args[param['key']] = 'probe_list'
        has_state_param = any(p.get('key') == 'state' for p in item.get('params', []))
        if '%state%' in item.get('emit', '') and not item.get('state') and not has_state_param:
            condition_rules.append(rule(conditions=[condition(item['id'])],
                                        actions=[count('checks')], name=item['id']))
            continue
        cond_where = CONTEXT_EVENT.get(item.get('needs', ''))
        if cond_where and cond_where != 'render.frame':
            by_context.setdefault(cond_where, []).append(
                rule(conditions=[condition(item['id'], **args)],
                     actions=[count('checks')], name=item['id']))
            continue
        condition_rules.append(rule(conditions=[condition(item['id'], **args)],
                                    actions=[count('checks')], name=item['id']))

    # ---- actions: by tier, and by the context each one needs ---------------------------
    tiers = {'safe': [], 'key': [], 'writes': []}
    in_loop = []
    for item in book['actions']:
        needs = item.get('needs', '')
        where = CONTEXT_EVENT.get(needs)

        args = {}
        for param in item.get('params', []):
            # A variable field is empty in the catalog, and an action writes into it
            if param.get('label') == 'mod.p.varName':
                args[param['key']] = 'probe_t' if item['id'] == 'var.setText' else 'value'
            if param.get('label') in ('mod.p.listName', 'mod.p.matrixList', 'mod.p.vertexList'):
                args[param['key']] = 'probe_list'
        step = rule(actions=[action(item['id'], **args)], name=item['id'])

        if needs == 'token':
            by_context.setdefault(item['_package'], []).append(step)
            continue
        if needs == 'loop':
            in_loop.append(step)
            continue
        if where and where != 'render.frame':
            by_context.setdefault(where, []).append(step)
            continue

        tier = 'safe' if item['id'] in SAFE else ('key' if item['id'] in ON_KEY else 'writes')
        tiers[tier].append(step)

    # ---- the sheet ---------------------------------------------------------------------
    frame_children = [
        rule(conditions=[condition('render.ready')],
             children=[rule(actions=[action('texture.canvas', canvas='probe', w=32, h=32)],
                            name='холст для картинок')],
             name='подготовка'),
        rule(children=expression_rules, group=True, name='выражения: %d' % len(expression_rules)),
        rule(children=condition_rules, group=True, name='условия: %d' % len(condition_rules)),
        rule(children=tiers['safe'], group=True, name='действия без последствий: %d' % len(tiers['safe'])),
        rule(conditions=[condition('input.keyDown', key=KEY)],
             children=tiers['key'], name='по F9: звук, тряска, шейдеры (%d)' % len(tiers['key'])),
        rule(conditions=[condition('var.on', name='writes_on')],
             children=tiers['writes'],
             name='только при writes_on: пишут в игру (%d)' % len(tiers['writes'])),
        rule(conditions=[condition('var.on', name='writes_on'),
                         condition('system.repeat', count=1, var='i')],
             children=in_loop, name='внутри цикла: %d' % len(in_loop)),
        rule(actions=[action('render.textAligned', text='apitest: {checks} проверок, кадр {probe_i}',
                             x=16, y=16, font='', scale=0.5, align='0x08', r=0.8, g=0.85, b=1, a=1)],
             name='видно, что живо'),
    ]

    rules = [rule(event='render.frame', children=frame_children, name='каждый кадр')]

    # every event of the catalog gets a rule, so subscribing itself is tried as well
    for item in book['events']:
        if item['id'] == 'render.frame':
            continue
        name = 'ev_' + item['id'].replace('.', '_')
        variables.append({'name': name, 'type': 'int', 'value': '0'})
        inside = [rule(actions=[count(name)], name='отметить')]
        inside += event_expressions.get(item['id'], [])
        # the cancels of this subsystem: they need a rule that holds a subscription
        for step in by_context.pop(item['_package'], []):
            inside.append(rule(conditions=[condition('var.on', name='writes_on')],
                               children=[step], name=step['name']))
        for step in by_context.get(item['id'], []):
            inside.append(rule(conditions=[condition('var.on', name='writes_on')],
                               children=[step], name=step['name']))
        rules.append(rule(event=item['id'], children=inside, name=item['id']))

    sheet = {'name': 'main', 'vars': variables, 'rules': rules}

    manifest = {'api_version': current_api_version(), 'author': 'AFModConstructor', 'capabilities': [],
                'description': 'Every entry of the catalog, built at least once',
                'entry': 'bin/apitest.dll', 'game_version': '1.0', 'id': 'apitest',
                'name': 'apitest',
                'requires': sorted(loader_subsystems(book)),
                'version': '1.0.0'}

    project = {'@project': {'@build': '0', '@last.file': '', '@last.proj': '', '@name': 'apitest',
                            '@name.game': '', '@name.pak': '', '@path.game': '',
                            '@proj.dir': '', '@type': '1'}}

    os.makedirs(os.path.join(PROJECT, 'logic'), exist_ok=True)
    for path, data in ((os.path.join(PROJECT, 'logic', 'main.json'), sheet),
                       (os.path.join(PROJECT, 'mod.json'), manifest),
                       (os.path.join(PROJECT, 'proj.json'), project)):
        with open(path, 'w', encoding='utf-8', newline='') as f:
            json.dump(data, f, ensure_ascii=False, indent=4)
            f.write('\n')

    print('выражений %d, условий %d, действий %d (без последствий %d, по клавише %d, за флагом %d)'
          % (len(expression_rules), len(condition_rules),
             sum(len(v) for v in tiers.values()) + sum(len(v) for v in by_context.values()),
             len(tiers['safe']), len(tiers['key']), len(tiers['writes'])))
    print('событий %d, переменных %d' % (len(book['events']), len(variables)))
    print('всего записей в листе:',
          len(expression_rules) + len(condition_rules) + len(book['events']) +
          sum(len(v) for v in tiers.values()) + sum(len(v) for v in by_context.values()))
    return 0


if __name__ == '__main__':
    sys.exit(main())
