# Bravely Emerald

Pokémon Emerald, reimagined as a Square Enix HD-2D game.

<p align="center"><a href="https://recompstore.com/dev/marcoscodas"><picture><source media="(prefers-color-scheme: dark)" srcset="https://recompstore.com/assets/press/verified-light.svg"><img alt="Verified developer on RecompStore" src="https://recompstore.com/assets/press/verified-dark.svg" height="48"></picture></a></p>

This is Pokémon Emerald with a lit, depth-blurred 3D diorama look, running natively on the Steam
Deck and on Windows. It is the same game and the same saves. The decompiled game is compiled to a
32-bit program, and a small layer on SDL2 and OpenGL stands in for the GBA hardware, so it is not
an emulator.

The repository holds no game data. You build the game yourself from a dump of your own cartridge,
and the result is for your own use. Please do not share builds, since they contain Nintendo's data.

<table>
<tr>
<td><img src="docs/screens/title_hd2d.jpg" alt="Title screen"><br>The new title screen.</td>
<td><img src="docs/screens/town_littleroot.jpg" alt="Littleroot Town"><br>Littleroot Town.</td>
</tr>
<tr>
<td><img src="docs/screens/battle_hd.jpg" alt="A wild battle"><br>A battle, with the backdrop blurred.</td>
<td><img src="docs/screens/before_after.jpg" alt="Before and after"><br>Before (left) and after (right).</td>
</tr>
</table>

## Get it

The easiest way is the builder, a small program in the look of Windows 95. It carries this
repository inside it, so you only need your ROM and an internet connection. It downloads the build
tools and the game files, builds the game, and puts it in the folder you chose. Nothing is uploaded.

<table>
<tr>
<td><img src="docs/screens/builder-text-125.png" alt="The builder window" width="400"></td>
<td><img src="docs/screens/builder-text-200.png" alt="The builder window, enlarged" width="400"></td>
</tr>
</table>

Download it from the [latest release](https://github.com/marcoscodas/bravely-emerald/releases/latest):
`BravelyEmeraldBuilder-win.zip` for Windows, or the `.tar.gz` for Linux and SteamOS (then run
`sh install.sh`).

1. Pick your ROM.
2. Pick a folder to build in.
3. Tick the box that allows the downloads and press Build.

The first build downloads the compiler tools and takes 30 to 60 minutes, and it needs about 15 GB
of disk space. More in [docs/BUILDER.md](docs/BUILDER.md). To build by hand, see
[docs/BUILD_DECK.md](docs/BUILD_DECK.md) for the Steam Deck and
[docs/WINDOWS_BUILD.md](docs/WINDOWS_BUILD.md) for Windows.

## What you get

- Lighting: a real sun shadow map, soft shadows under characters, and sun and moon rays that follow
  the time of day.
- Depth blur that follows the scene, plus bloom, colour grading and a vignette.
- Relief on the ground, walls and sprites, so flat pixel art picks up a sense of depth.
- Wind in the trees and grass, tall grass that bends as you walk, flowers, and water glints.
- Weather and particles: rain with streaks, mist, splashes and wet ground, pollen, fireflies, leaves.
- Ambient sound that fits the place: wind, rain, waves, crickets, birds, cave drips, thunder.
- Battles on a blurred 3D backdrop, with a coloured flash and glow when a move hits.
- Glass panels, smoother menus and bars, and button icons that match the Deck.
- A new title screen and an intro that reveals the HD-2D picture from the flat GBA one.
- Every effect has its own setting (off, low, medium, high) in the in-game options menu. With all of
  them off you get the old picture back.

Full feature and settings list: [docs/FEATURES.md](docs/FEATURES.md)

## Status

It works on the Steam Deck and on Windows. The Linux builder has not been tried on real hardware
yet, and some things have not been checked on every machine. Reports and issues are welcome. The
list of known problems is in [docs/KNOWN_ISSUES.md](docs/KNOWN_ISSUES.md).

## Credits and licence

Built on [pret/pokeemerald](https://github.com/pret/pokeemerald),
[pokeemerald-multiplatform](https://github.com/gradenGnostic/pokeemerald-multiplatform) by
gradenGnostic, and HctrRB's [PS Vita port](https://github.com/HctrRB/pokeemerald-vita-3ds), which
brings in the features of [Emerald 3DS](https://github.com/ZallaxDev/emerald-3ds) by ZallaxDev.
Fonts: Cinzel and W95FA, both under the SIL Open Font License.
The Deck and Windows work was written with an AI coding assistant (Claude Code) under marcoscodas's
direction.
Full list: [docs/CREDITS.md](docs/CREDITS.md) and [NOTICE.md](NOTICE.md). The code is MIT
([LICENSE](LICENSE)) except the parts derived from Emerald 3DS, which are CC BY-NC-SA 4.0. Pokémon
and its game content belong to Nintendo, Creatures Inc. and GAME FREAK inc., and this is a
non-commercial fan project.
