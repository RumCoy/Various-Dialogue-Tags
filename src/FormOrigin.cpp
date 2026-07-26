#include "FormOrigin.h"

namespace VariousDialogueTags::FormOrigin
{
    std::optional<Provenance> Resolve(const RE::TESForm* form)
    {
        if (!form) {
            return std::nullopt;
        }

        const auto formID = form->GetFormID();
        const auto highByte = static_cast<std::uint8_t>(formID >> 24);

        if (highByte == 0xFF) {
            return std::nullopt;
        }

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            return std::nullopt;
        }

        const RE::TESFile* originFile = nullptr;
        std::uint32_t localFormID = 0;

        if (highByte == 0xFE) {
            const auto lightIndex = static_cast<std::uint16_t>((formID >> 12) & 0x0FFFu);
            originFile = dataHandler->LookupLoadedLightModByIndex(lightIndex);
            localFormID = formID & 0x00000FFFu;
        } else {
            originFile = dataHandler->LookupLoadedModByIndex(highByte);
            localFormID = formID & 0x00FFFFFFu;
        }

        if (!originFile) {
            return std::nullopt;
        }

        const auto* winningFile = form->GetFile();
        if (!winningFile) {
            winningFile = originFile;
        }

        return Provenance{
            .origin = Identity{
                .file = originFile,
                .filename = std::string{ originFile->GetFilename() },
                .localFormID = localFormID
            },
            .winner = Identity{
                .file = winningFile,
                .filename = std::string{ winningFile->GetFilename() },
                .localFormID = localFormID
            }
        };
    }
}
