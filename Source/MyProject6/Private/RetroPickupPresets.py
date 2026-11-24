"""Starting palettes for the weapon pickup Blueprints. All values stay editable per actor."""
import unreal as u


def appearance(reward, icon=None):
    shape, motif, weapon = u.PickupShape, u.PickupMotif, u.PickupWeapon
    # Shape, enamel, metal trim, porcelain inlay, bezel width and marquee lights.
    palettes = {
        weapon.RAILGUN: (shape.FLYING_SAUCER, (.032,.21,.55), (.55,.76,.86), (.65,.92,1.), 1.1, 12),
        weapon.DEFAULT_WEAPON: (shape.JUKEBOX, (.025,.34,.19), (.83,.49,.15), (1.,.86,.56), 1., 7),
        weapon.BFG: (shape.FLYING_SAUCER, (.13,.40,.15), (.58,.66,.32), (.92,.99,.64), 1.12, 10),
        weapon.BOMB: (shape.DAISY_CHARM, (.69,.116,.047), (.84,.41,.13), (1.,.73,.46), 1., 6),
        weapon.SLEEP_SIGNAL: (shape.JUKEBOX, (.36,.17,.48), (.79,.53,.49), (1.,.76,.78), .90, 9),
        weapon.ELECTRIC_TETHER: (shape.WINGED_TOKEN, (.039,.23,.35), (.58,.78,.80), (.70,.99,.91), 1.02, 5),
        weapon.PURPLE_RAILGUN: (shape.WINGED_TOKEN, (.49,.029,.056), (.82,.47,.18), (1.,.82,.52), 1.13, 7),
    }
    form, enamel, trim, inlay, bezel, bulbs = palettes[reward]
    values = dict(shape=form, body_color=u.LinearColor(*enamel,1), trim_color=u.LinearColor(*trim,1),
                  inlay_color=u.LinearColor(*inlay,1), bezel_width=bezel, bulb_count=bulbs)
    if reward == weapon.RAILGUN and icon == motif.CROSS:
        # The hand-painted yellow pickup with the red cross has its own enamel.
        del values['body_color']
        values.update(shape=shape.DAISY_CHARM, trim_color=u.LinearColor(.78,.44,.11,1),
                      inlay_color=u.LinearColor(1.,.90,.55,1), petal_count=8, bulb_count=6)
    elif reward == weapon.RAILGUN and icon in (None, motif.STAR, motif.ROCKET, motif.WINGED_BOLT):
        values['icon'] = motif.WINGED_BOLT
    elif reward == weapon.DEFAULT_WEAPON and icon in (None, motif.ROCKET, motif.RINGED_PLANET):
        values['icon'] = motif.RINGED_PLANET
    elif reward == weapon.BOMB:
        values['petal_count'] = 6
    return values
