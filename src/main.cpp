// NorseCoSave - per-save data beside each game save (UE4SS v4.0.0 C++ mod for NORSE: Oath of Blood), 2026-11-17.
//
// Every save and load in Norse ends in the SPUD plugin's USpudSubsystem (the game's LoadSaveSubsystem sits on top), so
// NorseCoSave hooks SPUD's SaveGame / LoadGame / NewGame once (Norse Address Library IDs 7-9) and keeps a side file
// <slot>.norse beside <slot>.sav in %LOCALAPPDATA%\Norse\Saved\SaveGames: plain text, one [section] per mod. The game's
// own save stays vanilla - remove the mod and every save still loads. Modelled on SKSE's co-save, JContainers /
// PapyrusUtil storage and Satisfactory Mod Loader's per-save session settings.
// API: include/NorseCoSave.h (NCS_Set, NCS_Get). Events through NorseEvents: GameSaving, GameLoaded, NewGame.
// Console: ncs.set <owner> <key> <value>, ncs.get <owner> <key>, ncs.dump.
#include <Windows.h>
#include <ShlObj.h>

#include "NorseCoSave.h"
#include "NorseEvents.h"
#include "NorseAddressLibrary.hpp"
#include "Log.hpp"
#include "Ue.hpp"

#include <MinHook.h>

#include <chrono>
#include <ctime>
#include <map>
#include <mutex>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
using namespace nrn;
using namespace nrn::ue;

namespace
{
    fs::path g_mod, g_saveDir;
    std::recursive_mutex g_mx;
    std::map<std::string, std::map<std::string, std::string>> g_data; // owner -> key -> value
    std::string g_slot;                                               // the slot last saved or loaded

