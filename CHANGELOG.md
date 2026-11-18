# Changelog

## 0.1.0 - 2026-11-17 (tested in game: TEST-RESULT-0.1.0.txt)
### Added
- Side file <slot>.norse beside each save, written on every save and read on every load (SPUD SaveGame / LoadGame / NewGame hooks, NAL IDs 7-9); the game save stays vanilla.
- API NCS_Set / NCS_Get (include/NorseCoSave.h); events GameSaving, GameLoaded, NewGame through NorseEvents; console ncs.set, ncs.get, ncs.dump.
