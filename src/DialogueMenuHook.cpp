#include "DialogueMenuHook.h"

#include "Config.h"
#include "FormOrigin.h"

#include <SKSE/SKSE.h>

#include <array>
#include <cctype>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace
{
    bool EqualsIgnoreCase(std::string_view left, std::string_view right)
    {
        if (left.size() != right.size()) {
            return false;
        }

        for (std::size_t index = 0; index < left.size(); ++index) {
            const auto leftCharacter = static_cast<unsigned char>(left[index]);
            const auto rightCharacter = static_cast<unsigned char>(right[index]);
            if (std::tolower(leftCharacter) != std::tolower(rightCharacter)) {
                return false;
            }
        }
        return true;
    }

    bool IsVanillaPlugin(std::string_view filename)
    {
        constexpr std::string_view vanillaPlugins[] = {
            "Skyrim.esm",
            "Update.esm",
            "Dawnguard.esm",
            "HearthFires.esm",
            "Dragonborn.esm"
        };

        for (const auto vanillaPlugin : vanillaPlugins) {
            if (EqualsIgnoreCase(filename, vanillaPlugin)) {
                return true;
            }
        }
        return false;
    }

    std::string MakeFallbackTag(std::string_view filename)
    {
        if (filename.empty() || IsVanillaPlugin(filename)) {
            return {};
        }

        const auto extension = filename.find_last_of('.');
        const auto basename = filename.substr(0, extension);
        if (basename.empty()) {
            return {};
        }

        std::string tag;
        tag.reserve(basename.size() + 2);
        tag.push_back('[');
        tag.append(basename);
        tag.push_back(']');
        return tag;
    }

    bool HasPrefix(std::string_view text, std::string_view tag)
    {
        if (!text.starts_with(tag)) {
            return false;
        }
        return text.size() == tag.size() ||
               std::isspace(static_cast<unsigned char>(text[tag.size()])) != 0;
    }

    void AddCandidate(
        std::array<const VariousDialogueTags::FormOrigin::Identity*, 4>& candidates,
        std::size_t& candidateCount,
        const VariousDialogueTags::FormOrigin::Identity& candidate)
    {
        if (!candidate.file || candidate.filename.empty()) {
            return;
        }

        for (std::size_t index = 0; index < candidateCount; ++index) {
            if (candidates[index]->file == candidate.file) {
                return;
            }
        }
        candidates[candidateCount++] = std::addressof(candidate);
    }

    std::string MakeCacheKey(
        RE::FormID topicFormID,
        RE::FormID topicInfoFormID,
        std::string_view text)
    {
        return std::to_string(topicFormID) + '\n' +
               std::to_string(topicInfoFormID) + '\n' + std::string(text);
    }
}

namespace VariousDialogueTags
{
    void DialogueMenuHook::Install()
    {
        REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE_DialogueMenu[0] };
        original_ = vtable.write_vfunc(0x4, &DialogueMenuHook::ProcessMessageHook);
        SKSE::log::info("Installed DialogueMenu hook");
    }

    RE::UI_MESSAGE_RESULTS DialogueMenuHook::ProcessMessageHook(RE::UIMessage& message)
    {
        static std::unordered_map<std::string, std::string> cache;
        static RE::FormID lastConversationRoot = 0;

        auto* topicManager = RE::MenuTopicManager::GetSingleton();
        const auto currentRoot = topicManager && topicManager->rootTopicInfo ?
            topicManager->rootTopicInfo->GetFormID() : 0;

        const bool menuClosing =
            message.type == RE::UI_MESSAGE_TYPE::kHide ||
            message.type == RE::UI_MESSAGE_TYPE::kForceHide;
        if (menuClosing || currentRoot != lastConversationRoot) {
            cache.clear();
            lastConversationRoot = currentRoot;
        }

        const auto& config = Config::GetSingleton();
        const bool globalPluginNameFallback = config.GlobalPluginNameFallback();
        if (config.Enabled() && topicManager && topicManager->dialogueList) {
            for (auto iterator = topicManager->dialogueList->begin();
                 iterator != topicManager->dialogueList->end(); ++iterator) {
                auto* option = *iterator;
                if (!option || !option->parentTopic) {
                    continue;
                }

                auto* topic = option->parentTopic;
                auto* topicInfo = option->parentTopicInfo;
                const std::string currentText = option->topicText.c_str();
                const auto cacheKey = MakeCacheKey(
                    topic->GetFormID(), topicInfo ? topicInfo->GetFormID() : 0, currentText);

                if (const auto cached = cache.find(cacheKey); cached != cache.end()) {
                    option->topicText = cached->second;
                    continue;
                }

                std::string output = currentText;
                const auto topicProvenance = FormOrigin::Resolve(topic);
                const auto topicInfoProvenance = FormOrigin::Resolve(topicInfo);
                if (topicProvenance || topicInfoProvenance) {
                    std::array<const FormOrigin::Identity*, 4> candidates{};
                    std::size_t candidateCount = 0;

                    const auto* topicOrigin = topicProvenance ?
                        std::addressof(topicProvenance->origin) : nullptr;
                    if (topicInfoProvenance) {
                        if (!topicOrigin || topicInfoProvenance->winner.file != topicOrigin->file) {
                            AddCandidate(candidates, candidateCount, topicInfoProvenance->winner);
                        }
                        if (!topicOrigin || topicInfoProvenance->origin.file != topicOrigin->file) {
                            AddCandidate(candidates, candidateCount, topicInfoProvenance->origin);
                        }
                    }
                    if (topicProvenance) {
                        AddCandidate(candidates, candidateCount, topicProvenance->winner);
                        AddCandidate(candidates, candidateCount, topicProvenance->origin);
                    }

                    const Rule* rule = nullptr;
                    const FormOrigin::Identity* tagIdentity = nullptr;
                    for (std::size_t index = 0; index < candidateCount; ++index) {
                        if (const auto* candidateRule =
                                config.FindRule(candidates[index]->filename)) {
                            rule = candidateRule;
                            tagIdentity = candidates[index];
                            break;
                        }
                    }

                    std::string fallbackTag;
                    std::string_view tag;

                    if (rule) {
                        if (rule->Allows(tagIdentity->localFormID)) {
                            tag = rule->tag;
                        }
                    } else if (globalPluginNameFallback) {
                        for (std::size_t index = 0; index < candidateCount; ++index) {
                            fallbackTag = MakeFallbackTag(candidates[index]->filename);
                            if (!fallbackTag.empty()) {
                                tagIdentity = candidates[index];
                                tag = fallbackTag;
                                break;
                            }
                        }
                    }

                    if (!tag.empty() && !HasPrefix(output, tag)) {
                        output.reserve(tag.size() + 1 + currentText.size());
                        output.assign(tag);
                        output.push_back(' ');
                        output.append(currentText);
                        SKSE::log::debug("Tagged {}|{:X}: {}", tagIdentity->filename,
                            tagIdentity->localFormID, currentText);
                    }
                }

                option->topicText = output;
                cache.emplace(cacheKey, std::move(output));
            }
        }

        return original_(this, message);
    }
}
