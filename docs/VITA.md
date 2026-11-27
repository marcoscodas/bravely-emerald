# PS Vita port

> This fork's builds are for the Steam Deck and Windows (see [../README.md](../README.md)). This page
> is the original project's description of the PS Vita port, kept for reference. It has no build
> instructions for the Vita.


Unofficial native PS Vita port of Pokémon Emerald, built from the
[pret/pokeemerald](https://github.com/pret/pokeemerald) decompilation. It is not an emulator:
the game's C code is compiled for the Vita's ARM CPU, and a platform layer supplies what the GBA
hardware did. On top of it, the port brings the features of
[Emerald 3DS](https://github.com/ZallaxDev/emerald-3ds) to the Vita with vitaGL:

- a 3D voxel overworld built live from the game's maps, with sun, shadows, day/night and weather;
- 3D battles on a stage taken from the map where the battle starts;
- a new touch interface over the 3D world that replaces the GBA menus and boxes, with five
  menu themes;
- HGSS follower Pokémon, an accelerated clock, time-based encounters and optional QoL extras.

The full list is under [Features](#features).

> **AI-assisted project.** The code, build scripts and documentation were written with an AI
> coding assistant (Claude Code, by Anthropic) under the supervision of HctrRB, who sets the
> direction, reviews the changes and tests every build on a real PS Vita.

> **Legal notice.** Pokémon and Pokémon Emerald are © Nintendo, Creatures Inc. and GAME FREAK inc.
> This project is not affiliated with or endorsed by them. This repository does not contain the
> game's graphics, sounds or binary data, nor any ROM: to build, you check a dump of **your own
> cartridge** and the game files are downloaded from the public decompilation
> (see [BUILD.md](BUILD.md)). Like every decompilation, the tree still holds the game's code and text
> in decompiled form. A build contains Nintendo's data: **do not distribute builds or the
> downloaded files**.

## Features

### 3D world

- Voxel overworld built live from the game's maps: sun and shadows, day/night cycle, weather,
  depth of field, water reflections, 3D buildings and trees. 30 or 60 FPS (OPTIONS > WORLD).
- 3D battles: the stage is picked from the map around the player, and the camera glides from
  the field into it.
- Wild encounters in the 3D world fade smoothly into the battle.
- HGSS follower Pokémon, an accelerated clock and time-based encounters.

### New interface (OPTIONS > MODES > NEW UI (TEST), off by default)

> **Switched off since 1.0.** `Ui_IsEnabled()` returns false and the NEW UI option is gone. The code is kept; the text below describes it.

Drawn natively at 960×544 over the 3D world. Every screen works with the buttons and with touch
(first tap selects, a second tap on the selection confirms).

- **START menu**: Pokédex (list, entry, cry, size, area), party (summary pages, switch, held
  items, field moves, move reorder), bag (pockets, sort, use / give / toss / register), PokéNav
  (map, Match Call, condition, ribbons), Fly map, trainer card, Nuzlocke tracker, options, save.
- **PC boxes**: withdraw, deposit, move POKéMON and items, release, marks, jump to a box,
  wallpapers (the game's own, Walda's included) and box names.
- **Scenes the scripts open**: Poké Mart buy / sell, naming keyboard (player, rival, POKéMON,
  boxes), Easy Chat (mail, profile, battle messages, interviews, trendy saying, survey…), berry
  choice for the Berry Blender and Berry Crush, contest appeal choice, "choose a POKéMON" for
  the move tutor, the Day Care and other scripts.
- **Battle**: HUD with health plates, party balls, commands, moves with PP and type, targets in
  double battles, YES / NO, level-up stats and the Safari Zone; bag and party in battle (multi
  battles included); the Battle Pyramid bag.
- **Field**: dialogue box, YES / NO, choice lists (PC menus, NPC questions), money box, item
  obtained, PokéNav calls and zone signs with each town's motto.
- **Menu themes** (OPTIONS > GENERAL > MENU COLOR): RED (default), EMERALD, CRYSTAL, TROPICAL
  and ROYAL. Each has its own colours, box shapes, bevels and font, and applies to every screen,
  the battle HUD and the zone signs.

### Extras

Physical / special split, infinite TMs, running indoors, IV / EV pages, EXP Share and EXP
multiplier, Nuzlocke rules, field HM shortcuts, button remapping.

## Status

> **Work in progress, in active testing.** Expect bugs, crashes and unfinished screens. If you
> find one, please [open an issue](https://github.com/marcoscodas/bravely-emerald/issues/new/choose) with the bug report template: what
> you did, what happened, and the log files from `ux0:data/POKEVITA/`. Feedback on screens
> that are missing or feel wrong is very welcome.

Performance: 60 FPS with drops to 55 at 333 / 166 MHz.

### Not yet tested on a PS Vita

These parts build and run in the design mocks, but have not been checked on the console yet:

- The menu themes on every screen, the battle HUD in each theme, and the lighter TROPICAL font.
- PC boxes: moving, withdrawing, depositing, releasing, marks, wallpapers and box names.
- Naming keyboard: new game, box names, nicknames, a POKéMON caught with a full party.
- Easy Chat in each of its uses, the berry choice and the contest appeal choice.
- Day Care and move tutor choice (the right POKéMON is taken, the screen fades back in).
- The dialogue box after PokéNav calls and YES / NO answers (it used to flicker).
- The words of the PC choice menus, larger POKéMON names in battle.
- The fade into wild battles in the 3D world.
- Evolutions and some other scenes.

### Known issues and gaps

- Still on the GBA interface: the summary of a boxed POKéMON in the PC, the Battle Pyramid bag
  inside battles and its "bag full: choose what to drop" step, and Easy Chat's Bard song and
  quiz screens.
- Some UI screens are still missing: give feedback on the ones you find.
- The menu can freeze sometimes.
- Terrain generation bugs in some maps (they come from Emerald 3DS).
- The shop buy / sell flow over the 3D world with the new UI off still has visual glitches.
- Walda's FRIENDS wallpaper may draw without its frame.
- Trainer and legendary battle transitions are the GBA ones and only partly show over the 3D
  world.
- Link and recorded battles, Wally's catching tutorial and the Battle Arena judgment keep the
  GBA battle HUD.
- Intro and title screen margins at 16:9 are broken (sprites cut at the GBA edge, streaks
  beside Rayquaza); a replacement is designed in [SCENES_16_9.md](SCENES_16_9.md).
- Occasional frame drops in dense areas of the 3D overworld.
- Vita3K shows a black screen; test on hardware.

## Repository layout

```
CMakeLists.txt                       CMake build (Deck and Windows; the Vita build scripts were removed)
make_tools.mk, *_rules.mk, vita/     host tools, asset rules, Vita-lineage platform files
src/, include/, asm/, data/,         the pokeemerald decompilation (game code and text data;
constants/                           graphics/, sound/ and data binaries come from fetch_assets)
tools/fetch_assets.sh/.ps1, tools/assets/  game files download (checks your ROM, then the manifest)
src/platform/                        GBA hardware emulation and main loop
src/platform/voxel/                  3D voxel world
src/platform/vita/                   native UI and Vita-specific features
assets/                              port data (voxel, UI art, followers)
tools/                               pret host tools and the UI art generators (tools/ui_gen)
docs/                                technical documentation
```

## Controls

| Button | Action |
|---|---|
| Cross / Circle | A / B |
| D-pad or left stick | Move |
| L / R | L / R |
| START | Open or close the touch menu (field) |
| SELECT | Registered item |
| Triangle | Fast forward |
| Square | Options |
| Touch | Menu, battle action deck |

Buttons can be remapped in Options. Settings and saves live in `ux0:data/POKEVITA/`
(see [ARCHITECTURE.md](ARCHITECTURE.md#runtime-files-ux0datapokevita)).
