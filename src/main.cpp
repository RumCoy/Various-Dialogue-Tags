#include "Config.h"
#include "DBVO2LegacyCompatibility.h"
#include "DialogueMenuHook.h"
#include "EmbeddedData.h"
#include "Menu.h"

#include <filesystem>
#include <SKSE/SKSE.h>

namespace
{
    void OnSKSEMessage(SKSE::MessagingInterface::Message* message)
    {
        if (!message || message->type != SKSE::MessagingInterface::kDataLoaded) {
            return;
        }

        const auto internalData = VariousDialogueTags::EmbeddedData::GetInternalData();
        if (internalData.empty()) {
            SKSE::log::critical("Embedded internal-data resource is missing or empty");
        }

        std::filesystem::path tempCachePath;
        if (VariousDialogueTags::Menu::IsAvailable()) {
            tempCachePath = "Data/SKSE/Plugins/VariousDialogueTags_tempCache.ini";
        } else {
            SKSE::log::info(
                "SKSE Menu Framework unavailable; existing temp cache will be ignored");
        }

        VariousDialogueTags::Config::GetSingleton().Load(
            internalData,
            "Data/SKSE/Plugins/VariousDialogueTags_UserConfig.ini",
            tempCachePath);

        VariousDialogueTags::Menu::Register();
        SKSE::log::info("Configuration loaded");
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
    SKSE::Init(skse);

    VariousDialogueTags::DialogueMenuHook::Install();
    VariousDialogueTags::DBVO2LegacyCompatibility::Install();

    auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging || !messaging->RegisterListener(OnSKSEMessage)) {
        SKSE::log::critical("Unable to register the SKSE messaging listener");
        return false;
    }

    SKSE::log::info("Various Dialogue Tags initialized; DialogueMenu hook installed");
    return true;
}
