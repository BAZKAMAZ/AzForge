#include "src/Api/HudApi.h"

#include "src/Api/ApiProperty.h"
#include "src/Api/ApiRead.h"
#include "src/Api/ModContext.h"
#include "src/Game/GameAddresses.h"
#include "src/Core/ForgeLog.h"
#include "src/Game/GameModule.h"
#include "src/Game/GameSymbols.h"

#include <stddef.h>
#include <string.h>
#include <vector>

namespace Forge
{
    namespace
    {
        using namespace ApiRead;
        using ApiProperty::Field;
        using ApiProperty::Kind;

        AmodHudApi g_table = {};

        uint8_t g_asParsed[GameAddress::kHudSize] = {};
        ModContext* g_owner = nullptr;

        uint8_t* Hud()
        {
            uint8_t* const hud = static_cast<uint8_t*>(GameModule::Resolve(GameAddress::kHud));
            return (hud != nullptr && Readable(hud, GameAddress::kHudSize)) ? hud : nullptr;
        }

        constexpr Field kHudFields[] =
        {
            { "items.start_pos",     GameAddress::kHudItemStartOffset,       Kind::Floats, 2, 0, 0, true },
            { "items.offs",          GameAddress::kHudItemOffsOffset,        Kind::Floats, 2, 0, 0, true },
            { "items.size",          GameAddress::kHudItemSizeOffset,        Kind::Floats, 2, 0, 0, true },

            { "item_frame.texture",  GameAddress::kHudItemFrameOffset,       Kind::Resource, 0, 0, GameAddress::kResolveTextureSlot, true },
            { "item_frame.size",     GameAddress::kHudItemFrameSizeOffset,   Kind::Floats, 2, 0, 0, true },

            { "points_pos",          GameAddress::kHudPointsPosOffset,       Kind::Floats, 2, 0, 0, true },
            { "pbar_offs",           GameAddress::kHudPbarOffsOffset,        Kind::Floats, 2, 0, 0, true },
            { "bonus_pulse_scale",   GameAddress::kHudBonusPulseScaleOffset, Kind::Float,  0, 0, 0, true },
            { "life_pulse_scale",    GameAddress::kHudLifePulseScaleOffset,  Kind::Float,  0, 0, 0, true },
            { "life_pulse_time",     GameAddress::kHudLifePulseTimeOffset,   Kind::Float,  0, 0, 0, true },
        };

        constexpr size_t kFieldCount = sizeof(kHudFields) / sizeof(Field);

        int __cdecl ApiPropertyCount(AmodMod mod)
        {
            return Caller(mod) != nullptr ? static_cast<int>(kFieldCount) : 0;
        }

        const char* __cdecl ApiPropertyNameAt(AmodMod mod, int index)
        {
            return (Caller(mod) != nullptr && index >= 0 && index < static_cast<int>(kFieldCount))
                       ? kHudFields[index].key
                       : "";
        }

        int __cdecl ApiHasProperty(AmodMod mod, const char* key)
        {
            return (Caller(mod) != nullptr && Hud() != nullptr &&
                    ApiProperty::Find(kHudFields, kFieldCount, key) != nullptr)
                       ? 1
                       : 0;
        }

        int __cdecl ApiGetProperty(AmodMod mod, const char* key, AmodValue* value)
        {
            const uint8_t* const hud = Caller(mod) != nullptr ? Hud() : nullptr;
            const Field* const field = ApiProperty::Find(kHudFields, kFieldCount, key);

            return (hud != nullptr && field != nullptr) ? ApiProperty::Get(hud, *field, value) : 0;
        }

        int __cdecl ApiSetProperty(AmodMod mod, const char* key, const AmodValue* value)
        {
            ModContext* const caller = Caller(mod);
            uint8_t* const hud = caller != nullptr ? Hud() : nullptr;
            const Field* const field = ApiProperty::Find(kHudFields, kFieldCount, key);

            if (hud == nullptr || field == nullptr || value == nullptr)
            {
                return 0;
            }

            // Taken before anything is changed, so there is something to go back to.
            if (g_owner == nullptr)
            {
                memcpy(g_asParsed, hud, sizeof(g_asParsed));
                g_owner = caller;
            }

            return ApiProperty::Set(hud, *field, *value);
        }

        // --- images of a mod's own ------------------------------------------------------------
        //
        // Built by the game's own parser out of a fragment in game_info.txt syntax, and appended
        // to the collection the HUD already draws. Nothing here draws anything: the game does,
        // exactly as it does for the images its script named.

