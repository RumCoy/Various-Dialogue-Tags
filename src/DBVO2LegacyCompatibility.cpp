#include "DBVO2LegacyCompatibility.h"

#include <SKSE/SKSE.h>

#include <string>
#include <vector>

namespace
{
    struct TopicTextSnapshot
    {
        RE::MenuTopicManager::Dialogue* option{};
        std::string text;
    };
}

namespace VariousDialogueTags
{
    void DBVO2LegacyCompatibility::Install()
    {
        REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE_DialogueMenu[0] };
        original_ = vtable.write_vfunc(0x4, &DBVO2LegacyCompatibility::ProcessMessageHook);
        SKSE::log::info("Installed DBVO/DBReV compatibility hook");
    }

    RE::UI_MESSAGE_RESULTS DBVO2LegacyCompatibility::ProcessMessageHook(RE::UIMessage& message)
    {
        std::vector<TopicTextSnapshot> snapshots;

        const bool menuClosing =
            message.type == RE::UI_MESSAGE_TYPE::kHide ||
            message.type == RE::UI_MESSAGE_TYPE::kForceHide;

        if (!menuClosing) {
            if (auto* topicManager = RE::MenuTopicManager::GetSingleton();
                topicManager && topicManager->dialogueList) {
                snapshots.reserve(topicManager->dialogueList->size());
                for (auto* option : *topicManager->dialogueList) {
                    if (option) {
                        snapshots.push_back(TopicTextSnapshot{
                            .option = option,
                            .text = option->topicText.c_str()
                        });
                    }
                }
            }
        }

        const auto result = original_(this, message);

        for (const auto& snapshot : snapshots) {
            snapshot.option->topicText = snapshot.text;
        }

        return result;
    }
}
