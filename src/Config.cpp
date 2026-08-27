#include "Config.h"

#include <SKSE/SKSE.h>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstddef>
#include <fstream>
#include <memory>
#include <sstream>
#include <utility>
#include <vector>

namespace
{
    constexpr std::string_view kDynamicPrefix = "variousdialoguetags_";
    constexpr std::string_view kIniExtension = ".ini";
    constexpr std::string_view kUserConfigName = "variousdialoguetags_userconfig.ini";
    constexpr std::string_view kTempCacheName = "variousdialoguetags_tempcache.ini";

    std::string Trim(std::string value)
    {
        const auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
        value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
        value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
        return value;
    }

    std::string Lower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(),
            [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        return value;
    }

    bool IsDefaultValue(std::string value)
    {
        return Lower(Trim(std::move(value))) == "default";
    }

    bool IsDynamicConfigFile(const std::filesystem::path& path)
    {
        const auto fileName = Lower(path.filename().string());
        if (!fileName.starts_with(kDynamicPrefix) || !fileName.ends_with(kIniExtension)) {
            return false;
        }
        if (fileName == kUserConfigName || fileName == kTempCacheName) {
            return false;
        }
        return fileName.size() > kDynamicPrefix.size() + kIniExtension.size();
    }

    std::string NormalizeTag(std::string value)
    {
        value = Trim(std::move(value));
        if (value.empty() || (value.front() == '[' && value.back() == ']')) {
            return value;
        }
        value.insert(value.begin(), '[');
        value.push_back(']');
        return value;
    }

    std::optional<bool> ParseBoolValue(std::string value)
    {
        value = Lower(Trim(std::move(value)));
        if (value == "true" || value == "1" || value == "yes" || value == "on") {
            return true;
        }
        if (value == "false" || value == "0" || value == "no" || value == "off") {
            return false;
        }
        return std::nullopt;
    }

    bool ParseBool(std::string value, bool fallback)
    {
        if (const auto parsed = ParseBoolValue(std::move(value))) {
            return *parsed;
        }
        return fallback;
    }

    std::optional<std::uint32_t> ParseFormID(std::string token)
    {
        token = Trim(std::move(token));
        if (token.empty()) {
            return std::nullopt;
        }
        if (token.starts_with("0x") || token.starts_with("0X")) {
            token.erase(0, 2);
        }
        std::uint32_t value{};
        const auto [ptr, error] = std::from_chars(
            token.data(), token.data() + token.size(), value, 16);
        if (error != std::errc{} || ptr != token.data() + token.size()) {
            return std::nullopt;
        }
        return value;
    }

    bool ParseFormList(std::string value, std::unordered_set<std::uint32_t>& destination)
    {
        bool valid = true;
        std::stringstream stream(std::move(value));
        std::string token;
        while (std::getline(stream, token, ',')) {
            if (const auto parsed = ParseFormID(std::move(token))) {
                destination.insert(*parsed);
            } else {
                valid = false;
            }
        }
        return valid;
    }
}

namespace VariousDialogueTags
{
    bool Rule::Allows(std::uint32_t localFormID) const
    {
        if (excludeForms.contains(localFormID)) {
            return false;
        }
        return includeForms.empty() || includeForms.contains(localFormID);
    }

    Config& Config::GetSingleton()
    {
        static Config instance;
        return instance;
    }

    bool Config::Load(std::string_view embeddedInternalData,
        const std::filesystem::path& userConfigPath,
        const std::filesystem::path& tempCachePath)
    {
        rules_.clear();
        enabled_ = true;
        globalPluginNameFallback_ = false;
        immersiveMode_ = false;
        userEnabledOverride_.reset();
        userGlobalPluginNameFallbackOverride_.reset();
        userImmersiveModeOverride_.reset();
        userConfigPath_ = userConfigPath;
        tempCachePath_ = tempCachePath;

        std::istringstream internalDataInput{ std::string(embeddedInternalData) };
        const bool loadedEmbeddedData = embeddedInternalData.empty() ?
            false : LoadStream(internalDataInput, "embedded internal data", false, true, false);
        const bool loadedTempCache = LoadTempCache(tempCachePath_);
        const bool loadedUserConfig = LoadUserFile(userConfigPath_);
        if (loadedUserConfig && !SyncUserGeneralSettingsToTempCache()) {
            SKSE::log::error("Failed to synchronize one or more explicit user settings to temp cache");
        }
        const auto dynamicConfigCount = LoadDynamicFiles(userConfigPath_.parent_path());

        SKSE::log::info(
            "Configuration complete: {} dialogue-tag rule(s); enabled={}; "
            "globalPluginNameFallback={}; immersiveMode={}; embeddedData={}; "
            "userConfig={}; tempCache={}; dynamicConfigs={}",
            rules_.size(), enabled_, globalPluginNameFallback_, immersiveMode_,
            loadedEmbeddedData, loadedUserConfig, loadedTempCache, dynamicConfigCount);
        return loadedEmbeddedData || loadedUserConfig || loadedTempCache || dynamicConfigCount > 0;
    }