        typedef void* (__cdecl* ParserCreateRoutine)(char* text, uint8_t keyPrefix);
        typedef void (__cdecl* ParserDestroyRoutine)(void* parser);
        typedef int (__cdecl* ParserEnterBlockRoutine)(void* parser, const char* name);
        typedef int (__cdecl* ParserNextBlockRoutine)(void* parser);
        typedef void (__cdecl* ParserLeaveBlockRoutine)(void* parser);
        typedef void (__cdecl* ImageParseRoutine)(void* parser, void* record);
        typedef uint8_t* (__stdcall* ImagesAppendRoutine)(uint8_t** array, int* count);

        // These answer in AL and leave the rest of EAX as it was.
        bool Succeeded(int result)
        {
            return (result & 0xFF) != 0;
        }

        uint8_t* g_images = nullptr;
        int g_imagesBefore = 0;
        std::vector<uint8_t> g_imagesAsParsed;

        uint8_t** ImagesArray(uint8_t* collection)
        {
            return reinterpret_cast<uint8_t**>(collection + GameAddress::kImagesArrayOffset);
        }

        int* ImagesCount(uint8_t* collection)
        {
            return reinterpret_cast<int*>(collection + GameAddress::kImagesCountOffset);
        }

        // Taken once, before the first change of any kind.
        void RememberImages(uint8_t* collection, ModContext* caller, const uint8_t* hud)
        {
            if (g_images != nullptr)
            {
                return;
            }

            g_images = collection;
            g_imagesBefore = *ImagesCount(collection);

            const uint8_t* const records = *ImagesArray(collection);
            const size_t bytes = static_cast<size_t>(g_imagesBefore) * GameAddress::kImageRecordSize;

            if (records != nullptr && bytes != 0 && Readable(records, bytes))
            {
                g_imagesAsParsed.assign(records, records + bytes);
            }

            if (g_owner == nullptr)
            {
                memcpy(g_asParsed, hud, sizeof(g_asParsed));
                g_owner = caller;
            }
        }

        uint8_t* HudImages(AmodMod mod)
        {
            uint8_t* const hud = Caller(mod) != nullptr ? Hud() : nullptr;
            uint8_t* const collection = hud != nullptr
                                            ? Read<uint8_t*>(hud, GameAddress::kHudImagesOffset)
                                            : nullptr;

            return (collection != nullptr && Readable(collection, 8)) ? collection : nullptr;
        }

        int __cdecl ApiImageCount(AmodMod mod)
        {
            uint8_t* const collection = HudImages(mod);
            return collection != nullptr ? *ImagesCount(collection) : 0;
        }

        int __cdecl ApiRemoveImage(AmodMod mod, int index)
        {
            uint8_t* const collection = HudImages(mod);
            uint8_t* const hud = collection != nullptr ? Hud() : nullptr;
            if (collection == nullptr || hud == nullptr)
            {
                return 0;
            }

            int* const count = ImagesCount(collection);
            uint8_t* const records = *ImagesArray(collection);

            if (records == nullptr || index < 0 || index >= *count)
            {
                return 0;
            }

            RememberImages(collection, Caller(mod), hud);

            const size_t stride = GameAddress::kImageRecordSize;
            const size_t behind = static_cast<size_t>(*count - index - 1) * stride;

            if (behind != 0)
            {
                memmove(records + static_cast<size_t>(index) * stride,
                        records + static_cast<size_t>(index + 1) * stride, behind);
            }

            *count -= 1;
            return 1;
        }

