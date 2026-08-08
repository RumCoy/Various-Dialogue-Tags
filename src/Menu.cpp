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

            if (ImGuiMCP::Checkbox("Enable dialogue tags", &enabled)) {
                config.SetEnabled(enabled);
                if (!config.SaveTempCache()) {
                    SKSE::log::error("Failed to save temp cache");
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
        SKSEMenuFramework::AddSectionItem("Settings", RenderSettings);
        registered = true;
    }
}