    bool Config::LoadUserFile(const std::filesystem::path& path)
    {
        std::ifstream input(path);
        if (!input) {
            SKSE::log::info("Optional user configuration not found: {}", path.string());
            return false;
        }

        return LoadStream(input, path.filename().string(), true, true, true);
    }

    bool Config::LoadTempCache(const std::filesystem::path& path)
    {
        if (path.empty()) {
            return false;
        }

        std::ifstream input(path);
        if (!input) {
            return false;
        }

        return LoadStream(input, path.filename().string(), true, false, false);
    }

    std::size_t Config::LoadDynamicFiles(const std::filesystem::path& directory)
    {
        if (directory.empty()) {
            return 0;
        }

        std::error_code error;
        std::filesystem::directory_iterator iterator(directory, error);
        if (error) {
            SKSE::log::warn("Unable to scan dynamic configuration directory {}: {}",
                directory.string(), error.message());
            return 0;
        }

        std::vector<std::filesystem::path> paths;
        const std::filesystem::directory_iterator end;
        for (; iterator != end; iterator.increment(error)) {
            if (error) {
                break;
            }
            std::error_code typeError;
            if (!iterator->is_regular_file(typeError) || typeError) {
                continue;
            }
            if (IsDynamicConfigFile(iterator->path())) {
                paths.push_back(iterator->path());
            }
        }
        if (error) {
            SKSE::log::warn("Dynamic configuration scan stopped in {}: {}",
                directory.string(), error.message());
        }

        std::sort(paths.begin(), paths.end(), [](const auto& left, const auto& right) {
            const auto leftName = Lower(left.filename().string());
            const auto rightName = Lower(right.filename().string());
            if (leftName != rightName) {
                return leftName < rightName;
            }
            return left.filename().string() < right.filename().string();
        });

        std::size_t loadedFiles = 0;
        for (const auto& path : paths) {
            std::ifstream input(path);
            if (!input) {
                SKSE::log::warn("Unable to open dynamic configuration: {}", path.string());
                continue;
            }
            if (LoadStream(input, path.filename().string(), false, true, true)) {
                ++loadedFiles;
            }
        }
        return loadedFiles;
    }

