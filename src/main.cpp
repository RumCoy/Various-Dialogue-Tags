#include "Config.h"
#include "DBVO2LegacyCompatibility.h"
#include "DialogueMenuHook.h"
#include "EmbeddedData.h"
#include "Menu.h"

#include <filesystem>
#include <SKSE/SKSE.h>
#include <Windows.h>

namespace
{
    void OnSKSEMessage(SKSE::MessagingInterface::Message* message)
    {
        if (!message) {
            return;
        }

        if (message->type == SKSE::MessagingInterface::kPostPostLoad) {
            const bool voiceOverLoaded =
                ::GetModuleHandleW(L"DBVO.dll") != nullptr ||
                ::GetModuleHandleW(L"DBReV.dll") != nullptr;

            if (voiceOverLoaded) {
                VariousDialogueTags::DBVO2LegacyCompatibility::Install();
            }
            return;
        }

        if (message->type != SKSE::MessagingInterface::kDataLoaded) {
            return;
        }

        const auto internalData = VariousDialogueTags::EmbeddedData::GetInternalData();
        if (internalData.empty()) {
            SKSE::log::critical("Embedded internal-data resource is missing or empty");
        }

        VariousDialogueTags::Config::GetSingleton().Load(
            internalData,
            "Data/SKSE/Plugins/VariousDialogueTags_UserConfig.ini",
            "Data/SKSE/Plugins/VariousDialogueTags_tempCache.ini");

        VariousDialogueTags::Menu::Register();
        SKSE::log::info("Configuration loaded");
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
    SKSE::Init(skse);

    VariousDialogueTags::DialogueMenuHook::Install();

    auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging || !messaging->RegisterListener(OnSKSEMessage)) {
        SKSE::log::critical("Unable to register the SKSE messaging listener");
        return false;
    }

    SKSE::log::info("Various Dialogue Tags initialized; DialogueMenu hook installed");
    return true;
}