    fs::path ModDir()
    {
        HMODULE self = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&ModDir), &self);
        wchar_t buf[MAX_PATH * 2]{};
        GetModuleFileNameW(self, buf, static_cast<DWORD>(std::size(buf)));
        return fs::path(buf).parent_path().parent_path();
    }

    fs::path SaveDir()
    {
        PWSTR p = nullptr;
        fs::path out;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p))) out = fs::path(p) / L"Norse" / L"Saved" / L"SaveGames";
        CoTaskMemFree(p);
        return out;
    }

    // a slot name becomes a file name: keep it, but never let it leave the save folder
    fs::path SideFile(const std::string& slot)
    {
        std::wstring w = fs::path(slot).filename().wstring();
        return g_saveDir / (w + L".norse");
    }

    std::string Escape(const std::string& s)
    {
        std::string o;
        for (char c : s)
        {
            if (c == '\\') o += "\\\\";
            else if (c == '\n') o += "\\n";
            else if (c == '\r') o += "\\r";
            else o += c;
        }
        return o;
    }

    std::string Unescape(const std::string& s)
    {
        std::string o;
        for (size_t i = 0; i < s.size(); ++i)
        {
            if (s[i] == '\\' && i + 1 < s.size())
            {
                char n = s[++i];
                o += n == 'n' ? '\n' : n == 'r' ? '\r' : n;
            }
            else o += s[i];
        }
        return o;
    }

    void Publish(const char* name)
    {
        if (const ne::ApiTable* api = ne::Api())
        {
            NE_Event e{static_cast<uint32_t>(sizeof(NE_Event)), name, nullptr, nullptr, nullptr, 0, 0, 0};
            api->Publish(&e);
        }
    }

    size_t Count()
    {
        size_t n = 0;
        for (auto& [o, kv] : g_data) n += kv.size();
        return n;
    }

    void WriteSide(const std::string& slot)
    {
        std::lock_guard lk(g_mx);
        fs::path f = SideFile(slot);
        std::error_code ec;
        if (g_data.empty())
        {
            if (fs::exists(f, ec)) fs::remove(f, ec); // nothing stored: no side file, and no stale one either
            NRN_INFO("save '%s': nothing stored - no side file", slot.c_str());
            return;
        }
        fs::path tmp = f;
        tmp += L".tmp";
        {
            std::ofstream o(tmp, std::ios::binary);
            std::time_t now = std::time(nullptr);
            char when[32];
            std::tm tmv{};
            localtime_s(&tmv, &now);
            std::strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", &tmv);
            o << "# NorseCoSave side file for " << slot << ".sav - written " << when << ". Per-mod data; the game never reads it.\n";
            o << "[NorseCoSave]\nFormat=1\n";
            for (auto& [owner, kv] : g_data)
            {
                if (owner == "NorseCoSave") continue;
                o << "\n[" << owner << "]\n";
                for (auto& [k, v] : kv) o << k << "=" << Escape(v) << "\n";
            }
        }
        fs::rename(tmp, f, ec); // whole file or nothing: a crash mid-write never leaves half a side file
        if (ec) NRN_ERROR("save '%s': could not write %s (%s)", slot.c_str(), Narrow(f.wstring()).c_str(), ec.message().c_str());
        else NRN_INFO("save '%s': %zu value(s) from %zu mod(s) -> %s", slot.c_str(), Count(), g_data.size(), Narrow(f.filename().wstring()).c_str());
    }

    void ReadSide(const std::string& slot)
    {
        std::lock_guard lk(g_mx);
        g_data.clear();
        fs::path f = SideFile(slot);
        std::ifstream in(f, std::ios::binary);
        if (!in)
        {
            NRN_INFO("load '%s': no side file - this save has no NorseCoSave data (a vanilla save, or nothing was stored)", slot.c_str());
            return;
        }
        std::string line, owner;
        while (std::getline(in, line))
        {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            if (line.front() == '[' && line.back() == ']')
            {
                owner = line.substr(1, line.size() - 2);
                continue;
            }
            size_t eq = line.find('=');
            if (eq == std::string::npos || owner.empty() || owner == "NorseCoSave") continue;
            g_data[owner][line.substr(0, eq)] = Unescape(line.substr(eq + 1));
        }
        NRN_INFO("load '%s': %zu value(s) from %zu mod(s) <- %s", slot.c_str(), Count(), g_data.size(), Narrow(f.filename().wstring()).c_str());
    }

    std::string SlotOf(const void* fstring)
    {
        // FString (16 bytes): TCHAR* data, int32 num (with the terminator), int32 max
        const wchar_t* data = nullptr;
        std::memcpy(&data, fstring, 8);
        return data ? Narrow(data) : std::string();
    }

    // ------------------------------------------------------------------ hooks (SPUD, NAL 7-9)
    using SaveFn = void(__fastcall*)(UObject* spud, const void* slot, const void* title, bool shot, UObject* extra);
    using LoadFn = void(__fastcall*)(UObject* spud, const void* slot, const void* travel);
    using NewFn = void(__fastcall*)(UObject* spud, bool serverOnly, bool afterLevelLoad);
    SaveFn o_Save = nullptr;
    LoadFn o_Load = nullptr;
    NewFn o_New = nullptr;

    void __fastcall SaveHook(UObject* spud, const void* slot, const void* title, bool shot, UObject* extra)
    {
        try
        {
            std::string s = SlotOf(slot);
            Publish("GameSaving"); // modules Set their latest values now
            g_slot = s;
            WriteSide(s);
        }
        catch (...)
        {
            NRN_ERROR("save hook threw - the game's save goes on");
        }
        o_Save(spud, slot, title, shot, extra);
    }

    void __fastcall LoadHook(UObject* spud, const void* slot, const void* travel)
    {
        try
        {
            g_slot = SlotOf(slot);
            ReadSide(g_slot);
            Publish("GameLoaded");
        }
        catch (...)
        {
            NRN_ERROR("load hook threw - the game's load goes on");
        }
        o_Load(spud, slot, travel);
    }

    void __fastcall NewHook(UObject* spud, bool serverOnly, bool afterLevelLoad)
    {
        {
            std::lock_guard lk(g_mx);
            g_data.clear();
            g_slot.clear();
        }
        NRN_INFO("new game: per-save data cleared");
        Publish("NewGame");
        o_New(spud, serverOnly, afterLevelLoad);
    }

    template <typename F>
    bool HookId(uint32_t id, const char* what, F detour, F& original)
    {
        auto target = reinterpret_cast<void*>(nal::Library::Get().Address(id));
        if (!target || MH_CreateHook(target, reinterpret_cast<void*>(detour), reinterpret_cast<void**>(&original)) != MH_OK ||
            MH_EnableHook(target) != MH_OK)
        {
            NRN_ERROR("%s: not hooked (NAL ID %u) - per-save data is not %s this session", what, id, what);
            original = nullptr;
            return false;
        }
        NRN_INFO("%s: hooked (NAL ID %u)", what, id);
        return true;
    }

    void InstallHooks()
    {
        fs::path nalDir = g_mod.parent_path().parent_path() / "NorseAddressLibrary";
        if (!nal::Library::Get().Load(nalDir, [](const std::string& m) { NRN_INFO("%s", m.c_str()); }))
        {
            NRN_ERROR("Norse Address Library not found at %s - per-save data is off", Narrow(nalDir.wstring()).c_str());
            return;
        }
        MH_STATUS s = MH_Initialize();
        if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) return;
        HookId(nal::ID::USpudSubsystem::SaveGame, "saved", &SaveHook, o_Save);
        HookId(nal::ID::USpudSubsystem::LoadGame, "loaded", &LoadHook, o_Load);
        HookId(nal::ID::USpudSubsystem::NewGame, "cleared on a new game", &NewHook, o_New);
    }

    bool RunCommand(const std::wstring& c)
    {
        std::istringstream in(Narrow(c));
        std::string cmd, owner, key, value;
        in >> cmd >> owner >> key;
        std::getline(in, value);
        if (!value.empty() && value[0] == ' ') value.erase(0, 1);
        NRN_INFO("command: %s", Narrow(c).c_str());
        std::lock_guard lk(g_mx);
        if (cmd == "ncs.set" && !owner.empty() && !key.empty())
        {
            g_data[owner][key] = value;
            NRN_INFO("ncs.set %s.%s = %s", owner.c_str(), key.c_str(), value.c_str());
        }
        else if (cmd == "ncs.get" && !owner.empty() && !key.empty())
        {
            auto it = g_data.find(owner);
            bool has = it != g_data.end() && it->second.count(key);
            NRN_INFO("ncs.get %s.%s = %s", owner.c_str(), key.c_str(), has ? it->second[key].c_str() : "(not set)");
        }
        else if (cmd == "ncs.dump")
        {
            NRN_INFO("ncs.dump: slot '%s', %zu value(s)", g_slot.c_str(), Count());
            for (auto& [o, kv] : g_data)
                for (auto& [k, v] : kv) NRN_INFO("  %s.%s = %s", o.c_str(), k.c_str(), v.c_str());
        }
        else NRN_WARN("unknown command - ncs.set <owner> <key> <value> | ncs.get <owner> <key> | ncs.dump");
        return true;
    }
} // namespace

