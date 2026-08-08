#include "Menu.h"

#include "Config.h"

#include <atomic>
#include <filesystem>
#include <SKSEMenuFramework.h>

namespace VariousDialogueTags::Menu
{
    namespace
    {
        void __stdcall RenderSettings()
        {
            auto& config = Config::GetSingleton();
            bool enabled = config.Enabled();

            if (ImGuiMCP::Checkbox("Enable Various Dialogue Tags", &enabled)) {
                if (!config.SetEnabledFromMenu(enabled)) {
                    SKSE::log::error("Failed to persist one or more menu setting files");
                }
            }

            bool globalPluginNameFallback = config.GlobalPluginNameFallback();
            if (ImGuiMCP::Checkbox(
                    "Tag unconfigured mods with plugin names", &globalPluginNameFallback)) {
                if (!config.SetGlobalPluginNameFallbackFromMenu(globalPluginNameFallback)) {
                    SKSE::log::error("Failed to persist one or more menu setting files");
                }
            }
        }
    }

    void Register()
    {
        static bool registered = false;
        if (registered) {
            return;
        }

        if (!SKSEMenuFramework::IsInstalled()) {
            SKSE::log::info("SKSE Menu Framework is not installed; menu integration disabled");
            return;
        }

        if (SKSEMenuFramework::GetMenuFrameworkVersion() <= 0.0F) {
            SKSE::log::warn("SKSE Menu Framework returned an invalid version; menu integration disabled");
            return;
        }

        SKSEMenuFramework::SetSection("Various Dialogue Tags");
        SKSEMenuFramework::AddSectionItem("General", RenderSettings);
        registered = true;
    }
}
