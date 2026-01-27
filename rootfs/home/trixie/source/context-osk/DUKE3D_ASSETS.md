# Duke Nukem 3D Theme Assets

## Layout Overview

```
┌─────────────────────────────────────────────────────────────┐
│ Row 1 (60px high): Game Controls & Items                   │
│ [Save] [Load] [Map] | [HoloDuke] [Jetpack] [NV] [Med] [Ste]│
├─────────────────────────────────────────────────────────────┤
│ Row 2 (70px high): Weapons 1-5                             │
│ [Kick] [Pistol] [Shotgun] [Chaingun] [RPG]                │
├─────────────────────────────────────────────────────────────┤
│ Row 3 (70px high): Weapons 6-0                             │
│ [PipeBomb] [Shrinker] [Devastator] [Tripbomb] [Freezer]   │
└─────────────────────────────────────────────────────────────┘

Total dimensions: 548x214 pixels
Background color: #1a0d0d (dark red, Duke's style)
```

## Required Image Assets

All images should be PNG format with transparency support.

### Game Controls (64x60 pixels)
- `./DUKE3D/save.png` - Save game icon (F2)
- `./DUKE3D/load.png` - Load game icon (F3)
- `./DUKE3D/map.png` - 2D map icon (TAB)

### Items (64x60 pixels)
- `./DUKE3D/holoduke.png` - HoloDuke decoy device (H)
- `./DUKE3D/jetpack.png` - Jetpack (J)
- `./DUKE3D/nightvision.png` - Night vision goggles (N)
- `./DUKE3D/medkit.png` - Medical kit (M)
- `./DUKE3D/steroids.png` - Steroids (R)

### Weapons (70x70 pixels)
- `./DUKE3D/weapon_kick.png` - Mighty Foot/Kick (1)
- `./DUKE3D/weapon_pistol.png` - Pistol (2)
- `./DUKE3D/weapon_shotgun.png` - Shotgun (3)
- `./DUKE3D/weapon_chaingun.png` - Chaingun Cannon (4)
- `./DUKE3D/weapon_rpg.png` - RPG/Rocket Launcher (5)
- `./DUKE3D/weapon_pipebomb.png` - Pipe Bomb (6)
- `./DUKE3D/weapon_shrinker.png` - Shrinker Ray (7)
- `./DUKE3D/weapon_devastator.png` - Devastator (8)
- `./DUKE3D/weapon_tripbomb.png` - Laser Tripbomb (9)
- `./DUKE3D/weapon_freezer.png` - Freezethrower (0)

## Key Mappings

### Game Controls
| Button | Key | Keycode | Function |
|--------|-----|---------|----------|
| Save | F2 | 68 | Save game |
| Load | F3 | 69 | Load game |
| Map | TAB | 23 | Toggle 2D map |

### Items
| Button | Key | Keycode | Function |
|--------|-----|---------|----------|
| HoloDuke | H | 43 | Use HoloDuke decoy |
| Jetpack | J | 44 | Use Jetpack |
| NightVis | N | 57 | Use Night Vision |
| Medkit | M | 58 | Use Medkit |
| Steroids | R | 27 | Use Steroids |

### Weapons
| Button | Key | Keycode | Weapon |
|--------|-----|---------|--------|
| Kick | 1 | 10 | Mighty Foot |
| Pistol | 2 | 11 | Pistol |
| Shotgun | 3 | 12 | Shotgun |
| Chaingun | 4 | 13 | Chaingun Cannon |
| RPG | 5 | 14 | RPG |
| PipeBomb | 6 | 15 | Pipe Bomb |
| Shrinker | 7 | 16 | Shrinker |
| Devastor | 8 | 17 | Devastator |
| Tripbomb | 9 | 18 | Laser Tripbomb |
| Freezer | 0 | 19 | Freezethrower |

## Design Notes

- **Color Scheme**: Dark red background (#1a0d0d) matches Duke's aesthetic
- **Window Match**: Theme activates when window title contains "DUKE3D.EXE"
- **Layout**: Logical grouping - controls/items at top, weapons in two rows below
- **Button Sizes**:
  - Game controls & items: 64x60 (compact for utilities)
  - Weapons: 70x70 (larger for quick weapon switching in combat)
- **Spacing**: 2px gaps between buttons for clear visual separation

## Asset Creation Tips

1. Use iconic game sprites or rendered icons
2. Ensure good contrast against the dark red background
3. Consider adding subtle glows or borders for visibility
4. Weapon icons should be instantly recognizable
5. Item icons should match in-game inventory appearance
6. Save/Load/Map can use generic game UI symbols
7. **Images are centered with padding** - aspect ratio is preserved, not stretched
8. You can create square images or any aspect ratio - they'll be scaled to fit and centered
9. Use transparent backgrounds (PNG alpha) for best results

## Testing

1. Place all PNG files in `./DUKE3D/` directory relative to the theme file
2. Launch DOSBox or Duke3D executable
3. Context-OSK should automatically load this theme when Duke3D is active
4. If images don't load, labels will display as fallback
