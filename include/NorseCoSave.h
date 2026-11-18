// NorseCoSave.h - the public API of NorseCoSave (per-save data beside each game save), 2026-11-17.
//
// A mod keeps its own per-playthrough values (attribute points, follower state, settings chosen for this save) here.
// NorseCoSave writes them to a side file next to the game's save (<slot>.norse beside <slot>.sav) whenever the game
// saves, and reads them back whenever the game loads, so the game's own save stays vanilla.
//
//   #include "NorseCoSave.h"
//   if (auto* cs = ncs::Api()) cs->Set("MyMod", "StaminaBonus", "12");
//   char buf[256]; if (auto* cs = ncs::Api(); cs && cs->Get("MyMod", "StaminaBonus", buf, sizeof(buf)) >= 0) { ... }
//
// With NorseEvents installed: GameSaving (just before the side file is written - the moment to Set your latest
// values), GameLoaded (the side file has been read - read your values), NewGame (everything was cleared).
#pragma once

#include <Windows.h>

#include <filesystem>

extern "C"
{
    typedef int (*NCS_SetFn)(const char* owner, const char* key, const char* value); // value null = remove; 1 = done
    typedef int (*NCS_GetFn)(const char* owner, const char* key, char* buf, int size); // length, or -1 when not set
}

namespace ncs
{
    struct ApiTable
    {
        NCS_SetFn Set;
        NCS_GetFn Get;
    };

    // Finds the loaded NorseCoSave beside the calling mod (...\ue4ss\Mods\NorseCoSave\dlls\main.dll); null when absent.
    inline const ApiTable* Api()
    {
        static ApiTable t{};
        if (t.Set) return &t;
        HMODULE self = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&Api), &self);
        wchar_t buf[MAX_PATH * 2]{};
        GetModuleFileNameW(self, buf, static_cast<DWORD>(sizeof(buf) / sizeof(buf[0])));
        std::filesystem::path dll = std::filesystem::path(buf).parent_path().parent_path().parent_path() / L"NorseCoSave" / L"dlls" / L"main.dll";
        HMODULE m = GetModuleHandleW(dll.c_str());
        if (!m) return nullptr;
        t.Set = reinterpret_cast<NCS_SetFn>(GetProcAddress(m, "NCS_Set"));
        t.Get = reinterpret_cast<NCS_GetFn>(GetProcAddress(m, "NCS_Get"));
        if (!t.Set || !t.Get) t = {};
        return t.Set ? &t : nullptr;
    }
} // namespace ncs
