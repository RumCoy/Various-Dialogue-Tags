#include "Config.h"

#include <SKSE/SKSE.h>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <fstream>
#include <memory>
#include <sstream>
#include <utility>

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
        const std::filesystem::path& userConfigPath)
    {
        rules_.clear();
        enabled_ = true;
        globalPluginNameFallback_ = false;

        std::istringstream internalDataInput{ std::string(embeddedInternalData) };
        const bool loadedEmbeddedData = embeddedInternalData.empty() ?
            false : LoadStream(internalDataInput, "embedded internal data", false);
        const bool loadedUserConfig = LoadUserFile(userConfigPath);

        SKSE::log::info(
            "Configuration complete: {} dialogue-tag rule(s); enabled={}; "
            "globalPluginNameFallback={}; embeddedData={}; userConfig={}",
            rules_.size(), enabled_, globalPluginNameFallback_, loadedEmbeddedData, loadedUserConfig);
        return loadedEmbeddedData || loadedUserConfig;
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
            }
        }

        std::size_t loaded = 0;
        std::size_t overridden = 0;
        for (auto& [pluginName, rule] : fileRules) {
            if (rule.tag.empty()) {
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

    bool Config::GlobalPluginNameFallback() const noexcept
    {
        return globalPluginNameFallback_;
    }

    const Rule* Config::FindRule(std::string_view pluginName) const
    {
        auto key = Lower(std::string(pluginName));
        const auto found = rules_.find(key);
        return found == rules_.end() ? nullptr : std::addressof(found->second);
    }
}
