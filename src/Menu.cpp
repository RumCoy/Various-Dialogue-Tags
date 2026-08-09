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
            if (ImGuiMCP::IsItemHovered(0)) {
                ImGuiMCP::SetTooltip(
                    "DEFAULT: OFF. Uses the plugin filename as the tag for dialogue from "
                    "unconfigured non-vanilla plugins.");
            }

            bool immersiveMode = config.ImmersiveMode();
            if (ImGuiMCP::Checkbox("Immersive Mode", &immersiveMode)) {
                if (!config.SetImmersiveModeFromMenu(immersiveMode)) {
                    SKSE::log::error("Failed to persist one or more menu setting files");
                }
            }
            if (ImGuiMCP::IsItemHovered(0)) {
                ImGuiMCP::SetTooltip(
                    "Hides tags when only one tag is present in the dialogue list.");
            }
        }
    }

    bool IsAvailable()
    {
        return SKSEMenuFramework::IsInstalled() &&
               SKSEMenuFramework::GetMenuFrameworkVersion() > 0.0F;
    }

    void Register()
    {
        static bool registered = false;
        if (registered) {
            return;
        }

        if (!IsAvailable()) {
            SKSE::log::info("SKSE Menu Framework unavailable; menu integration disabled");
            return;
        }

        SKSEMenuFramework::SetSection("Various Dialogue Tags");
        SKSEMenuFramework::AddSectionItem("Home", RenderSettings);
        registered = true;
    }
}
