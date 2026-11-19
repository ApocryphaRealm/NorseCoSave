# Changelog

## 1.0.0 - 2026-11-18 - working

### Added
- uLogLevel in NorseCoSave.ini: 2 = info (the default), 0 = everything for a bug report.
- First release. It contains everything built and tested in 0.1.0, listed here and kept as history below:
- Side file <slot>.norse beside each save, written on every save and read on every load (SPUD SaveGame / LoadGame / NewGame hooks, NAL IDs 7-9); the game save stays vanilla.
- API NCS_Set / NCS_Get (include/NorseCoSave.h); events GameSaving, GameLoaded, NewGame through NorseEvents; console ncs.set, ncs.get, ncs.dump.
- main.dll carries a Windows version resource (file and product version 1.0.0, shown in its Properties > Details).

## 0.1.0 - 2026-11-17 (tested in game: TEST-RESULT-0.1.0.txt)
### Added
- Side file <slot>.norse beside each save, written on every save and read on every load (SPUD SaveGame / LoadGame / NewGame hooks, NAL IDs 7-9); the game save stays vanilla.
- API NCS_Set / NCS_Get (include/NorseCoSave.h); events GameSaving, GameLoaded, NewGame through NorseEvents; console ncs.set, ncs.get, ncs.dump.
