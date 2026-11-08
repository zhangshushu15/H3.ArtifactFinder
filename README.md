# H3.ArtifactFinder

An HD Mod plugin for Heroes 3: Shadow of Death (single player). It runs alongside SoD_SP.

On the adventure map, press **Shift+F** (Ctrl+F is SoD_SP's FindMe) to open the search.

- The list shows every artifact lying on the map, with its coordinates. As you type, it narrows to the artifacts whose name contains the text (case doesn't matter).
- **Choose an entry** by clicking it, or with Up/Down (Page Up/Down, mouse wheel) and Enter or OK. The view centers on the artifact, and a panel at the bottom of the screen shows its picture and coordinates.
- After you close that panel, the search reopens with your text and selection kept, so you can check the next candidate. Esc or Cancel closes it.
- The fog of war is lifted around the artifact only while the panel is open. It's restored afterwards, and the view returns to where you were looking.
- If nothing matches, the list says "No artifact matching … was found on the map".
- Spell scrolls are listed with their spell, e.g. "Spell Scroll (Town Portal)", so you can search by spell name too.
- The hotkey does nothing in multiplayer games.

**Limitation:** only artifacts lying on the ground are found. Artifacts held by heroes, offered as Seer Hut or event rewards, inside Pandora's Boxes or creature banks, or sold in Black Markets are not searched.

## Install

You need Heroes 3 Shadow of Death or Complete Edition (GOG, Ubisoft or CD) with [HD Mod](https://sites.google.com/site/heroes3hd/) installed. SoD_SP is optional.

1. Download `H3.ArtifactFinder.dll` from the [latest release](https://github.com/zhangshushu15/H3.ArtifactFinder/releases/latest).
2. In your game folder, create the folder `_HD3_Data\Packs\ArtifactFinder\` and put the DLL in it.
   For example: `C:\GOG Games\HoMM 3 Complete\_HD3_Data\Packs\ArtifactFinder\H3.ArtifactFinder.dll`.
   (If you use SoD_SP, its pack sits next to it, in `_HD3_Data\Packs\SoD_SP.<version>\`.)
3. Start `HD_Launcher.exe`, enable the **ArtifactFinder** pack, and launch the game from the launcher.
4. In a single-player game, press **Shift+F** on the adventure map.

To update, close the game and replace the DLL. To uninstall, disable the pack in the launcher or delete the folder.

## Build from source

1. Clone [H3API](https://github.com/RoseKavalier/H3API) next to this folder, so `..\H3API\single_header\H3API.hpp` exists.
2. Open `ArtifactFinder.sln` in Visual Studio (Desktop development with C++ workload). If it offers to retarget the toolset, accept.
3. Build **Release | x86**. The output is `bin\Release\H3.ArtifactFinder.dll`; install it as described above.

## Settings

The constants at the top of `src/ArtifactFinder.cpp` control the hotkey, the reveal radius, the maximum text length and the number of visible rows.

## Support

ArtifactFinder is free. If you find it useful, you can buy me a coffee at [ko-fi.com/pingpongplayer](https://ko-fi.com/pingpongplayer).

## Credits

- [H3API](https://github.com/RoseKavalier/H3API) by RoseKavalier (MIT): Heroes 3 structures and functions.
- [HD Mod](https://sites.google.com/site/heroes3hd/) by baratorch: plugin loader and the patcher_x86 hooking library.
- Hook point and plugin structure based on the ShiftToggle example in [H3Plugins](https://github.com/RoseKavalier/H3Plugins).

## License

MIT, see [LICENSE](LICENSE). Heroes of Might and Magic is a trademark of Ubisoft; this project is not affiliated with or endorsed by Ubisoft.
