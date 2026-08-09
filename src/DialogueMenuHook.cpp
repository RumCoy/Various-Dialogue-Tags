#include "DialogueMenuHook.h"

#include "Config.h"
#include "FormOrigin.h"

#include <SKSE/SKSE.h>
#include <SKSE/Translation.h>

#include <array>
#include <cctype>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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

    bool IsExcludedOriginPlugin(std::string_view filename)
    {
        constexpr std::string_view excludedOriginPlugins[] = {
            "Skyrim.esm",
            "Update.esm",
            "Dawnguard.esm",
            "HearthFires.esm",
            "Dragonborn.esm",
            "ccasvsse001-almsivi.esm",
            "ccBGSSSE001-Fish.esm",
            "ccbgssse003-zombies.esl",
            "ccbgssse005-goldbrand.esl",
            "ccbgssse020-graycowl.esl",
            "cctwbsse001-puzzledungeon.esm",
            "cceejsse001-hstead.esm",
            "ccbgssse035-petnhound.esl",
            "ccvsvsse002-pets.esl",
            "ccbgssse034-mntuni.esm",
            "ccbgssse036-petbwolf.esl",
            "ccffbsse001-imperialdragon.esl",
            "ccmtysse002-ve.esl",
            "cceejsse003-hollow.esm",
            "ccbgssse031-advcyrus.esm",
            "ccbgssse038-bowofshadows.esl",
            "ccbgssse040-advobgobs.esl",
            "ccbgssse059-ba_dragonplate.esl",
            "ccbgssse041-netchleather.esl",
            "ccbgssse063-ba_ebony.esl",
            "ccbgssse055-ba_orcishscaled.esl",
            "ccbgssse051-ba_daedricmail.esl",
            "ccbgssse067-daedinv.esm",
            "ccbgssse068-bloodfall.esl",
            "ccbgssse069-contest.esl",
            "ccVSVSSE003-NecroArts.esl",
            "ccvsvsse004-beafarmer.esl",
            "ccbgssse025-advdsgs.esm",
            "ccrmssse001-necrohouse.esl",
            "ccedhsse003-redguard.esl",
            "cceejsse004-hall.esl",
            "cceejsse005-cave.esm",
            "cckrtsse001_altar.esl",
            "ccafdsse001-dwesanctuary.esm"
        };

        for (const auto excludedOriginPlugin : excludedOriginPlugins) {
            if (EqualsIgnoreCase(filename, excludedOriginPlugin)) {
                return true;
            }
        }
        return false;
    }

    std::string MakeFallbackTag(std::string_view filename)
    {
        if (filename.empty() || IsExcludedOriginPlugin(filename)) {
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

    bool IsBlank(std::string_view text)
    {
        for (const auto character : text) {
            if (std::isspace(static_cast<unsigned char>(character)) == 0) {
                return false;
            }
        }
        return true;
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
            if (candidates[index]->file == candidate.file &&
                candidates[index]->localFormID == candidate.localFormID) {
                return;
            }
        }
        candidates[candidateCount++] = std::addressof(candidate);
    }

    struct TagResolution
    {
        std::string tag;
        std::string filename;
        std::uint32_t localFormID{};
    };

    TagResolution ResolveTag(
        const VariousDialogueTags::Config& config,
        const RE::TESTopic* topic,
        const RE::TESTopicInfo* topicInfo,
        bool globalPluginNameFallback)
    {
        const auto topicProvenance = VariousDialogueTags::FormOrigin::Resolve(topic);
        const auto topicInfoProvenance = VariousDialogueTags::FormOrigin::Resolve(topicInfo);
        const bool topicEligible = topicProvenance &&
            !IsExcludedOriginPlugin(topicProvenance->origin.filename);
        const bool topicInfoEligible = topicInfoProvenance &&
            !IsExcludedOriginPlugin(topicInfoProvenance->origin.filename);
        if (!topicEligible && !topicInfoEligible) {
            return {};
        }

        std::array<const VariousDialogueTags::FormOrigin::Identity*, 4> candidates{};
        std::size_t candidateCount = 0;

        const auto* topicOrigin = topicEligible ?
            std::addressof(topicProvenance->origin) : nullptr;
        if (topicInfoEligible) {
            if (!topicOrigin || topicInfoProvenance->winner.file != topicOrigin->file) {
                AddCandidate(candidates, candidateCount, topicInfoProvenance->winner);
            }
            if (!topicOrigin || topicInfoProvenance->origin.file != topicOrigin->file) {
                AddCandidate(candidates, candidateCount, topicInfoProvenance->origin);
            }
        }
        if (topicEligible) {
            AddCandidate(candidates, candidateCount, topicProvenance->winner);
            AddCandidate(candidates, candidateCount, topicProvenance->origin);
        }

        bool explicitRuleFound = false;
        for (std::size_t index = 0; index < candidateCount; ++index) {
            const auto* candidateRule = config.FindRule(candidates[index]->filename);
            if (!candidateRule) {
                continue;
            }
            explicitRuleFound = true;
            if (!candidateRule->modNameTags) {
                return {};
            }
        }

        for (std::size_t index = 0; index < candidateCount; ++index) {
            const auto* candidateRule = config.FindRule(candidates[index]->filename);
            if (candidateRule && candidateRule->Allows(candidates[index]->localFormID)) {
                return TagResolution{
                    .tag = candidateRule->tag,
                    .filename = candidates[index]->filename,
                    .localFormID = candidates[index]->localFormID
                };
            }
        }

        if (!explicitRuleFound && globalPluginNameFallback) {
            for (std::size_t index = 0; index < candidateCount; ++index) {
                auto tag = MakeFallbackTag(candidates[index]->filename);
                if (!tag.empty()) {
                    return TagResolution{
                        .tag = std::move(tag),
                        .filename = candidates[index]->filename,
                        .localFormID = candidates[index]->localFormID
                    };
                }
            }
        }

        return {};
    }

    struct ConversationState
    {
        bool initialized{};
        RE::ObjectRefHandle speaker;
        const RE::MenuTopicManager::Dialogue* selectedDialogue{};
        RE::FormID selectedTopic{};
        RE::FormID selectedTopicInfo{};
        std::string activeTag;
        bool hasPreviousMenu{};
        bool previousMenuUniform{};
        std::string previousMenuSignature;
        std::string previousMenuTag;
    };

    void SetObservedSelection(
        ConversationState& state,
        const RE::MenuTopicManager::Dialogue* selection)
    {
        state.selectedDialogue = selection;
        state.selectedTopic = selection && selection->parentTopic ?
            selection->parentTopic->GetFormID() : 0;
        state.selectedTopicInfo = selection && selection->parentTopicInfo ?
            selection->parentTopicInfo->GetFormID() : 0;
    }

    bool SelectionChanged(
        const ConversationState& state,
        const RE::MenuTopicManager::Dialogue* selection)
    {
        const auto topic = selection && selection->parentTopic ?
            selection->parentTopic->GetFormID() : 0;
        const auto topicInfo = selection && selection->parentTopicInfo ?
            selection->parentTopicInfo->GetFormID() : 0;
        return selection != state.selectedDialogue ||
               topic != state.selectedTopic ||
               topicInfo != state.selectedTopicInfo;
    }

    bool IsTopLevelMenu(const RE::MenuTopicManager& topicManager)
    {
        if (!topicManager.dialogueList) {
            return false;
        }

        for (auto* option : *topicManager.dialogueList) {
            if (!option || !option->parentTopic || !option->parentTopic->ownerBranch) {
                continue;
            }

            auto* branch = option->parentTopic->ownerBranch;
            if (branch->startingTopic != option->parentTopic) {
                continue;
            }

            for (auto* topLevelBranch : topicManager.topLevelBranches) {
                if (topLevelBranch == branch) {
                    return true;
                }
            }
        }

        return false;
    }

    struct PendingOption
    {
        RE::MenuTopicManager::Dialogue* option{};
        std::string text;
        std::string cacheKey;
        TagResolution tag;
        bool sameBranchTag{};
    };

    struct CachedOption
    {
        std::string source;
        std::string output;
        std::string tag;
        bool tagged{};
    };

    std::string MakeCacheKey(
        const RE::MenuTopicManager::Dialogue* option,
        RE::FormID topicFormID,
        RE::FormID topicInfoFormID)
    {
        return std::to_string(reinterpret_cast<std::uintptr_t>(option)) + '\n' +
               std::to_string(topicFormID) + '\n' + std::to_string(topicInfoFormID);
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
        static std::unordered_map<std::string, CachedOption> cache;
        static RE::FormID lastConversationRoot = 0;
        static ConversationState conversation;

        auto* topicManager = RE::MenuTopicManager::GetSingleton();
        const auto currentRoot = topicManager && topicManager->rootTopicInfo ?
            topicManager->rootTopicInfo->GetFormID() : 0;

        const bool menuClosing =
            message.type == RE::UI_MESSAGE_TYPE::kHide ||
            message.type == RE::UI_MESSAGE_TYPE::kForceHide;
        if (menuClosing) {
            cache.clear();
            lastConversationRoot = 0;
            conversation = {};
            return original_(this, message);
        }
        const bool rootChanged = currentRoot != lastConversationRoot;
        if (rootChanged) {
            cache.clear();
            lastConversationRoot = currentRoot;
        }

        const auto& config = Config::GetSingleton();
        const bool globalPluginNameFallback = config.GlobalPluginNameFallback();
        if (!config.Enabled() || !topicManager || !topicManager->dialogueList) {
            conversation = {};
            return original_(this, message);
        }

        bool contextUpdated = false;
        if (!conversation.initialized || conversation.speaker != topicManager->speaker) {
            conversation = {};
            conversation.initialized = true;
            conversation.speaker = topicManager->speaker;
            SetObservedSelection(conversation, topicManager->lastSelectedDialogue);
        } else {
            if (SelectionChanged(conversation, topicManager->lastSelectedDialogue)) {
                auto* selected = topicManager->lastSelectedDialogue;
                SetObservedSelection(conversation, selected);
                if (selected) {
                    conversation.activeTag = ResolveTag(config, selected->parentTopic,
                        selected->parentTopicInfo, globalPluginNameFallback).tag;
                    contextUpdated = true;
                }
            } else if (rootChanged && topicManager->rootTopicInfo) {
                auto* root = topicManager->rootTopicInfo;
                conversation.activeTag = ResolveTag(config, root->parentTopic, root,
                    globalPluginNameFallback).tag;
                contextUpdated = true;
            }
        }

        const bool topLevelMenu = IsTopLevelMenu(*topicManager);
        if (topLevelMenu) {
            conversation.activeTag.clear();
        }

        std::vector<PendingOption> pending;
        std::unordered_set<std::string> visibleContexts;
        std::unordered_set<std::string> visibleTags;
        std::unordered_map<const RE::TESTopic*, std::string> branchTags;
        std::string menuSignature;
        for (auto* option : *topicManager->dialogueList) {
            if (!option || !option->parentTopic) {
                continue;
            }

            std::string currentText = option->topicText.c_str();
            if (IsBlank(currentText)) {
                continue;
            }

            auto* topicInfo = option->parentTopicInfo;
            const auto topicFormID = option->parentTopic->GetFormID();
            const auto topicInfoFormID = topicInfo ? topicInfo->GetFormID() : 0;
            auto cacheKey = MakeCacheKey(option, topicFormID, topicInfoFormID);
            if (const auto cached = cache.find(cacheKey);
                cached != cache.end() && currentText == cached->second.output) {
                currentText = cached->second.source;
            }
            menuSignature.append(cacheKey);
            menuSignature.push_back('\0');
            auto tag = ResolveTag(
                config, option->parentTopic, topicInfo, globalPluginNameFallback);
            bool sameBranchTag = false;
            auto* branch = option->parentTopic->ownerBranch;
            if (branch && branch->startingTopic &&
                branch->startingTopic != option->parentTopic) {
                auto [branchTag, inserted] = branchTags.try_emplace(
                    branch->startingTopic);
                if (inserted) {
                    branchTag->second = ResolveTag(config, branch->startingTopic,
                        nullptr, globalPluginNameFallback).tag;
                }
                sameBranchTag = !tag.tag.empty() && tag.tag == branchTag->second;
            }
            visibleContexts.insert(tag.tag);
            if (!tag.tag.empty()) {
                visibleTags.insert(tag.tag);
            }
            pending.push_back(PendingOption{
                .option = option,
                .text = currentText,
                .cacheKey = std::move(cacheKey),
                .tag = std::move(tag),
                .sameBranchTag = sameBranchTag
            });
        }

        const bool menuChanged = conversation.hasPreviousMenu &&
            menuSignature != conversation.previousMenuSignature;
        if (!topLevelMenu && !contextUpdated && menuChanged &&
            conversation.previousMenuUniform) {
            conversation.activeTag = conversation.previousMenuTag;
        }

        const bool immersiveMode = config.ImmersiveMode() && visibleTags.size() <= 1;
        for (auto& item : pending) {
            const bool sameContext = !conversation.activeTag.empty() &&
                item.tag.tag == conversation.activeTag;
            const bool shouldTag = !item.tag.tag.empty() &&
                !sameContext && !item.sameBranchTag && !immersiveMode;
            if (const auto cached = cache.find(item.cacheKey);
                cached != cache.end() && cached->second.source == item.text &&
                cached->second.tag == item.tag.tag && cached->second.tagged == shouldTag) {
                item.option->topicText = cached->second.output;
                continue;
            }

            std::string output = item.text;
            if (shouldTag) {
                std::string displayText = item.text;
                if (item.text.starts_with('$') &&
                    !SKSE::Translation::Translate(item.text, displayText)) {
                    SKSE::log::debug("Deferred unresolved localization token: {}", item.text);
                    item.option->topicText = item.text;
                    continue;
                }
                if (IsBlank(displayText)) {
                    SKSE::log::debug("Deferred blank dialogue text: {}", item.text);
                    item.option->topicText = item.text;
                    continue;
                }

                output = displayText;
                if (!HasPrefix(output, item.tag.tag)) {
                    output.reserve(item.tag.tag.size() + 1 + displayText.size());
                    output.assign(item.tag.tag);
                    output.push_back(' ');
                    output.append(displayText);
                    SKSE::log::debug("Tagged {}|{:X}: {}", item.tag.filename,
                        item.tag.localFormID, displayText);
                }
            }

            item.option->topicText = output;
            cache.insert_or_assign(std::move(item.cacheKey), CachedOption{
                .source = std::move(item.text),
                .output = std::move(output),
                .tag = std::move(item.tag.tag),
                .tagged = shouldTag
            });
        }

        conversation.hasPreviousMenu = !pending.empty();
        conversation.previousMenuUniform = visibleContexts.size() == 1;
        conversation.previousMenuSignature = std::move(menuSignature);
        conversation.previousMenuTag = conversation.previousMenuUniform ?
            *visibleContexts.begin() : std::string{};

        return original_(this, message);
    }
}