extern "C" __declspec(dllexport) int NCS_Set(const char* owner, const char* key, const char* value)
{
    if (!owner || !key || !*owner || !*key || std::strchr(owner, ']') || std::strchr(key, '=')) return 0;
    std::lock_guard lk(g_mx);
    if (value) g_data[owner][key] = value;
    else if (auto it = g_data.find(owner); it != g_data.end())
    {
        it->second.erase(key);
        if (it->second.empty()) g_data.erase(it);
    }
    return 1;
}

extern "C" __declspec(dllexport) int NCS_Get(const char* owner, const char* key, char* buf, int size)
{
    if (!owner || !key) return -1;
    std::lock_guard lk(g_mx);
    auto it = g_data.find(owner);
    if (it == g_data.end()) return -1;
    auto kv = it->second.find(key);
    if (kv == it->second.end()) return -1;
    if (buf && size > 0) strncpy_s(buf, static_cast<size_t>(size), kv->second.c_str(), _TRUNCATE);
    return static_cast<int>(kv->second.size());
}

class NorseCoSave : public RC::CppUserModBase
{
  public:
    NorseCoSave()
    {
        ModName = L"NorseCoSave";
        ModVersion = L"" NCS_VERSION;
        ModDescription = L"Per-save data for Norse mods in a side file beside each save; the game's own save stays vanilla.";
        ModAuthors = L"ApocryphaRealm";
        g_mod = ModDir();
        g_saveDir = SaveDir();
        std::wstring dir;
        {
            std::ifstream in(g_mod / "NorseCoSave.ini");
            std::string line;
            while (std::getline(in, line))
            {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.rfind("sLogDir=", 0) == 0)
                {
                    int n = MultiByteToWideChar(CP_UTF8, 0, line.c_str() + 8, -1, nullptr, 0);
                    dir.resize(static_cast<size_t>(n > 0 ? n - 1 : 0));
                    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, line.c_str() + 8, -1, dir.data(), n);
                }
            }
        }
        Log::Get().Open((dir.empty() ? g_mod : fs::path(dir)) / "NorseCoSave.log", 2);
        Log::Get().Write(Level::Error, "NorseCoSave v%s - side files in %s", NCS_VERSION, Narrow(g_saveDir.wstring()).c_str());
    }

    auto on_unreal_init() -> void override
    {
        InstallHooks();
        try
        {
            RC::Unreal::Hook::RegisterProcessConsoleExecCallback(
                    [](UObject*, const wchar_t* cmd, RC::Unreal::FOutputDevice&, UObject*) -> bool {
                        if (!cmd || _wcsnicmp(cmd, L"ncs.", 4) != 0) return false;
                        static ULONGLONG last = 0;
                        static std::wstring lastCmd;
                        ULONGLONG now = GetTickCount64();
                        if (lastCmd == cmd && now - last < 250) return true;
                        last = now;
                        lastCmd = cmd;
                        return RunCommand(cmd);
                    });
        }
        catch (...)
        {
            NRN_ERROR("console callback registration failed");
        }
    }
};

extern "C" __declspec(dllexport) RC::CppUserModBase* start_mod()
{
    return new NorseCoSave();
}

extern "C" __declspec(dllexport) void uninstall_mod(RC::CppUserModBase* mod)
{
    delete mod;
}
