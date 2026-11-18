# NorseCoSave

Per-save data for the Norse overhaul (NORSE: Oath of Blood, UE4SS v4.0.0). A mod's own per-playthrough values -
attribute points, follower state, settings chosen for this save - go in a side file beside the game's save:
`%LOCALAPPDATA%\Norse\Saved\SaveGames\<slot>.norse` next to `<slot>.sav`. The game's own save stays vanilla, so a save
still loads with the mod removed. Survey: `RESEARCH-more-frameworks.md` (#2); modelled on SKSE's co-save, JContainers /
PapyrusUtil storage and Satisfactory Mod Loader's per-save session settings.

## How

Every save and load in Norse ends in the SPUD plugin's `USpudSubsystem` (the game's `ULoadSaveSubsystem` sits on top of
it), so NorseCoSave hooks SPUD's `SaveGame`, `LoadGame` and `NewGame` once each (Norse Address Library IDs 7-9):

- save: publish **GameSaving** (modules store their latest values now), then write the side file - to a temporary file,
  then renamed, so a crash never leaves half a file. Nothing stored: no side file (and a stale one is removed).
- load: read the side file, then publish **GameLoaded**. A save without one is a vanilla save: no data.
- new game: clear everything, publish **NewGame**.

The side file is plain text, one `[section]` per mod (`docs/example-side-file.norse`).

## Using it from another mod

`include/NorseCoSave.h`: `ncs::Api()->Set("MyMod", "key", "value")` (null value removes), `Get(owner, key, buf, size)`
(length, or -1). Console: `ncs.set <owner> <key> <value>`, `ncs.get <owner> <key>`, `ncs.dump`.

## Status

0.1.0 tested in game (TEST-RESULT-0.1.0.txt): a side file written beside a real save through the game's own save path,
read back on load, and the stored value survived a change and a load. Not yet exercised: autosaves, NewGame, and a mod
calling the API. Build: `xmake f -p windows -a x64 -m release --toolchain=msvc -y`, then `xmake` (needs
`../NorsePatcher`, `../NorseAddressLibrary` and `../NorseEvents` beside it; MinHook in `third_party/`).