        int __cdecl ApiAddImages(AmodMod mod, const char* fragment)
        {
            ModContext* const caller = Caller(mod);
            uint8_t* const hud = caller != nullptr ? Hud() : nullptr;
            if (hud == nullptr || fragment == nullptr || fragment[0] == '\0')
            {
                return 0;
            }

            uint8_t* const collection = Read<uint8_t*>(hud, GameAddress::kHudImagesOffset);
            if (collection == nullptr || !Readable(collection, 8))
            {
                FORGE_LOG_ERROR("The HUD has no images to add to yet");
                return 0;
            }

            const ParserCreateRoutine create =
                reinterpret_cast<ParserCreateRoutine>(GameSymbols::Address(GameSymbol::ParserCreateFromBuffer));
            const ParserDestroyRoutine destroy =
                reinterpret_cast<ParserDestroyRoutine>(GameSymbols::Address(GameSymbol::ParserDestroy));
            const ParserEnterBlockRoutine enter =
                reinterpret_cast<ParserEnterBlockRoutine>(GameSymbols::Address(GameSymbol::ParserEnterBlock));
            const ParserNextBlockRoutine next =
                reinterpret_cast<ParserNextBlockRoutine>(GameSymbols::Address(GameSymbol::ParserNextBlock));
            const ParserLeaveBlockRoutine leave =
                reinterpret_cast<ParserLeaveBlockRoutine>(GameSymbols::Address(GameSymbol::ParserLeaveBlock));

            const ImageParseRoutine parseImage =
                reinterpret_cast<ImageParseRoutine>(GameModule::Resolve(GameAddress::kImageParseRecord));
            const ImagesAppendRoutine append =
                reinterpret_cast<ImagesAppendRoutine>(GameModule::Resolve(GameAddress::kImagesAppend));

            if (create == nullptr || destroy == nullptr || enter == nullptr || next == nullptr ||
                leave == nullptr || parseImage == nullptr || append == nullptr)
            {
                return 0;
            }

            // The parser keeps the pointer and strips comments in place, so the text has to be
            // writable and outlive it - a mod's literal is neither.
            std::vector<char> text(fragment, fragment + strlen(fragment) + 1);

            void* const parser = create(text.data(), GameAddress::kParserKeyPrefix);
            if (parser == nullptr)
            {
                return 0;
            }

            int added = 0;
            if (Succeeded(enter(parser, "images")))
            {
                if (Succeeded(enter(parser, "image")))
                {
                    RememberImages(collection, caller, hud);

                    uint8_t** const array = ImagesArray(collection);
                    int* const count = ImagesCount(collection);

                    do
                    {
                        uint8_t record[GameAddress::kImageRecordSize] = {};
                        parseImage(parser, record);

                        uint8_t* const slot = append(array, count);
                        if (slot != nullptr)
                        {
                            memcpy(slot, record, sizeof(record));
                            ++added;
                        }
                    }
                    while (Succeeded(next(parser)));

                    leave(parser);
                }
                leave(parser);
            }

            destroy(parser);
            return added;
        }

        void TakeImagesBack()
        {
            if (g_images == nullptr || !Readable(g_images, 8))
            {
                g_images = nullptr;
                g_imagesBefore = 0;
                g_imagesAsParsed.clear();
                return;
            }

            uint8_t** const array = ImagesArray(g_images);
            int* const count = ImagesCount(g_images);

            const ImagesAppendRoutine append =
                reinterpret_cast<ImagesAppendRoutine>(GameModule::Resolve(GameAddress::kImagesAppend));

            while (*count < g_imagesBefore && append != nullptr && append(array, count) != nullptr)
            {
            }

            // The count is what the HUD walks, so it may never outrun the array.
            const int restored = *count < g_imagesBefore ? *count : g_imagesBefore;
            const size_t bytes = static_cast<size_t>(restored) * GameAddress::kImageRecordSize;

            if (*array != nullptr && bytes != 0 && bytes <= g_imagesAsParsed.size())
            {
                memcpy(*array, g_imagesAsParsed.data(), bytes);
            }
            *count = restored;

            g_images = nullptr;
            g_imagesBefore = 0;
            g_imagesAsParsed.clear();
        }
    }

    bool HudApi::Install()
    {
        g_table.structSize = sizeof(AmodHudApi);
        g_table.GetProperty = &ApiGetProperty;
        g_table.SetProperty = &ApiSetProperty;
        g_table.HasProperty = &ApiHasProperty;
        g_table.PropertyCount = &ApiPropertyCount;
        g_table.PropertyNameAt = &ApiPropertyNameAt;
        g_table.AddImages = &ApiAddImages;
        g_table.RemoveImage = &ApiRemoveImage;
        g_table.ImageCount = &ApiImageCount;

        return true;
    }

    void HudApi::ForgetMod(ModContext* owner)
    {
        uint8_t* const hud = (owner != nullptr && owner == g_owner) ? Hud() : nullptr;
        if (hud == nullptr)
        {
            return;
        }

        TakeImagesBack();

        for (const Field& field : kHudFields)
        {
            ApiProperty::Restore(hud, field, g_asParsed);
        }
        g_owner = nullptr;
    }

    void HudApi::Uninstall()
    {
        ForgetMod(g_owner);
        g_table = AmodHudApi{};
    }

    const AmodHudApi* HudApi::Table()
    {
        return g_table.structSize != 0 ? &g_table : nullptr;
    }
}
