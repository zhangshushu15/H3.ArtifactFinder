# H3.ArtifactFinder

An HD Mod plugin for Heroes 3: Shadow of Death (single player). It runs alongside SoD_SP.

On the adventure map, press **Shift+F** (Ctrl+F is SoD_SP's FindMe) to open the search.

- The list shows every artifact lying on the map, with its coordinates. As you type, it narrows to the artifacts whose name contains the text (case doesn't matter).
- **Choose an entry** by clicking it, or with Up/Down (Page Up/Down, mouse wheel) and Enter or OK. The view centers on the artifact and a message shows its picture and coordinates.
- After you close that message, the search reopens with your text and selection kept, so you can check the next candidate. Esc or Cancel closes it.
- The fog of war is lifted around the artifact only while its message is open. It's restored afterwards, and the view returns to where you were looking.
- If nothing matches, the list says "No artifact matching … was found on the map".
- Spell scrolls are listed with their spell, e.g. "Spell Scroll (Town Portal)", so you can search by spell name too.
- The hotkey does nothing in multiplayer games.

**Limitation:** only artifacts lying on the ground are found. Artifacts held by heroes, offered as Seer Hut or event rewards, inside Pandora's Boxes or creature banks, or sold in Black Markets are not searched.

## Build

1. Clone [H3API](https://github.com/RoseKavalier/H3API) next to this folder, so `..\H3API\single_header\H3API.hpp` exists.
2. Open `ArtifactFinder.sln` in Visual Studio (Desktop development with C++ workload). If it offers to retarget the toolset, accept.
3. Build **Release | x86**. The output is `bin\Release\H3.ArtifactFinder.dll`.

## Install

HD Mod loads every DLL inside an enabled "pack" folder (SoD_SP is installed as `_HD3_Data\Packs\SoD_SP.<version>\`).

1. Create `<game>\_HD3_Data\Packs\ArtifactFinder\` and copy `H3.ArtifactFinder.dll` into it.
2. In `HD_Launcher.exe`, enable the `ArtifactFinder` pack next to SoD_SP, then start the game.

## Settings

The constants at the top of `src/ArtifactFinder.cpp` control the hotkey, the reveal radius, the maximum text length and the number of visible rows.

## Credits

- [H3API](https://github.com/RoseKavalier/H3API) by RoseKavalier (MIT): Heroes 3 structures and functions.
- [HD Mod](https://sites.google.com/site/heroes3hd/) by baratorch: plugin loader and the patcher_x86 hooking library.
- Hook point and plugin structure based on the ShiftToggle example in [H3Plugins](https://github.com/RoseKavalier/H3Plugins).

## License

MIT, see [LICENSE](LICENSE). Heroes of Might and Magic is a trademark of Ubisoft; this project is not affiliated with or endorsed by Ubisoft.
