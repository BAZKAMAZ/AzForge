"""Writes examples/particlestest: an interactive in-game showcase of the entire Particles package.

Every event (4), condition (9), action (35), and expression (50) of packages/particles is wired
into a live HUD + hotkey showcase so every ParticlesApi call can be inspected and triggered in-game.

    python tools/make_particlestest.py
"""
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROJECT = os.path.join(ROOT, 'examples', 'particlestest')
PART_PKG = os.path.join(ROOT, 'packages', 'particles', 'catalog')
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
        {'name': 'trail_lift', 'type': 'bool', 'value': '0'},
        {'name': 'skip_draw', 'type': 'bool', 'value': '0'},
        {'name': 'phase', 'type': 'float', 'value': '0.0'},
        {'name': 'birth_total', 'type': 'int', 'value': '0'},
        {'name': 'birth_last_kind', 'type': 'int', 'value': '-1'},
        {'name': 'trail_events', 'type': 'int', 'value': '0'},
        {'name': 'trail_x', 'type': 'float', 'value': '0.0'},
        {'name': 'trail_y', 'type': 'float', 'value': '0.0'},
        {'name': 'trail_z', 'type': 'float', 'value': '0.0'},
        {'name': 'trail_wp', 'type': 'float', 'value': '0.0'},
        {'name': 'think_ticks', 'type': 'int', 'value': '0'},
        {'name': 'think_dt', 'type': 'float', 'value': '0.0'},
        {'name': 'draw_ticks', 'type': 'int', 'value': '0'},
        {'name': 'pool2_seen', 'type': 'int', 'value': '0'},
        {'name': 'insp_x', 'type': 'float', 'value': '0.0'},
        {'name': 'insp_y', 'type': 'float', 'value': '0.0'},
        {'name': 'insp_z', 'type': 'float', 'value': '0.0'},
        {'name': 'insp_dx', 'type': 'float', 'value': '0.0'},
        {'name': 'insp_dy', 'type': 'float', 'value': '0.0'},
        {'name': 'insp_dz', 'type': 'float', 'value': '0.0'},
        {'name': 'insp_spd', 'type': 'float', 'value': '0.0'},
        {'name': 'insp_sz', 'type': 'float', 'value': '0.0'},
        {'name': 'insp_life', 'type': 'float', 'value': '0.0'},
        {'name': 'insp_dec', 'type': 'float', 'value': '0.0'},
        {'name': 'insp_r', 'type': 'float', 'value': '0.0'},
        {'name': 'insp_g', 'type': 'float', 'value': '0.0'},
        {'name': 'insp_b', 'type': 'float', 'value': '0.0'},
        {'name': 'insp_a', 'type': 'float', 'value': '0.0'},
        {'name': 'insp_gx', 'type': 'float', 'value': '0.0'},
        {'name': 'insp_gy', 'type': 'float', 'value': '0.0'},
        {'name': 'insp_gz', 'type': 'float', 'value': '0.0'},
        {'name': 'insp_sg', 'type': 'float', 'value': '0.0'},
        {'name': 'insp_zg', 'type': 'float', 'value': '0.0'},
        {'name': 'insp_ang', 'type': 'float', 'value': '0.0'},
        {'name': 'insp_spin', 'type': 'float', 'value': '0.0'},
        {'name': 'insp_gsx', 'type': 'float', 'value': '0.0'},
        {'name': 'insp_gsy', 'type': 'float', 'value': '0.0'},
        {'name': 'insp_wp', 'type': 'float', 'value': '0.0'},
        {'name': 'insp_shape', 'type': 'int', 'value': '0'},
        {'name': 'insp_mdl', 'type': 'int', 'value': '0'},
        {'name': 'insp_frm', 'type': 'int', 'value': '0'},
        {'name': 'insp_par', 'type': 'int', 'value': '0'},
        {'name': 'insp_kind', 'type': 'int', 'value': '-1'},
        {'name': 'insp_tag', 'type': 'int', 'value': '0'},
        {'name': 'st_here', 'type': 'text', 'value': 'NO'},
        {'name': 'st_font', 'type': 'text', 'value': 'NO'},
        {'name': 'st_busy', 'type': 'text', 'value': 'idle'},
        {'name': 'last_cmd', 'type': 'text', 'value': 'Ready - enter a level and press 1..0'},
    ]

    cond_checks = [
        rule(conditions=[cond('particles.isHere')],
             actions=[act('var.setText', name='st_here', value='YES')]),
        rule(conditions=[cond('particles.isHere', inverted=True)],
             actions=[act('var.setText', name='st_here', value='NO')]),
        rule(conditions=[cond('particles.hasTextFont')],
             actions=[act('var.setText', name='st_font', value='READY')]),
        rule(conditions=[cond('particles.hasTextFont', inverted=True)],
             actions=[act('var.setText', name='st_font', value='NO')]),
        rule(conditions=[cond('particles.countIs', kind='-1', op='>', value=0)],
             actions=[act('var.setText', name='st_busy', value='ACTIVE')]),
        rule(conditions=[cond('particles.countIs', kind='-1', op='==', value=0)],
             actions=[act('var.setText', name='st_busy', value='empty')]),
    ]

    inspector_rules = [
        rule(actions=[
            act('var.add', name='phase', value='=mod_delta * 3.2f'),
            act('particles.select', by='0', kind='-1', index=0),
            act('var.set', name='insp_kind', value='=part_kind_of()'),
            act('var.set', name='insp_tag', value='=((int)part_tag_of())'),
        ], name='Pick newest particle in any store'),
        rule(conditions=[cond('particles.hasPicked')], actions=[
            act('var.set', name='insp_x', value='=part_num(0)'),
            act('var.set', name='insp_y', value='=part_num(1)'),
            act('var.set', name='insp_z', value='=part_num(2)'),
            act('var.set', name='insp_dx', value='=part_num(3)'),
            act('var.set', name='insp_dy', value='=part_num(4)'),
            act('var.set', name='insp_dz', value='=part_num(5)'),
            act('var.set', name='insp_spd', value='=part_num(6)'),
            act('var.set', name='insp_sz', value='=part_num(7)'),
            act('var.set', name='insp_life', value='=part_num(8)'),
            act('var.set', name='insp_dec', value='=part_num(9)'),
            act('var.set', name='insp_r', value='=part_num(10)'),
            act('var.set', name='insp_g', value='=part_num(11)'),
            act('var.set', name='insp_b', value='=part_num(12)'),
            act('var.set', name='insp_a', value='=part_num(13)'),
            act('var.set', name='insp_gx', value='=part_num(14)'),
            act('var.set', name='insp_gy', value='=part_num(15)'),
            act('var.set', name='insp_gz', value='=part_num(16)'),
            act('var.set', name='insp_sg', value='=part_num(17)'),
            act('var.set', name='insp_zg', value='=part_num(18)'),
            act('var.set', name='insp_ang', value='=part_num(19)'),
            act('var.set', name='insp_spin', value='=part_num(20)'),
            act('var.set', name='insp_gsx', value='=part_num(21)'),
            act('var.set', name='insp_gsy', value='=part_num(22)'),
            act('var.set', name='insp_wp', value='=part_num(23)'),
            act('var.set', name='insp_shape', value='=part_int(0)'),
            act('var.set', name='insp_mdl', value='=part_int(1)'),
            act('var.set', name='insp_frm', value='=part_int(2)'),
            act('var.set', name='insp_par', value='=part_int(3)'),
        ], name='Snapshot all 28 fields of picked particle'),
    ]

    hotkeys = [
        rule(event='input.key', filters={'key': '112'},
             actions=[act('var.toggle', name='hud_on')],
             name='F1: Toggle HUD'),

        rule(event='input.key', filters={'key': '49'},
             conditions=[cond('particles.isHere'), cond('system.repeat', count=12, var='s_i')],
             actions=[
                 act('particles.spawnSprite',
                     kind='AMOD_PARTICLE_KIND_POOL_0',
                     shape='AMOD_PARTICLE_SHAPE_SPRITE',
                     texA='textures/star.tga', texB='',
                     x='=player_position(0)',
                     y='=player_position(1) + 0.35f',
                     z='=player_position(2)',
                     dx='=cosf((float)s_i * 0.5236f)',
                     dy='1.35',
                     dz='=sinf((float)s_i * 0.5236f)',
                     speed='0.95', size='0.18', life='1.0', decay='0.45',
                     r='1.0', g='0.88', b='0.30', a='1.0'),
                 act('particles.setGravity', gx='0', gy='-1.5', gz='0'),
                 act('particles.setGrowth', speedGrow='-0.15', sizeGrow='-0.03'),
                 act('particles.setTurn', angle='0', spin='6.0'),
                 act('particles.setTag', tag='1'),
                 set_status('1: Spawned 12 golden star fountain particles (tag 1)'),
             ],
             name='1: Golden Star Fountain (Pool 0 + gravity + spin + tag 1)'),

        rule(event='input.key', filters={'key': '50'},
             conditions=[cond('particles.isHere')],
             actions=[
                 act('particles.createAnimation', name='pulse',
                     frames='textures/part_red.jpg;textures/part_green.jpg;textures/part_blue.jpg;textures/flare.bmp'),
                 set_status('2: Spawned 10 flipbook particles in Pool 1 (tag 2)'),
             ],
             children=[
                 rule(conditions=[cond('system.repeat', count=10, var='f_i')],
                      actions=[
                          act('particles.spawnSprite',
                              kind='AMOD_PARTICLE_KIND_POOL_1',
                              shape='AMOD_PARTICLE_SHAPE_SPRITE',
                              texA='textures/part_red.jpg', texB='',
                              x='=player_position(0) + cosf((float)f_i * 0.6283f) * 0.25f',
                              y='=player_position(1) + 0.35f',
                              z='=player_position(2) + sinf((float)f_i * 0.6283f) * 0.25f',
                              dx='=cosf((float)f_i * 0.6283f) * 0.4f',
                              dy='0.85',
                              dz='=sinf((float)f_i * 0.6283f) * 0.4f',
                              speed='0.55', size='0.22', life='1.0', decay='0.35',
                              r='1.0', g='1.0', b='1.0', a='1.0'),
                          act('particles.setAnimation', name='pulse', fps='12'),
                          act('particles.setTag', tag='2'),
                      ]),
             ],
             name='2: Flipbook Animation Burst (Pool 1 + CreateAnimation)'),

        rule(event='input.key', filters={'key': '51'},
             conditions=[cond('particles.isHere'), cond('system.repeat', count=8, var='t_i')],
             actions=[
                 act('particles.spawnSprite',
                     kind='AMOD_PARTICLE_KIND_POOL_0',
                     shape='AMOD_PARTICLE_SHAPE_SPRITE',
                     texA='textures/firework.tga', texB='',
                     x='=player_position(0) + cosf((float)t_i * 0.7854f) * 0.45f',
                     y='=player_position(1) + 0.25f + (float)t_i * 0.05f',
                     z='=player_position(2) + sinf((float)t_i * 0.7854f) * 0.45f',
                     dx='0', dy='1', dz='0',
                     speed='0.2', size='0.20', life='1.0', decay='0.25',
                     r='0.4', g='0.95', b='1.0', a='1.0'),
                 act('particles.setTag', tag='3'),
                 act('particles.setThink'),
                 set_status('3: Spawned 8 custom-think helix particles (tag 3)'),
             ],
             name='3: Custom Think Helix (SetThink + tag 3)'),

        rule(event='input.key', filters={'key': '52'},
             conditions=[cond('particles.isHere')],
             actions=[
                 act('particles.spawnText', text='+500 AZFORGE',
                     x='=player_position(0)',
                     y='=player_position(1) + 0.55f',
                     z='=player_position(2)',
                     scaleX='1.1', scaleY='1.1',
                     speed='0.14', decay='0.45',
                     dx='0', dy='1', dz='0', gravity='0'),
                 act('particles.spawnGlyph',
                     kind='AMOD_PARTICLE_KIND_POOL_0',
                     frame='0', scale='1.6',
                     x='=player_position(0) - 0.35f',
                     y='=player_position(1) + 0.45f',
                     z='=player_position(2)',
                     dx='-0.3', dy='1', dz='0',
                     speed='0.4', life='1.0', decay='0.45',
                     r='1.0', g='0.9', b='0.3', a='1.0'),
                 act('particles.spawnModel',
                     kind='AMOD_PARTICLE_KIND_POOL_0',
                     model='=part_text_font()', frame='1',
                     scaleX='=part_text_scale(0) * 1.6f',
                     scaleY='=part_text_scale(1) * 1.6f',
                     x='=player_position(0) + 0.35f',
                     y='=player_position(1) + 0.45f',
                     z='=player_position(2)',
                     dx='0.3', dy='1', dz='0',
                     speed='0.4', life='1.0', decay='0.45'),
                 set_status('4: Spawned 3D floating text + glyph + model particles'),
             ],
             name='4: Floating 3D Score Text + Glyph + Model'),

        rule(event='input.key', filters={'key': '53'},
             conditions=[cond('particles.isHere'), cond('system.repeat', count=8, var='u_i')],
             actions=[
                 act('particles.spawnTrail',
                     shape='AMOD_PARTICLE_SHAPE_SPRITE',
                     texA='textures/part_blue.jpg',
                     texB='textures/flare.bmp',
                     x='=player_position(0) + cosf((float)u_i * 0.7854f) * 0.4f',
                     y='=player_position(1) + 0.35f',
                     z='=player_position(2) + sinf((float)u_i * 0.7854f) * 0.4f',
                     waypoint='0', speed='0.35', size='0.17',
                     life='1.0', decay='0.45',
                     r='0.45', g='0.85', b='1.0', a='1.0'),
                 act('particles.setWaypoint', waypoint='0'),
                 set_status('5: Spawned 8 two-layer Uni trail particles'),
             ],
             name='5: Two-Layer Bonus Trail Ring (Uni)'),

        rule(event='input.key', filters={'key': '54'},
             conditions=[cond('particles.isHere'), cond('system.repeat', count=6, var='p2_i')],
             actions=[
                 act('particles.spawnSprite',
                     kind='AMOD_PARTICLE_KIND_POOL_2',
                     shape='AMOD_PARTICLE_SHAPE_QUAD',
                     texA='textures/dlight.jpg', texB='',
                     x='=player_position(0)',
                     y='=player_position(1) + 0.4f',
                     z='=player_position(2)',
                     dx='=cosf((float)p2_i * 1.0472f)',
                     dy='0.5',
                     dz='=sinf((float)p2_i * 1.0472f)',
                     speed='0.3', size='0.2', life='1.0', decay='0.25',
                     r='0.8', g='0.4', b='1.0', a='1.0'),
                 set_status('6: Spawned 6 Pool 2 particles (stepped, hidden from game draw)'),
             ],
             name='6: Pool 2 Custom-Draw Store'),

        rule(event='input.key', filters={'key': '55'},
             conditions=[cond('particles.isHere')],
             actions=[
                 act('particles.select', by='1', tag='1', index='0'),
                 set_status('7: Mutated all tag-1 particles (colour/size/speed/textures)'),
             ],
             children=[
                 rule(conditions=[cond('particles.forEach', by='1', tag='1', var='m_i')],
                      actions=[
                          act('particles.setColour', r='0.2', g='1.0', b='0.85', a='1.0'),
                          act('particles.setVelocity', dx='0', dy='1', dz='0', speed='0.75'),
                          act('particles.setSpeed', speed='0.8'),
                          act('particles.setSize', size='0.26'),
                          act('particles.setLife', life='1.0', decay='0.35'),
                          act('particles.setTextures', texA='textures/flare.bmp', texB='textures/star.tga'),
                          act('particles.setShape', shape='AMOD_PARTICLE_SHAPE_SPRITE'),
                          act('particles.setParent', parent='0'),
                      ]),
                 rule(conditions=[cond('particles.hasPicked'),
                                  cond('particles.kindIs', kind='AMOD_PARTICLE_KIND_POOL_0'),
                                  cond('particles.fieldIs', field='8', op='>', value='0.99')],
                      actions=[
                          act('particles.setModel', model='0', frame='0'),
                          act('particles.setGlyphScale', scaleX='0.006', scaleY='0.006'),
                      ]),
             ],
             name='7: Mutate All Tag-1 Particles (Set fields + forEach tag)'),

        rule(event='input.key', filters={'key': '56'},
             conditions=[cond('particles.isHere')],
             actions=[
                 act('particles.select', by='1', tag='3', index='0'),
                 act('particles.clearThink', scope='0'),
                 act('particles.clearThink', scope='1', tag='3'),
                 act('particles.select', by='1', tag='2', index='0'),
                 act('particles.kill', scope='0'),
                 act('particles.kill', scope='1', tag='2'),
                 set_status('8: Restored game step on tag 3 & killed tag 2'),
             ],
             name='8: ClearThink on Tag 3 & Kill Tag 2'),

        rule(event='input.key', filters={'key': '57'},
             actions=[
                 act('var.toggle', name='trail_lift'),
                 set_status('9: Toggled bonus trail spawn +0.5Y lift'),
             ],
             name='9: Toggle Trail Spawn Lift'),

        rule(event='input.key', filters={'key': '48'},
             conditions=[cond('particles.isHere')],
             actions=[
                 act('particles.clear', kind='-1'),
                 set_status('0: Cleared all 4 particle stores'),
             ],
             name='0: Clear All Particle Stores'),

        rule(event='input.key', filters={'key': '116'},
             actions=[
                 act('particles.clearHandler', which='0'),
                 set_status('F5: Cleared birth, trail-spawn & draw handlers'),
             ],
             name='F5: Detach Particle Handlers'),

        rule(event='input.key', filters={'key': '117'},
             actions=[
                 act('particles.restoreHandlers'),
                 set_status('F6: Re-attached sheet particle handlers'),
             ],
             name='F6: Restore Particle Handlers'),

        rule(event='input.key', filters={'key': '118'},
             actions=[
                 act('var.toggle', name='skip_draw'),
                 set_status('F7: Toggled skipping game particle draw pass'),
             ],
             name='F7: Toggle Skip Game Particle Draw'),
    ]

    hud_children = [
        rule(actions=[
            act('render.rect', x=10, y=10, w=620, h=222, r=0.05, g=0.07, b=0.11, a=0.78),
            act('render.outline', x=10, y=10, w=620, h=222, thickness=1, r=0.30, g=0.62, b=0.95, a=0.90),
            hud_line(15,
                     'PARTICLES API TEST [F1 HUD]  Here:{st_here}({#(API->particles ? API->particles->IsHere(SELF) : 0)})  '
                     'Font:{st_font}({#part_text_font()})  State:{st_busy}',
                     0.45, 0.86, 1.0),
            hud_line(31,
                     'Counts All:{#part_count(-1)}/{#part_capacity(-1)}  '
                     'P0:{#part_count(AMOD_PARTICLE_KIND_POOL_0)}/{#part_capacity(AMOD_PARTICLE_KIND_POOL_0)}  '
                     'P1:{#part_count(AMOD_PARTICLE_KIND_POOL_1)}  '
                     'P2:{#part_count(AMOD_PARTICLE_KIND_POOL_2)}  '
                     'Uni:{#part_count(AMOD_PARTICLE_KIND_UNI)}/{#part_capacity(AMOD_PARTICLE_KIND_UNI)}'),
            hud_line(47,
                     'Tag1:{#part_count_tagged(1)}  Anim(pulse):{#part_anim_id("pulse")}  '
                     'TextScale:({~part_text_scale(0)}, {~part_text_scale(1)})'),
            hud_line(63,
                     'Events Birth:{birth_total}(lastKind:{birth_last_kind})  '
                     'Think:{think_ticks}(dt:{think_dt})  '
                     'Draw:{draw_ticks}(P2:{pool2_seen})  '
                     'Trail:{trail_events}'),
            hud_line(79,
                     'Last Trail Spawn Pos:({trail_x}, {trail_y}, {trail_z}) wp:{trail_wp}  '
                     'Lift[9]:{trail_lift}  SkipDraw[F7]:{skip_draw}'),
            hud_line(95,
                     'Newest Picked Kind:{insp_kind} Tag:{insp_tag} Shape:{insp_shape} '
                     'Pos:({insp_x}, {insp_y}, {insp_z})'),
            hud_line(111,
                     'Dir:({insp_dx}, {insp_dy}, {insp_dz}) Spd:{insp_spd} Size:{insp_sz} '
                     'Life:{insp_life} Decay:{insp_dec}'),
            hud_line(127,
                     'RGBA:({insp_r}, {insp_g}, {insp_b}, {insp_a}) '
                     'Grav:({insp_gx}, {insp_gy}, {insp_gz}) '
                     'Grow:({insp_sg}, {insp_zg})'),
            hud_line(143,
                     'Ang:{insp_ang} Spin:{insp_spin} Mdl:{insp_mdl}:#{insp_frm} '
                     'Glyph:({insp_gsx}, {insp_gsy}) Par:{insp_par} WP:{insp_wp}'),
            hud_line(163,
                     '[1]Stars(tag1) [2]Flipbook(tag2) [3]ThinkHelix(tag3) [4]3DText+Glyph [5]UniTrail',
                     0.75, 0.92, 0.78),
            hud_line(179,
                     '[6]Pool2 [7]MutateTag1 [8]ClearThink3+Kill2 [9]TrailLift [0]ClearAll',
                     0.75, 0.92, 0.78),
            hud_line(195,
                     '[F5]DetachHandlers [F6]RestoreHandlers [F7]SkipGameDraw',
                     0.75, 0.92, 0.78),
            hud_line(213, 'Last: {last_cmd}', 1.0, 0.88, 0.45),
        ], name='Draw telemetry HUD'),
    ]

    rules = [
        rule(event='render.frame', name='Frame update & HUD', children=[
            rule(group=True, name='Evaluate conditions', collapsed=True, children=cond_checks),
            rule(group=True, name='Snapshot newest particle', collapsed=True, children=inspector_rules),
            rule(conditions=[cond('var.on', name='hud_on')],
                 children=hud_children,
                 name='HUD when enabled'),
        ]),

        rule(event='particles.birth', name='Count every particle allocation', actions=[
            act('var.add', name='birth_total', value='1'),
            act('var.set', name='birth_last_kind', value='=part_ev_birth_kind()'),
        ]),

        rule(event='particles.trailSpawning', name='Inspect & optionally lift bonus trails', actions=[
            act('var.add', name='trail_events', value='1'),
            act('var.set', name='trail_x', value='=part_ev_trail_num(0)'),
            act('var.set', name='trail_y', value='=part_ev_trail_num(1)'),
            act('var.set', name='trail_z', value='=part_ev_trail_num(2)'),
            act('var.set', name='trail_wp', value='=part_ev_trail_num(3)'),
        ], children=[
            rule(conditions=[cond('var.on', name='trail_lift')],
                 actions=[
                     act('particles.trailSetPos',
                         x='=part_ev_trail_num(0)',
                         y='=part_ev_trail_num(1) + 0.5f',
                         z='=part_ev_trail_num(2)'),
                 ],
                 name='Lift trail +0.5Y when enabled'),
        ]),

        rule(event='particles.think', name='Helix motion for tag-3 particles', actions=[
            act('var.add', name='think_ticks', value='1'),
            act('var.set', name='think_dt', value='=part_ev_think_dt()'),
        ], children=[
            rule(conditions=[cond('particles.tagIs', tag='3')],
                 actions=[
                     act('particles.setPos',
                         x='=part_num(0) - sinf(phase + part_num(1) * 4.0f) * 0.9f * part_ev_think_dt()',
                         y='=part_num(1) + 0.45f * part_ev_think_dt()',
                         z='=part_num(2) + cosf(phase + part_num(1) * 4.0f) * 0.9f * part_ev_think_dt()'),
                     act('particles.setLife',
                         life='=part_num(8) - 0.30f * part_ev_think_dt()',
                         decay='0.30'),
                 ],
                 children=[
                     rule(conditions=[cond('particles.fieldIs', field='8', op='<=', value='0.02')],
                          actions=[act('particles.kill', scope='0')],
                          name='Finish when life spent'),
                 ],
                 name='Swirl upward & spend life'),
        ]),

        rule(event='particles.draw', name='Count draw passes & walk Pool 2', actions=[
            act('var.add', name='draw_ticks', value='1'),
            act('var.set', name='pool2_seen', value='0'),
        ], children=[
            rule(conditions=[cond('particles.forEach', by='0', kind='AMOD_PARTICLE_KIND_POOL_2', var='d_i')],
                 actions=[act('var.add', name='pool2_seen', value='1')],
                 name='Walk live Pool 2 particles during draw'),
        ]),

        rule(event='particles.draw', veto=True,
             conditions=[cond('var.on', name='skip_draw')],
             actions=[],
             name='Skip game particle draw when F7 toggle is on'),
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

    for item in json.load(open(os.path.join(PART_PKG, 'events.json'), encoding='utf-8')):
        if item['id'] not in events:
            missing.append('event ' + item['id'])
    for item in json.load(open(os.path.join(PART_PKG, 'conditions.json'), encoding='utf-8')):
        if item['id'] not in conditions:
            missing.append('condition ' + item['id'])
    for item in json.load(open(os.path.join(PART_PKG, 'actions.json'), encoding='utf-8')):
        if item['id'] not in actions:
            missing.append('action ' + item['id'])
    for item in json.load(open(os.path.join(PART_PKG, 'expressions.json'), encoding='utf-8')):
        if item['text'] not in text_blob:
            missing.append('expression %s (%s)' % (item['id'], item['text']))

    return missing


def main():
    sheet = build_sheet()
    missing = verify_coverage(sheet)
    if missing:
        print('MISSING IN PARTICLESTEST:')
        for m in missing:
            print('  -', m)
        return 1

    manifest = {
        'api_version': current_api_version(),
        'author': 'AFModConstructor',
        'capabilities': [],
        'description': 'Interactive showcase of all ParticlesApi events, conditions, actions and expressions',
        'entry': 'bin/particlestest.dll',
        'game_version': '1.0',
        'id': 'particlestest',
        'name': 'particlestest',
        'requires': ['input', 'particles', 'player', 'render', 'textures'],
        'version': '1.0.0',
    }
    project = {
        '@project': {
            '@build': '0',
            '@last.file': '',
            '@last.proj': '',
            '@name': 'particlestest',
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

    ev_n = len(json.load(open(os.path.join(PART_PKG, 'events.json'), encoding='utf-8')))
    cd_n = len(json.load(open(os.path.join(PART_PKG, 'conditions.json'), encoding='utf-8')))
    ac_n = len(json.load(open(os.path.join(PART_PKG, 'actions.json'), encoding='utf-8')))
    ex_n = len(json.load(open(os.path.join(PART_PKG, 'expressions.json'), encoding='utf-8')))
    print('particlestest ready: events %d/%d, conditions %d/%d, actions %d/%d, expressions %d/%d'
          % (ev_n, ev_n, cd_n, cd_n, ac_n, ac_n, ex_n, ex_n))
    return 0


if __name__ == '__main__':
    sys.exit(main())