    bool Config::LoadStream(std::istream& input, std::string sourceName,
        bool loadGeneralSettings, bool loadPluginRules, bool userOverride)
    {
        struct ParsedRule
        {
            Rule rule;
            bool tagSpecified{};
            bool includeFormsSpecified{};
            bool excludeFormsSpecified{};
            bool modNameTagsSpecified{};
        };

        std::unordered_map<std::string, ParsedRule> fileRules;
        enum class Section { kOther, kGeneral, kPlugin };
        Section activeSection = Section::kOther;
        ParsedRule* activeRule = nullptr;
        std::string line;
        std::size_t lineNumber = 0;
        while (std::getline(input, line)) {
            ++lineNumber;
            line = Trim(std::move(line));
            if (line.empty() || line.starts_with(';') || line.starts_with('#')) {
                continue;
            }

            if (line.front() == '[' && line.back() == ']') {
                auto section = Trim(line.substr(1, line.size() - 2));
                const auto lowered = Lower(section);
                activeSection = Section::kOther;
                activeRule = nullptr;
                constexpr std::string_view prefix = "plugin:";
                if (loadGeneralSettings && lowered == "general") {
                    activeSection = Section::kGeneral;
                } else if (loadPluginRules && lowered.starts_with(prefix)) {
                    auto pluginName = Trim(section.substr(prefix.size()));
                    if (!pluginName.empty()) {
                        const auto key = Lower(std::move(pluginName));
                        auto result = userOverride ?
                            fileRules.try_emplace(key, ParsedRule{}) :
                            fileRules.insert_or_assign(key, ParsedRule{});
                        activeRule = std::addressof(result.first->second);
                        activeSection = Section::kPlugin;
                    }
                }
                continue;
            }

            const auto separator = line.find('=');
            if (separator == std::string::npos) {
                SKSE::log::warn("Ignored malformed line {} in {}", lineNumber, sourceName);
                continue;
            }

            auto key = Lower(Trim(line.substr(0, separator)));
            auto value = Trim(line.substr(separator + 1));

            if (activeSection == Section::kGeneral) {
                if (IsDefaultValue(value)) {
                    continue;
                }
                if (key == "enabled") {
                    if (const auto parsed = ParseBoolValue(value)) {
                        enabled_ = *parsed;
                        if (userOverride) {
                            userEnabledOverride_ = *parsed;
                        }
                    }
                } else if (key == "globalpluginnamefallback") {
                    if (const auto parsed = ParseBoolValue(value)) {
                        globalPluginNameFallback_ = *parsed;
                        if (userOverride) {
                            userGlobalPluginNameFallbackOverride_ = *parsed;
                        }
                    }
                } else if (key == "immersivemode") {
                    if (const auto parsed = ParseBoolValue(value)) {
                        immersiveMode_ = *parsed;
                        if (userOverride) {
                            userImmersiveModeOverride_ = *parsed;
                        }
                    }
                }
                continue;
            }

            if (activeSection != Section::kPlugin || !activeRule) {
                continue;
            }

            if (key == "tag") {
                activeRule->tagSpecified = true;
                activeRule->rule.tag = NormalizeTag(std::move(value));
            } else if (key == "includeforms") {
                if (userOverride) {
                    std::unordered_set<std::uint32_t> forms;
                    if (ParseFormList(value, forms)) {
                        activeRule->includeFormsSpecified = true;
                        activeRule->rule.includeForms = std::move(forms);
                    } else {
                        SKSE::log::warn("Ignored invalid IncludeForms at line {} in {}",
                            lineNumber, sourceName);
                    }
                } else {
                    activeRule->includeFormsSpecified = true;
                    activeRule->rule.includeForms.clear();
                    ParseFormList(std::move(value), activeRule->rule.includeForms);
                }
            } else if (key == "excludeforms") {
                if (userOverride) {
                    std::unordered_set<std::uint32_t> forms;
                    if (ParseFormList(value, forms)) {
                        activeRule->excludeFormsSpecified = true;
                        activeRule->rule.excludeForms = std::move(forms);
                    } else {
                        SKSE::log::warn("Ignored invalid ExcludeForms at line {} in {}",
                            lineNumber, sourceName);
                    }
                } else {
                    activeRule->excludeFormsSpecified = true;
                    activeRule->rule.excludeForms.clear();
                    ParseFormList(std::move(value), activeRule->rule.excludeForms);
                }
            } else if (key == "modnametags") {
                if (!userOverride || value.empty()) {
                    activeRule->modNameTagsSpecified = true;
                    activeRule->rule.modNameTags = ParseBool(value, true);
                } else if (const auto parsed = ParseBoolValue(value)) {
                    activeRule->modNameTagsSpecified = true;
                    activeRule->rule.modNameTags = *parsed;
                } else {
                    SKSE::log::warn("Ignored invalid ModNameTags at line {} in {}",
                        lineNumber, sourceName);
                }
            }
        }

        std::size_t loaded = 0;
        std::size_t overridden = 0;
        for (auto& [pluginName, parsed] : fileRules) {
            const auto existing = rules_.find(pluginName);
            const bool hasExistingRule = existing != rules_.end();
            Rule rule;

            if (userOverride) {
                if (hasExistingRule) {
                    rule = existing->second;
                }
                if (parsed.tagSpecified) {
                    rule.tag = std::move(parsed.rule.tag);
                }
                if (parsed.includeFormsSpecified) {
                    rule.includeForms = std::move(parsed.rule.includeForms);
                }
                if (parsed.excludeFormsSpecified) {
                    rule.excludeForms = std::move(parsed.rule.excludeForms);
                }
                if (parsed.modNameTagsSpecified) {
                    rule.modNameTags = parsed.rule.modNameTags;
                }
            } else {
                rule = std::move(parsed.rule);
            }

            if (!userOverride && rule.tag.empty() && rule.modNameTags) {
                SKSE::log::warn("Ignored rule without Tag for [{}] in {}",
                    pluginName, sourceName);
                continue;
            }
            if (hasExistingRule) {
                ++overridden;
            }
            rules_.insert_or_assign(std::move(pluginName), std::move(rule));
            ++loaded;
        }

        SKSE::log::info("Loaded {} rule(s) from {}; {} override(s)",
            loaded, sourceName, overridden);
        return true;
    }

