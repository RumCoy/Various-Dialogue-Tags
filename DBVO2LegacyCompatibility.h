#pragma once

#include <RE/Skyrim.h>

namespace VariousDialogueTags
{
    class DBVO2LegacyCompatibility final : public RE::DialogueMenu
    {
    public:
        static void Install();

    private:
        RE::UI_MESSAGE_RESULTS ProcessMessageHook(RE::UIMessage& message);
        static inline REL::Relocation<decltype(&RE::DialogueMenu::ProcessMessage)> original_;
    };
}
