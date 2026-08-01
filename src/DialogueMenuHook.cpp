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
                if (IsBlank(currentText)) {
                    continue;
                }
                const auto cacheKey = MakeCacheKey(
                    topic->GetFormID(), topicInfo ? topicInfo->GetFormID() : 0, currentText);

                if (const auto cached = cache.find(cacheKey); cached != cache.end()) {
                    option->topicText = cached->second;
                    continue;
                }

                std::string output = currentText;
                const auto topicProvenance = FormOrigin::Resolve(topic);
                const auto topicInfoProvenance = FormOrigin::Resolve(topicInfo);
                const bool topicEligible = topicProvenance &&
                    !IsExcludedOriginPlugin(topicProvenance->origin.filename);
                const bool topicInfoEligible = topicInfoProvenance &&
                    !IsExcludedOriginPlugin(topicInfoProvenance->origin.filename);
                if (topicEligible || topicInfoEligible) {
                    std::array<const FormOrigin::Identity*, 4> candidates{};
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

                    const Rule* rule = nullptr;
                    const FormOrigin::Identity* tagIdentity = nullptr;
                    bool tagsEnabled = true;
                    for (std::size_t index = 0; index < candidateCount; ++index) {
                        const auto* candidateRule =
                            config.FindRule(candidates[index]->filename);
                        if (!candidateRule) {
                            continue;
                        }
                        if (!candidateRule->modNameTags) {
                            tagsEnabled = false;
                            break;
                        }
                        if (!rule) {
                            rule = candidateRule;
                            tagIdentity = candidates[index];
                        }
                    }

                    std::string fallbackTag;
                    std::string_view tag;

                    if (tagsEnabled && rule) {
                        if (rule->Allows(tagIdentity->localFormID)) {
                            tag = rule->tag;
                        }
                    } else if (tagsEnabled && globalPluginNameFallback) {
                        for (std::size_t index = 0; index < candidateCount; ++index) {
                            fallbackTag = MakeFallbackTag(candidates[index]->filename);
                            if (!fallbackTag.empty()) {
                                tagIdentity = candidates[index];
                                tag = fallbackTag;
                                break;
                            }
                        }
                    }

                    if (!tag.empty()) {
                        std::string displayText = currentText;
                        if (currentText.starts_with('$') &&
                            !SKSE::Translation::Translate(currentText, displayText)) {
                            SKSE::log::debug("Deferred unresolved localization token: {}", currentText);
                            continue;
                        }
                        if (IsBlank(displayText)) {
                            SKSE::log::debug("Deferred blank dialogue text: {}", currentText);
                            continue;
                        }

                        output = displayText;
                        if (!HasPrefix(output, tag)) {
                            output.reserve(tag.size() + 1 + displayText.size());
                            output.assign(tag);
                            output.push_back(' ');
                            output.append(displayText);
                            SKSE::log::debug("Tagged {}|{:X}: {}", tagIdentity->filename,
                                tagIdentity->localFormID, displayText);
                        }
                    }
                }

                option->topicText = output;
                cache.emplace(cacheKey, std::move(output));
            }
        }

        return original_(this, message);
    }
}