    bool Config::Enabled() const noexcept
    {
        return enabled_;
    }

    bool Config::SetEnabledFromMenu(bool enabled)
    {
        enabled_ = enabled;
        return PersistMenuSetting("Enabled", enabled ? "true" : "false");
    }

    bool Config::PersistMenuSetting(std::string_view key, std::string_view value) const
    {
        bool saved = true;
        if (!UpdateGeneralSetting(tempCachePath_, key, value, true)) {
            SKSE::log::error("Failed to save SKSE Menu Framework setting {} to temp cache: {}",
                key, tempCachePath_.string());
            saved = false;
        }

        std::error_code error;
        const bool userConfigExists = !userConfigPath_.empty() &&
            std::filesystem::exists(userConfigPath_, error);
        if (error) {
            SKSE::log::error("Failed to check user configuration path {}: {}",
                userConfigPath_.string(), error.message());
            return false;
        }

        if (userConfigExists &&
            !UpdateGeneralSetting(userConfigPath_, key, value, false)) {
            SKSE::log::error(
                "Failed to save SKSE Menu Framework setting {} to user configuration: {}",
                key, userConfigPath_.string());
            saved = false;
        }

        return saved;
    }

    bool Config::SyncUserGeneralSettingsToTempCache() const
    {
        bool saved = true;
        const auto sync = [this, &saved](
                              std::string_view key, const std::optional<bool>& setting) {
            if (!setting.has_value()) {
                return;
            }
            if (!UpdateGeneralSetting(
                    tempCachePath_, key, *setting ? "true" : "false", true)) {
                saved = false;
            }
        };

        sync("Enabled", userEnabledOverride_);
        sync("GlobalPluginNameFallback", userGlobalPluginNameFallbackOverride_);
        sync("ImmersiveMode", userImmersiveModeOverride_);
        return saved;
    }

