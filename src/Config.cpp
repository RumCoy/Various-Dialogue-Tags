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

    bool ParseBool(std::string value, bool fallback)
    {
        value = Lower(Trim(std::move(value)));
        if (value == "true" || value == "1" || value == "yes" || value == "on") {
            return true;
        }
        if (value == "false" || value == "0" || value == "no" || value == "off") {
            return false;
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
        const auto [ptr, error] = std::from_chars(token.data(), token.data() + token.size(), value, 16);
        if (error != std::errc{} || ptr != token.data() + token.size()) {
            return std::nullopt;
        }
        return value;
    }

    void ParseFormList(std::string value, std::unordered_set<std::uint32_t>& destination)
    {
        std::stringstream stream(std::move(value));
        std::string token;
        while (std::getline(stream, token, ',')) {
            if (const auto parsed = ParseFormID(std::move(token))) {
                destination.insert(*parsed);
            }
        }
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
        userConfigPath_ = userConfigPath;
        tempCachePath_ = tempCachePath;

        std::istringstream internalDataInput{ std::string(embeddedInternalData) };
        const bool loadedEmbeddedData = embeddedInternalData.empty() ?
            false : LoadStream(internalDataInput, "embedded internal data", false);
        const bool loadedTempCache = LoadTempCache(tempCachePath_);
        const bool loadedUserConfig = LoadUserFile(userConfigPath_);

        SKSE::log::info(
            "Configuration complete: {} dialogue-tag rule(s); enabled={}; "
            "globalPluginNameFallback={}; immersiveMode={}; embeddedData={}; "
            "userConfig={}; tempCache={}",
            rules_.size(), enabled_, globalPluginNameFallback_, immersiveMode_,
            loadedEmbeddedData, loadedUserConfig, loadedTempCache);
        return loadedEmbeddedData || loadedUserConfig || loadedTempCache;
    }

    bool Config::LoadUserFile(const std::filesystem::path& path)
    {
        std::ifstream input(path);
        if (!input) {
            SKSE::log::info("Optional user configuration not found: {}", path.string());
            return false;
        }

        return LoadStream(input, path.filename().string(), true);
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

        return LoadStream(input, path.filename().string(), true);
    }

    bool Config::LoadStream(std::istream& input, std::string sourceName, bool userOverride)
    {
        std::unordered_map<std::string, Rule> fileRules;
        enum class Section { kOther, kGeneral, kPlugin };
        Section activeSection = Section::kOther;
        Rule* activeRule = nullptr;
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
                activeSection = Section::kOther;
                activeRule = nullptr;
                constexpr std::string_view prefix = "Plugin:";
                if (Lower(section) == "general") {
                    activeSection = Section::kGeneral;
                } else if (section.size() >= prefix.size() &&
                           Lower(section.substr(0, prefix.size())) == "plugin:") {
                    auto pluginName = Trim(section.substr(prefix.size()));
                    if (!pluginName.empty()) {
                        const auto key = Lower(std::move(pluginName));
                        auto result = fileRules.insert_or_assign(key, Rule{});
                        activeRule = std::addressof(result.first->second);
                        activeSection = Section::kPlugin;
                    }
                }
                continue;
            }

            const auto separator = line.find('=');
            if (separator == std::string::npos) {
                SKSE::log::warn("Ignored malformed line {} in {}",
                    lineNumber, sourceName);
                continue;
            }

            auto key = Lower(Trim(line.substr(0, separator)));
            auto value = Trim(line.substr(separator + 1));

            if (activeSection == Section::kGeneral) {
                if (key == "enabled") {
                    enabled_ = ParseBool(value, enabled_);
                } else if (key == "globalpluginnamefallback") {
                    globalPluginNameFallback_ = ParseBool(value, globalPluginNameFallback_);
                } else if (key == "immersivemode") {
                    immersiveMode_ = ParseBool(value, immersiveMode_);
                }
                continue;
            }

            if (activeSection != Section::kPlugin || !activeRule) {
                continue;
            }

            if (key == "tag") {
                activeRule->tag = NormalizeTag(std::move(value));
            } else if (key == "includeforms") {
                ParseFormList(std::move(value), activeRule->includeForms);
            } else if (key == "excludeforms") {
                ParseFormList(std::move(value), activeRule->excludeForms);
            } else if (key == "modnametags") {
                activeRule->modNameTags = ParseBool(value, true);
            }
        }

        std::size_t loaded = 0;
        std::size_t overridden = 0;
        for (auto& [pluginName, rule] : fileRules) {
            if (rule.tag.empty() && rule.modNameTags) {
                SKSE::log::warn("Ignored rule without Tag for [{}] in {}",
                    pluginName, sourceName);
                if (userOverride && rules_.erase(pluginName) > 0) {
                    ++overridden;
                }
                continue;
            }
            if (rules_.contains(pluginName)) {
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
        if (!SaveTempCache()) {
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
            !UpdateUserConfigSetting(key, value)) {
            SKSE::log::error(
                "Failed to save SKSE Menu Framework setting {} to user configuration: {}",
                key, userConfigPath_.string());
            saved = false;
        }

        return saved;
    }

    bool Config::SaveTempCache() const
    {
        if (tempCachePath_.empty()) {
            return false;
        }

        std::ofstream output(tempCachePath_, std::ios::trunc);
        if (!output) {
            return false;
        }

        output
            << "[General]\n"
            << "Enabled = " << (Enabled() ? "true" : "false") << '\n'
            << "GlobalPluginNameFallback = "
            << (GlobalPluginNameFallback() ? "true" : "false") << '\n'
            << "ImmersiveMode = " << (ImmersiveMode() ? "true" : "false") << '\n';

        return static_cast<bool>(output);
    }

    bool Config::UpdateUserConfigSetting(std::string_view key, std::string_view value) const
    {
        if (userConfigPath_.empty()) {
            return false;
        }

        std::ifstream input(userConfigPath_, std::ios::binary);
        if (!input) {
            return false;
        }

        std::ostringstream buffer;
        buffer << input.rdbuf();
        if (input.bad()) {
            return false;
        }
        const std::string contents = buffer.str();

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
        bool updated = false;
        std::size_t lastGeneralHeader = lines.size();
        std::size_t enabledLine = lines.size();
        std::size_t globalFallbackLine = lines.size();

        for (std::size_t i = 0; i < lines.size(); ++i) {
            auto parsedLine = Trim(lines[i].text);
            if (i == 0 && parsedLine.starts_with("\xEF\xBB\xBF")) {
                parsedLine.erase(0, 3);
            }

            if (!parsedLine.empty() && parsedLine.front() == '[' && parsedLine.back() == ']') {
                const auto sectionName = Lower(Trim(
                    parsedLine.substr(1, parsedLine.size() - 2)));
                inGeneral = sectionName == "general";
                if (inGeneral) {
                    foundGeneral = true;
                    lastGeneralHeader = i;
                    enabledLine = lines.size();
                    globalFallbackLine = lines.size();
                }
                continue;
            }

            if (!inGeneral || parsedLine.empty() ||
                parsedLine.starts_with(';') || parsedLine.starts_with('#')) {
                continue;
            }

            const auto separator = lines[i].text.find('=');
            if (separator == std::string::npos) {
                continue;
            }
            const auto lineKey = Lower(Trim(lines[i].text.substr(0, separator)));
            if (lineKey == "enabled") {
                enabledLine = i;
            } else if (lineKey == "globalpluginnamefallback") {
                globalFallbackLine = i;
            }
            if (lineKey != normalizedKey) {
                continue;
            }

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

            lines[i].text.replace(valueStart, valueEnd - valueStart, value);
            updated = true;
        }

        if (!updated) {
            TextLine settingLine{
                std::string(key) + " = " + std::string(value),
                preferredLineEnding
            };

            if (foundGeneral) {
                std::size_t insertionIndex = lastGeneralHeader + 1;
                if (normalizedKey == "globalpluginnamefallback" &&
                    enabledLine != lines.size()) {
                    insertionIndex = enabledLine + 1;
                } else if (normalizedKey == "immersivemode") {
                    if (globalFallbackLine != lines.size()) {
                        insertionIndex = globalFallbackLine + 1;
                    } else if (enabledLine != lines.size()) {
                        insertionIndex = enabledLine + 1;
                    }
                }

                if (insertionIndex == lines.size()) {
                    if (!lines.empty() && lines.back().ending.empty()) {
                        lines.back().ending = preferredLineEnding;
                    }
                    settingLine.ending = hadFinalNewline ? preferredLineEnding : std::string{};
                    lines.push_back(std::move(settingLine));
                } else {
                    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(insertionIndex),
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
        }

        input.close();
        std::ofstream output(userConfigPath_, std::ios::binary | std::ios::trunc);
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