    bool Config::UpdateGeneralSetting(const std::filesystem::path& path,
        std::string_view key, std::string_view value, bool createIfMissing) const
    {
        if (path.empty()) {
            return false;
        }

        std::error_code error;
        const bool exists = std::filesystem::exists(path, error);
        if (error || (!exists && !createIfMissing)) {
            return false;
        }

        std::string contents;
        if (exists) {
            std::ifstream input(path, std::ios::binary);
            if (!input) {
                return false;
            }
            std::ostringstream buffer;
            buffer << input.rdbuf();
            if (input.bad()) {
                return false;
            }
            contents = buffer.str();
        }

        struct TextLine
        {
            std::string text;
            std::string ending;
        };

        std::vector<TextLine> lines;
        std::size_t cursor = 0;
        while (cursor < contents.size()) {
            const auto newlinePosition = contents.find('\n', cursor);
            if (newlinePosition == std::string::npos) {
                lines.push_back({ contents.substr(cursor), {} });
                break;
            }

            auto textEnd = newlinePosition;
            std::string ending = "\n";
            if (textEnd > cursor && contents[textEnd - 1] == '\r') {
                --textEnd;
                ending = "\r\n";
            }
            lines.push_back({ contents.substr(cursor, textEnd - cursor), std::move(ending) });
            cursor = newlinePosition + 1;
        }

        std::string preferredLineEnding = "\n";
        for (const auto& line : lines) {
            if (!line.ending.empty()) {
                preferredLineEnding = line.ending;
                break;
            }
        }
        const bool hadFinalNewline = !contents.empty() && contents.back() == '\n';

        const auto normalizedKey = Lower(std::string(key));
        bool inGeneral = false;
        bool foundGeneral = false;
        bool trackingLastGeneral = false;
        bool foundSetting = false;
        bool changed = false;
        std::size_t lastGeneralEnd = lines.size();

        for (std::size_t i = 0; i < lines.size(); ++i) {
            auto parsedLine = Trim(lines[i].text);
            if (i == 0 && parsedLine.starts_with("\xEF\xBB\xBF")) {
                parsedLine.erase(0, 3);
            }

            if (!parsedLine.empty() && parsedLine.front() == '[' && parsedLine.back() == ']') {
                if (trackingLastGeneral) {
                    lastGeneralEnd = i;
                    trackingLastGeneral = false;
                }
                const auto sectionName = Lower(Trim(
                    parsedLine.substr(1, parsedLine.size() - 2)));
                inGeneral = sectionName == "general";
                if (inGeneral) {
                    foundGeneral = true;
                    trackingLastGeneral = true;
                    lastGeneralEnd = lines.size();
                }
                continue;
            }

            if (!inGeneral || parsedLine.empty() ||
                parsedLine.starts_with(';') || parsedLine.starts_with('#')) {
                continue;
            }

            const auto separator = lines[i].text.find('=');
            if (separator == std::string::npos ||
                Lower(Trim(lines[i].text.substr(0, separator))) != normalizedKey) {
                continue;
            }

            foundSetting = true;
            std::size_t valueStart = separator + 1;
            while (valueStart < lines[i].text.size() &&
                   std::isspace(static_cast<unsigned char>(lines[i].text[valueStart]))) {
                ++valueStart;
            }

            std::size_t valueEnd = lines[i].text.size();
            for (std::size_t j = valueStart; j < lines[i].text.size(); ++j) {
                const char ch = lines[i].text[j];
                if (ch == ';' || ch == '#') {
                    valueEnd = j;
                    break;
                }
            }
            while (valueEnd > valueStart &&
                   std::isspace(static_cast<unsigned char>(lines[i].text[valueEnd - 1]))) {
                --valueEnd;
            }

            if (lines[i].text.substr(valueStart, valueEnd - valueStart) != value) {
                lines[i].text.replace(valueStart, valueEnd - valueStart, value);
                changed = true;
            }
        }

        if (trackingLastGeneral) {
            lastGeneralEnd = lines.size();
        }

        if (!foundSetting) {
            TextLine settingLine{
                std::string(key) + " = " + std::string(value),
                preferredLineEnding
            };

            if (foundGeneral) {
                if (lastGeneralEnd == lines.size()) {
                    if (!lines.empty() && lines.back().ending.empty()) {
                        lines.back().ending = preferredLineEnding;
                    }
                    settingLine.ending = hadFinalNewline ? preferredLineEnding : std::string{};
                    lines.push_back(std::move(settingLine));
                } else {
                    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(lastGeneralEnd),
                        std::move(settingLine));
                }
            } else {
                if (!lines.empty()) {
                    if (lines.back().ending.empty()) {
                        lines.back().ending = preferredLineEnding;
                    }
                    if (!Trim(lines.back().text).empty()) {
                        lines.push_back({ {}, preferredLineEnding });
                    }
                }
                lines.push_back({ "[General]", preferredLineEnding });
                lines.push_back({
                    std::string(key) + " = " + std::string(value),
                    (hadFinalNewline || contents.empty()) ? preferredLineEnding : std::string{}
                });
            }
            changed = true;
        }

        if (exists && !changed) {
            return true;
        }

        if (!exists) {
            const auto parent = path.parent_path();
            if (!parent.empty()) {
                std::filesystem::create_directories(parent, error);
                if (error) {
                    return false;
                }
            }
        }

        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output) {
            return false;
        }
        for (const auto& line : lines) {
            output << line.text << line.ending;
        }
        return static_cast<bool>(output);
    }

    bool Config::GlobalPluginNameFallback() const noexcept
    {
        return globalPluginNameFallback_;
    }

    bool Config::SetGlobalPluginNameFallbackFromMenu(bool enabled)
    {
        globalPluginNameFallback_ = enabled;
        return PersistMenuSetting(
            "GlobalPluginNameFallback", enabled ? "true" : "false");
    }

    bool Config::ImmersiveMode() const noexcept
    {
        return immersiveMode_;
    }

    bool Config::SetImmersiveModeFromMenu(bool enabled)
    {
        immersiveMode_ = enabled;
        return PersistMenuSetting("ImmersiveMode", enabled ? "true" : "false");
    }

    const Rule* Config::FindRule(std::string_view pluginName) const
    {
        auto key = Lower(std::string(pluginName));
        const auto found = rules_.find(key);
        return found == rules_.end() ? nullptr : std::addressof(found->second);
    }
}
