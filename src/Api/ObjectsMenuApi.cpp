#include <windows.h>

#include "src/Api/ObjectsMenuApi.h"
#include "src/Api/ModContext.h"
#include "src/Core/ForgeLog.h"
#include "src/Game/GameAddresses.h"
#include "src/Game/GameModule.h"
#include "src/Game/GameSymbols.h"
#include "src/Hook/InlineHook.h"
#include "src/Core/ForgeCrash.h"

#include <map>
#include <set>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <algorithm>

namespace Forge
{
    namespace
    {
        typedef int(__fastcall* RegisterRoutine)(void* manager, void* unused, void* menu, const char* scriptFile, const char* menuId, int force);
        typedef int(__stdcall* LoadFromScriptRoutine)(const char* menuId, const char* scriptFile);
        typedef int(__fastcall* ParseScriptRoutine)(void* menu, void* unused, const char* scriptFile);
        typedef void*(__fastcall* FindItemRoutine)(void* menu, void* unused, const char* itemId);
        typedef void*(__fastcall* FindMenuRoutine)(void* manager, void* unused, const char* menuId);
        typedef void(__fastcall* TakeClickResultRoutine)(void* menu, void* unused, void* dest);
        typedef void(__fastcall* MenuClickRoutine)(void* menu, void* unused, float x, float y);
        typedef void* (__fastcall* HoveredInGroupRoutine)(void* menu, void* unused, int group);

        struct Subscription
        {
            uint32_t id;
            ModContext* owner;
            AmodMenuEvent event;
            AmodMenuCallback callback;
            void* user;
        };

        InlineHook g_registerHook;
        InlineHook g_loadHook;
        InlineHook g_parseHook;
        InlineHook g_clickHook;
        InlineHook g_newGameHook;

        MenuClickRoutine g_originalNewGameClick = nullptr;
        MenuClickRoutine g_menuBaseClick = nullptr;
        HoveredInGroupRoutine g_hovered = nullptr;

        RegisterRoutine g_originalRegister = nullptr;
        LoadFromScriptRoutine g_originalLoad = nullptr;
        ParseScriptRoutine g_originalParse = nullptr;
        TakeClickResultRoutine g_originalTakeClick = nullptr;
        FindItemRoutine g_findItem = nullptr;
        FindMenuRoutine g_findMenu = nullptr;

        // A table's own two, both methods on it: one writes a cell, one raises the dots of a line.
        typedef void(__fastcall* TableSetCellRoutine)(void* table, void* unused, int column, int line, const char* text);
        typedef void(__fastcall* TableSetDotsRoutine)(void* table, void* unused, int line, int dots);

        typedef void(__fastcall* TableSetStyleRoutine)(void* table, void* unused, int line, const float* colour, const float* scale);

        TableSetCellRoutine g_tableSetCell = nullptr;
        TableSetDotsRoutine g_tableSetDots = nullptr;
        TableSetStyleRoutine g_tableSetStyle = nullptr;

        std::map<std::string, void*> g_menusById;
        std::map<void*, std::vector<void*>> g_elementsByMenu;
        std::set<void*> g_validElements;

        void* g_parsingMenu = nullptr;
        void* g_lastParsedMenu = nullptr;

        std::vector<Subscription> g_subscriptions;
        uint32_t g_nextSubscriptionId = 1;

        // A menu asked for inside a click goes into the click's own result; Menu_Update opens it once the click is done.
        bool g_inClick = false;
        bool g_askedBack = false;
        const char* g_askedSub = nullptr;
        const char* g_askedBox = nullptr;

        // Not ID_BACK, not ID_CLOSE_MENU: with this as the item the game only walks to what was asked.
        const char kNoItem[] = "";
        const char kBackItem[] = "ID_BACK";

        bool Succeeded(int result)
        {
            return (result & 0xFF) != 0;
        }

        ModContext* Resolve(AmodMod mod)
        {
            ModContext* context = static_cast<ModContext*>(mod);
            return (context != nullptr && context->magic == kModContextMagic) ? context : nullptr;
        }

        uint8_t* Field(void* item, uintptr_t offset)
        {
            return static_cast<uint8_t*>(item) + offset;
        }

        float* Floats(void* item, uintptr_t offset)
        {
            return reinterpret_cast<float*>(Field(item, offset));
        }

        bool IsLive(AmodMenuItem item)
        {
            return item != nullptr && g_validElements.find(item) != g_validElements.end();
        }

        enum class ElementKind
        {
            Unknown,
            Item,
            Logo,
            TextButton,
            ImageButton,
            HotkeyButton,
            CastleButton,
            List,
            Slider,
            Table,
            Image,
            AnimImage,
            StaticText
        };

        // Kinds that are not individual allocations, and so cannot be recognised from an
        // allocator header, are recorded here as the menu is walked.
        std::map<void*, ElementKind> g_kindByElement;

        ElementKind KindOf(AmodMenuItem item)
        {
            const auto recorded = g_kindByElement.find(item);
            if (recorded != g_kindByElement.end())
            {
                return recorded->second;
            }

            const uint8_t* const header =
                static_cast<const uint8_t*>(item) - GameAddress::kTrackedHeaderSize;

            if (*reinterpret_cast<const uint16_t*>(header + GameAddress::kTrackedMagicOffset) !=
                GameAddress::kTrackedMagic)
            {
                return ElementKind::Unknown;
            }

            switch (*reinterpret_cast<const uint32_t*>(header + GameAddress::kTrackedSizeOffset))
            {
            case GameAddress::kSizeMenuItem:     return ElementKind::Item;
            case GameAddress::kSizeMenuLogo:     return ElementKind::Logo;
            case GameAddress::kSizeTextButton:   return ElementKind::TextButton;
            case GameAddress::kSizeImageButton:  return ElementKind::ImageButton;
            case GameAddress::kSizeHotkeyButton: return ElementKind::HotkeyButton;
            case GameAddress::kSizeCastleButton: return ElementKind::CastleButton;
            case GameAddress::kSizeMenuList:     return ElementKind::List;
            case GameAddress::kSizeMenuSlider:   return ElementKind::Slider;
            case GameAddress::kSizeMenuTable:    return ElementKind::Table;
            default:                             return ElementKind::Unknown;
            }
        }

        const char* KindName(ElementKind kind)
        {
            switch (kind)
            {
            case ElementKind::Item:         return "item";
            case ElementKind::Logo:         return "logo";
            case ElementKind::TextButton:   return "text_button";
            case ElementKind::ImageButton:  return "img_button";
            case ElementKind::HotkeyButton: return "hotkey_button";
            case ElementKind::CastleButton: return "castle_button";
            case ElementKind::List:         return "list";
            case ElementKind::Slider:       return "slider";
            case ElementKind::Table:        return "table";
            case ElementKind::Image:        return "image";
            case ElementKind::AnimImage:    return "image_anim";
            case ElementKind::StaticText:   return "static";
            case ElementKind::Unknown:      break;
            }
            return "";
        }

        void RecomputeCentre(void* item)
        {
            const float* const rect = Floats(item, GameAddress::kMenuElementRectOffset);
            float* const centre = Floats(item, GameAddress::kMenuElementCentreOffset);

            centre[0] = rect[0] + (rect[2] - rect[0]) * 0.5f;
            centre[1] = rect[1] + (rect[3] - rect[1]) * 0.5f;
        }

        void ApplyGeometry(void* item, float width, float height)
        {
            const float* const pos = Floats(item, GameAddress::kMenuElementPosOffset);
            float* const rect = Floats(item, GameAddress::kMenuElementRectOffset);
            const uint32_t align = *reinterpret_cast<const uint32_t*>(
                Field(item, GameAddress::kMenuElementAlignOffset));

            if ((align & 0x04) != 0)            // horizontally centred
            {
                rect[0] = pos[0] - width * 0.5f;
                rect[2] = pos[0] + width * 0.5f;
            }
            else if ((align & 0x02) != 0)       // right edge at the position
            {
                rect[0] = pos[0] - width;
                rect[2] = pos[0];
            }
            else                                // left edge at the position
            {
                rect[0] = pos[0];
                rect[2] = pos[0] + width;
            }

            if ((align & 0x20) != 0)            // vertically centred
            {
                rect[1] = pos[1] - height * 0.5f;
                rect[3] = pos[1] + height * 0.5f;
            }
            else if ((align & 0x08) != 0)       // top edge at the position
            {
                rect[1] = pos[1] - height;
                rect[3] = pos[1];
            }
            else                                // bottom edge at the position
            {
                rect[1] = pos[1];
                rect[3] = pos[1] + height;
            }

            RecomputeCentre(item);
        }

        void CurrentSize(void* item, float& width, float& height)
        {
            const float* const rect = Floats(item, GameAddress::kMenuElementRectOffset);
            width = rect[2] - rect[0];
            height = rect[3] - rect[1];
        }

        // --- the property table --------------------------------------------------------

        enum class Store
        {
            Float,
            String,
            Bool,
            Int
        };

        constexpr uint32_t KindBit(ElementKind kind)
        {
            return 1u << static_cast<uint32_t>(kind);
        }

        constexpr uint32_t kAnyKind = 0xFFFFFFFFu;
        constexpr uint32_t kItem = KindBit(ElementKind::Item);
        constexpr uint32_t kLogo = KindBit(ElementKind::Logo);
        constexpr uint32_t kTextBtn = KindBit(ElementKind::TextButton);
        constexpr uint32_t kHotkeyBtn = KindBit(ElementKind::HotkeyButton);
        constexpr uint32_t kImgBtn = KindBit(ElementKind::ImageButton);
        constexpr uint32_t kCastleBtn = KindBit(ElementKind::CastleButton);
        constexpr uint32_t kList = KindBit(ElementKind::List);
        constexpr uint32_t kSlider = KindBit(ElementKind::Slider);
        constexpr uint32_t kTable = KindBit(ElementKind::Table);
        constexpr uint32_t kImage = KindBit(ElementKind::Image);
        constexpr uint32_t kAnimImage = KindBit(ElementKind::AnimImage);
        constexpr uint32_t kStaticText = KindBit(ElementKind::StaticText);

        constexpr uint32_t kElementKinds =
            kItem | kLogo | kTextBtn | kImgBtn | kHotkeyBtn | kCastleBtn | kList | kSlider;

        constexpr uint32_t kAlignedKinds = kElementKinds & ~kLogo;

        struct PropertyDescriptor
        {
            const char* key;
            uintptr_t offset;
            Store store;
            uint32_t count;     // how many floats, for Store::Float
            size_t capacity;    // bytes available, for Store::String
            uint32_t kinds;     // which element kinds carry it
            bool writable;
            bool (*read)(void* item, AmodValue& value);              // null: plain load
            bool (*write)(void* item, const AmodValue& value);       // null: plain store

            uintptr_t arrayOffset;
            uintptr_t countOffset;
            size_t stride;      // zero on every key that is not a collection
        };

        bool SplitKey(const char* key, char* base, size_t baseSize, int& index)
        {
            index = -1;

            if (key == nullptr)
            {
                return false;
            }

            const char* const bracket = strchr(key, '[');
            if (bracket == nullptr)
            {
                return strncpy_s(base, baseSize, key, _TRUNCATE) == 0;
            }

            const size_t length = static_cast<size_t>(bracket - key);
            if (length == 0 || length + 1 >= baseSize)
            {
                return false;
            }

            memcpy(base, key, length);
            base[length] = '\0';

            char* end = nullptr;
            const long parsed = strtol(bracket + 1, &end, 10);
            if (end != bracket + 1 && *end == ']' && end[1] == '\0' && parsed >= 0)
            {
                index = static_cast<int>(parsed);
                return true;
            }

            return false;
        }

        bool ReadKind(void* item, AmodValue& value)
        {
            value.kind = AMOD_VALUE_STRING;
            value.text = KindName(KindOf(item));
            return true;
        }

        bool ReadSize(void* item, AmodValue& value)
        {
            float width = 0.0f;
            float height = 0.0f;
            CurrentSize(item, width, height);

            value.kind = AMOD_VALUE_FLOAT;
            value.count = 2;
            value.numbers[0] = width;
            value.numbers[1] = height;
            return true;
        }

        bool WriteSize(void* item, const AmodValue& value)
        {
            ApplyGeometry(item, value.numbers[0], value.numbers[1]);
            return true;
        }

        bool WritePosition(void* item, const AmodValue& value)
        {
            float width = 0.0f;
            float height = 0.0f;
            CurrentSize(item, width, height);

            float* const pos = Floats(item, GameAddress::kMenuElementPosOffset);
            pos[0] = value.numbers[0];
            pos[1] = value.numbers[1];

            ApplyGeometry(item, width, height);
            return true;
        }

        bool WriteAlign(void* item, const AmodValue& value)
        {
            float width = 0.0f;
            float height = 0.0f;
            CurrentSize(item, width, height);

            *reinterpret_cast<int*>(Field(item, GameAddress::kMenuElementAlignOffset)) = value.integer;

            ApplyGeometry(item, width, height);
            return true;
        }

        bool WriteRect(void* item, const AmodValue& value)
        {
            float* const rect = Floats(item, GameAddress::kMenuElementRectOffset);
            for (int i = 0; i < 4; ++i)
            {
                rect[i] = value.numbers[i];
            }

            RecomputeCentre(item);
            return true;
        }

        // --- logo ----------------------------------------------------------------------
        // Reproduces the tail of MenuLogo_Parse @0x0045f2d0. A logo is the one kind that
        // ignores $align: MenuButton_ParseCommon lays it out like everything else and the
        // logo parser then throws that away and builds the edges from $pos and $size, with
        // a second copy of them right behind.
        void LogoApplyGeometry(void* item)
        {
            const float* const pos = Floats(item, GameAddress::kMenuElementPosOffset);
            const float* const size = Floats(item, GameAddress::kLogoSizeOffset);
            float* const rect = Floats(item, GameAddress::kMenuElementRectOffset);

            rect[0] = pos[0];
            rect[1] = pos[1] - size[1];
            rect[2] = pos[0] + size[0];
            rect[3] = pos[1];

            float* const copy = Floats(item, GameAddress::kMenuElementCaptionRectOffset);
            for (int i = 0; i < 4; ++i)
            {
                copy[i] = rect[i];
            }

            RecomputeCentre(item);
        }

        bool WriteLogoPosition(void* item, const AmodValue& value)
        {
            float* const pos = Floats(item, GameAddress::kMenuElementPosOffset);
            pos[0] = value.numbers[0];
            pos[1] = value.numbers[1];

            LogoApplyGeometry(item);
            return true;
        }

        bool WriteLogoSize(void* item, const AmodValue& value)
        {
            float* const size = Floats(item, GameAddress::kLogoSizeOffset);
            size[0] = value.numbers[0];
            size[1] = value.numbers[1];

            LogoApplyGeometry(item);
            return true;
        }

        constexpr PropertyDescriptor g_properties[] =
        {
            // key                  offset store          n  cap    kinds                          writable read        write
            { "kind",               0,     Store::String, 0, 0,     kAnyKind,     false,   &ReadKind,  nullptr },

            { "id",                 0x715, Store::String, 0, 0x43,  kElementKinds,                 true,    nullptr,    nullptr },
            { "sub_menu",           0x014, Store::String, 0, 0x40,  kElementKinds,                 true,    nullptr,    nullptr },
            { "box_menu",           0x054, Store::String, 0, 0x40,  kElementKinds,                 true,    nullptr,    nullptr },
            { "reg_menu",           0x094, Store::String, 0, 0x100, kElementKinds,                 true,    nullptr,    nullptr },
            { "group",              0x194, Store::String, 0, 0x100, kElementKinds,                 true,    nullptr,    nullptr },
            { "help",               0x2B5, Store::String, 0, 0x100, kElementKinds,                 true,    nullptr,    nullptr },
            { "warning",            0x3B5, Store::String, 0, 0x100, kElementKinds,                 true,    nullptr,    nullptr },
            { "lock",               0x4B5, Store::String, 0, 0x100, kElementKinds,                 true,    nullptr,    nullptr },
            { "unreg_enabled",      0x5B5, Store::Bool,   0, 0,     kElementKinds,true,    nullptr,    nullptr },
            { "pos",                0x5B8, Store::Float,  2, 0,     kAlignedKinds,true,    nullptr,    &WritePosition },
            { "rect",               0x294, Store::Float,  4, 0,     kElementKinds,true,    nullptr,    &WriteRect },
            { "size",               0,     Store::Float,  2, 0,     kAlignedKinds,true,    &ReadSize,  &WriteSize },
            { "rect_size",          0,     Store::Float,  2, 0,     kAlignedKinds,true,    &ReadSize,  &WriteSize },
            { "centre",             0x5C0, Store::Float,  2, 0,     kElementKinds,false,   nullptr,    nullptr },
            { "align",              0x5C8, Store::Int,    0, 0,     kAlignedKinds,true,    nullptr,    &WriteAlign },
            { "font_id",            0x5F8, Store::Int,    0, 0,     kElementKinds,false,   nullptr,    nullptr },
            { "caption.text",       0x5FF, Store::String, 0, 0x101, kElementKinds,                 true,    nullptr,    nullptr },
            { "caption.offs",       0x700, Store::Float,  2, 0,     kElementKinds,true,    nullptr,    nullptr },
            { "caption.rect_len",   0x708, Store::Float,  1, 0,     kElementKinds,true,    nullptr,    nullptr },
            { "caption.rect",       0x2A4, Store::Float,  4, 0,     kElementKinds,true,    nullptr,    nullptr },
            { "caption.enabled",    0x2B4, Store::Bool,   0, 0,     kElementKinds,true,    nullptr,    nullptr },
            { "visible",            0x714, Store::Bool,   0, 0,     kElementKinds,true,    nullptr,    nullptr },

            { "text",               0x758, Store::String, 0, 0x40,  kItem | kTextBtn | kHotkeyBtn, true,    nullptr,    nullptr },
            { "plank_width",        0x798, Store::Int,    0, 0,     kItem,                         true,    nullptr,    nullptr },
            { "cursor3d",           0x79C, Store::Bool,   0, 0,     kItem,                         true,    nullptr,    nullptr },
            { "cursor3d.orient",    0x7A4, Store::Float,  3, 0,     kItem,                         true,    nullptr,    nullptr },
            { "cursor3d.pos",       0x7B0, Store::Float,  3, 0,     kItem,                         true,    nullptr,    nullptr },

            // ImageButton_Parse @0x0045e700, which the castle button inherits.
            { "img_enabled",        0x758, Store::Int,    0, 0,     kImgBtn | kCastleBtn,          true,    nullptr,    nullptr },
            { "img_disabled",       0x75C, Store::Int,    0, 0,     kImgBtn | kCastleBtn,          true,    nullptr,    nullptr },
            { "horz_flip",          0x760, Store::Bool,   0, 0,     kImgBtn | kCastleBtn,          true,    nullptr,    nullptr },

            { "sp1",                0x758, Store::Int,    0, 0,     kLogo,                         true,    nullptr,    nullptr },
            { "sp2",                0x75C, Store::Int,    0, 0,     kLogo,                         true,    nullptr,    nullptr },
            { "sprite",             0x760, Store::Int,    0, 0,     kLogo,                         true,    nullptr,    nullptr },
            { "two_sprites",        0x764, Store::Bool,   0, 0,     kLogo,                         true,    nullptr,    nullptr },
            { "swap_on_focus",      0x765, Store::Bool,   0, 0,     kLogo,                         true,    nullptr,    nullptr },
            { "pos",                0x5B8, Store::Float,  2, 0,     kLogo,                         true,    nullptr,    &WriteLogoPosition },
            { "size",               0x768, Store::Float,  2, 0,     kLogo,                         true,    nullptr,    &WriteLogoSize },
            { "align",              0x5C8, Store::Int,    0, 0,     kLogo,                         true,    nullptr,    nullptr },

            { "items",              0x00,  Store::String, 0, 0x40,  kList,                         true,    nullptr,    nullptr,    0x758, 0x75C, 0x48 },
            // How many entries fit across, which the parser works out from the width.
            { "columns",            0x764, Store::Int,    0, 0,     kList,                         true,    nullptr,    nullptr },
            { "left.rect",          0x7B4, Store::Float,  4, 0,     kList,                         true,    nullptr,    nullptr },
            { "left.key",           0x7C8, Store::Int,    0, 0,     kList,                         true,    nullptr,    nullptr },
            { "text.rect",          0x820, Store::Float,  4, 0,     kList,                         true,    nullptr,    nullptr },
            { "text.key",           0x834, Store::Int,    0, 0,     kList,                         true,    nullptr,    nullptr },
            { "right.rect",         0x88C, Store::Float,  4, 0,     kList,                         true,    nullptr,    nullptr },
            { "right.key",          0x8A0, Store::Int,    0, 0,     kList,                         true,    nullptr,    nullptr },

            // HotkeyButton_Parse @0x0045e410.
            { "help_key",           0xAB8, Store::String, 0, 0x100, kHotkeyBtn,                    true,    nullptr,    nullptr },

            // MenuSlider_Parse @0x0045b4b0
            { "position",           0x764, Store::Float,  1, 0,     kSlider,                       true,    nullptr,    nullptr },
            { "margin",             0x75C, Store::Float,  2, 0,     kSlider,                       true,    nullptr,    nullptr },
            { "range",              0x76C, Store::Float,  2, 0,     kSlider,                       true,    nullptr,    nullptr },

            // CastleButton_Parse @0x0045cf60.
            { "start_level",        0x7DC, Store::String, 0, 0x100, kCastleBtn,                    true,    nullptr,    nullptr },
            { "demo_level",         0x8DC, Store::String, 0, 0x100, kCastleBtn,                    true,    nullptr,    nullptr },
            { "unable_menu",        0x9DC, Store::String, 0, 0x100, kCastleBtn,                    true,    nullptr,    nullptr },
            { "door_offs",          0xB54, Store::Float,  2, 0,     kCastleBtn,                    true,    nullptr,    nullptr },
            { "door_size",          0xB5C, Store::Float,  2, 0,     kCastleBtn,                    true,    nullptr,    nullptr },
            { "part_count",         0xB64, Store::Int,    0, 0,     kCastleBtn,                    true,    nullptr,    nullptr },
            { "part_radius",        0xB68, Store::Float,  2, 0,     kCastleBtn,                    true,    nullptr,    nullptr },
            { "part_disp",          0xB70, Store::Float,  1, 0,     kCastleBtn,                    true,    nullptr,    nullptr },
            { "active_door_scale",  0xB7C, Store::Float,  2, 0,     kCastleBtn,                    true,    nullptr,    nullptr },
            { "inactive_door_scale",0xB84, Store::Float,  2, 0,     kCastleBtn,                    true,    nullptr,    nullptr },
            { "blocked_offs",       0xB8C, Store::Float,  2, 0,     kCastleBtn,                    true,    nullptr,    nullptr },
            { "img_blocked",        0xB94, Store::Int,    0, 0,     kCastleBtn,                    true,    nullptr,    nullptr },
            { "img_size",           0xB98, Store::Float,  2, 0,     kCastleBtn,                    true,    nullptr,    nullptr },
            { "help_demo",          0xBA1, Store::String, 0, 0x100, kCastleBtn,                    true,    nullptr,    nullptr },
            { "start_play",         0xCA1, Store::String, 0, 0x100, kCastleBtn,                    true,    nullptr,    nullptr },
            { "start_demo",         0xDA1, Store::String, 0, 0x100, kCastleBtn,                    true,    nullptr,    nullptr },

            { "anim.color_passive",    0x764, Store::Float, 4, 0, kImgBtn | kCastleBtn,         true,    nullptr,    nullptr },
            { "anim.color_active",     0x774, Store::Float, 4, 0, kImgBtn | kCastleBtn,         true,    nullptr,    nullptr },
            { "anim.color_disabled",   0x784, Store::Float, 4, 0, kImgBtn | kCastleBtn,         true,    nullptr,    nullptr },
            { "anim.color_blocked",    0x794, Store::Float, 4, 0, kImgBtn | kCastleBtn,         true,    nullptr,    nullptr },
            { "anim.color_nof_active", 0x7A4, Store::Float, 4, 0, kImgBtn | kCastleBtn,         true,    nullptr,    nullptr },
            { "anim.scale_passive",    0x7B4, Store::Float, 2, 0, kImgBtn | kCastleBtn,         true,    nullptr,    nullptr },
            { "anim.scale_active",     0x7BC, Store::Float, 2, 0, kImgBtn | kCastleBtn,         true,    nullptr,    nullptr },
            { "anim.scale_disabled",   0x7C4, Store::Float, 2, 0, kImgBtn | kCastleBtn,         true,    nullptr,    nullptr },
            { "anim.scale_blocked",    0x7CC, Store::Float, 2, 0, kImgBtn | kCastleBtn,         true,    nullptr,    nullptr },
            { "anim.scale_nof_active", 0x7D4, Store::Float, 2, 0, kImgBtn | kCastleBtn,         true,    nullptr,    nullptr },

            { "anim_castle.color_passive",    0xADC, Store::Float, 4, 0, kCastleBtn,           true,    nullptr,    nullptr },
            { "anim_castle.color_active",     0xAEC, Store::Float, 4, 0, kCastleBtn,           true,    nullptr,    nullptr },
            { "anim_castle.color_disabled",   0xAFC, Store::Float, 4, 0, kCastleBtn,           true,    nullptr,    nullptr },
            { "anim_castle.color_blocked",    0xB0C, Store::Float, 4, 0, kCastleBtn,           true,    nullptr,    nullptr },
            { "anim_castle.color_nof_active", 0xB1C, Store::Float, 4, 0, kCastleBtn,           true,    nullptr,    nullptr },
            { "anim_castle.scale_passive",    0xB2C, Store::Float, 2, 0, kCastleBtn,           true,    nullptr,    nullptr },
            { "anim_castle.scale_active",     0xB34, Store::Float, 2, 0, kCastleBtn,           true,    nullptr,    nullptr },
            { "anim_castle.scale_disabled",   0xB3C, Store::Float, 2, 0, kCastleBtn,           true,    nullptr,    nullptr },
            { "anim_castle.scale_blocked",    0xB44, Store::Float, 2, 0, kCastleBtn,           true,    nullptr,    nullptr },
            { "anim_castle.scale_nof_active", 0xB4C, Store::Float, 2, 0, kCastleBtn,           true,    nullptr,    nullptr },

            { "id",                 0x004, Store::String, 0, 0x40,  kTable,                        true,    nullptr,    nullptr },
            { "header_color",       0x044, Store::Float,  4, 0,     kTable,                        true,    nullptr,    nullptr },
            { "header_scale",       0x054, Store::Float,  2, 0,     kTable,                        true,    nullptr,    nullptr },
            { "body_color",         0x05C, Store::Float,  4, 0,     kTable,                        true,    nullptr,    nullptr },
            { "body_scale",         0x06C, Store::Float,  2, 0,     kTable,                        true,    nullptr,    nullptr },
            { "line_pos",           0x074, Store::Float,  1, 0,     kTable,                        true,    nullptr,    nullptr },
            { "line_step",          0x078, Store::Float,  1, 0,     kTable,                        true,    nullptr,    nullptr },
            { "line_count",         0x07C, Store::Int,    0, 0,     kTable,                        true,    nullptr,    nullptr },
            { "lines_vert_align",   0x080, Store::Int,    0, 0,     kTable,                        true,    nullptr,    nullptr },
            { "point_step",         0x08C, Store::Float,  1, 0,     kTable,                        true,    nullptr,    nullptr },
            { "point_margin",       0x090, Store::Float,  1, 0,     kTable,                        true,    nullptr,    nullptr },
            { "font_id",            0x09C, Store::Int,    0, 0,     kTable,                        false,   nullptr,    nullptr },
            { "col.title",          0x00,  Store::String, 0, 0x40,  kTable,                        true,    nullptr,    nullptr,    0x98, 0x94, 0x50 },
            { "col.horz_pos",       0x40,  Store::Float,  1, 0,     kTable,                        true,    nullptr,    nullptr,    0x98, 0x94, 0x50 },
            { "col.align",          0x44,  Store::Int,    0, 0,     kTable,                        true,    nullptr,    nullptr,    0x98, 0x94, 0x50 },
            { "col.fixed",          0x4C,  Store::Float,  1, 0,     kTable,                        true,    nullptr,    nullptr,    0x98, 0x94, 0x50 },

            { "texture",            0x00,  Store::Int,    0, 0,     kImage,                        true,    nullptr,    nullptr },
            { "pos",                0x04,  Store::Float,  2, 0,     kImage,                        true,    nullptr,    nullptr },
            { "is_offset",          0x0C,  Store::Bool,   0, 0,     kImage,                        false,   nullptr,    nullptr },
            { "size",               0x10,  Store::Float,  2, 0,     kImage,                        true,    nullptr,    nullptr },
            { "horz_flip",          0x18,  Store::Bool,   0, 0,     kImage,                        true,    nullptr,    nullptr },
            { "vert_flip",          0x19,  Store::Bool,   0, 0,     kImage,                        true,    nullptr,    nullptr },
            { "horz_rept",          0x1C,  Store::Float,  1, 0,     kImage,                        true,    nullptr,    nullptr },
            { "vert_rept",          0x20,  Store::Float,  1, 0,     kImage,                        true,    nullptr,    nullptr },
            { "unreg_only",         0x24,  Store::Int,    0, 0,     kImage,                        true,    nullptr,    nullptr },

            { "pos",                0x00,  Store::Float,  2, 0,     kAnimImage,                    true,    nullptr,    nullptr },
            { "size",               0x08,  Store::Float,  2, 0,     kAnimImage,                    true,    nullptr,    nullptr },
            { "unreg_only",         0x10,  Store::Int,    0, 0,     kAnimImage,                    true,    nullptr,    nullptr },
            { "animation",          0x14,  Store::Int,    0, 0,     kAnimImage,                    true,    nullptr,    nullptr },

            { "id",                 0x000, Store::String, 0, 0x40,  kStaticText,                   true,    nullptr,    nullptr },
            { "text",               0x040, Store::String, 0, 0x400, kStaticText,                   true,    nullptr,    nullptr },
            { "pos",                0x440, Store::Float,  2, 0,     kStaticText,                   true,    nullptr,    nullptr },
            { "font_id",            0x448, Store::Int,    0, 0,     kStaticText,                   false,   nullptr,    nullptr },
            { "font_scale",         0x44C, Store::Float,  2, 0,     kStaticText,                   true,    nullptr,    nullptr },
            { "font_color",         0x454, Store::Float,  4, 0,     kStaticText,                   true,    nullptr,    nullptr },
            { "line_height",        0x464, Store::Float,  1, 0,     kStaticText,                   true,    nullptr,    nullptr },
            { "align",              0x468, Store::Int,    0, 0,     kStaticText,                   true,    nullptr,    nullptr },
            { "unreg_only",         0x46C, Store::Int,    0, 0,     kStaticText,                   true,    nullptr,    nullptr },
        };

        constexpr size_t kPropertyCount = sizeof(g_properties) / sizeof(g_properties[0]);

        bool ValidatePropertyTable()
        {
            bool sound = true;

            for (size_t i = 0; i < kPropertyCount; ++i)
            {
                for (size_t k = i + 1; k < kPropertyCount; ++k)
                {
                    if (strcmp(g_properties[i].key, g_properties[k].key) != 0)
                    {
                        continue;
                    }

                    const uint32_t overlap = g_properties[i].kinds & g_properties[k].kinds;
                    if (overlap != 0)
                    {
                        FORGE_LOG_ERROR("Property table: '%s' is claimed twice for the same "
                                        "kind (masks %08X and %08X overlap in %08X)",
                                        g_properties[i].key, g_properties[i].kinds,
                                        g_properties[k].kinds, overlap);
                        sound = false;
                    }
                }
            }

            return sound;
        }

        int CollectionCount(void* item, const PropertyDescriptor& descriptor)
        {
            const uint8_t* const entries =
                *reinterpret_cast<uint8_t* const*>(Field(item, descriptor.arrayOffset));
            const int count =
                *reinterpret_cast<const int*>(Field(item, descriptor.countOffset));

            return (entries != nullptr && count > 0) ? count : 0;
        }

        void* CollectionAt(void* item, const PropertyDescriptor& descriptor, int index)
        {
            if (index < 0 || index >= CollectionCount(item, descriptor))
            {
                return nullptr;
            }

            uint8_t* const entries =
                *reinterpret_cast<uint8_t**>(Field(item, descriptor.arrayOffset));
            return entries + static_cast<size_t>(index) * descriptor.stride;
        }

        const PropertyDescriptor* FindDescriptor(AmodMenuItem item, const char* key)
        {
            if (key == nullptr)
            {
                return nullptr;
            }

            for (const PropertyDescriptor& descriptor : g_properties)
            {
                if (strcmp(descriptor.key, key) != 0)
                {
                    continue;
                }

                if ((descriptor.kinds & KindBit(KindOf(item))) == 0)
                {
                    continue;
                }

                return &descriptor;
            }

            return nullptr;
        }

        bool LoadProperty(void* item, const PropertyDescriptor& descriptor, AmodValue& value)
        {
            value = AmodValue();

            if (descriptor.read != nullptr)
            {
                return descriptor.read(item, value);
            }

            switch (descriptor.store)
            {
            case Store::Float:
            {
                const float* const numbers = Floats(item, descriptor.offset);
                value.kind = AMOD_VALUE_FLOAT;
                value.count = descriptor.count;
                for (uint32_t i = 0; i < descriptor.count; ++i)
                {
                    value.numbers[i] = numbers[i];
                }
                return true;
            }

            case Store::String:
                value.kind = AMOD_VALUE_STRING;
                value.text = reinterpret_cast<const char*>(Field(item, descriptor.offset));
                return true;

            case Store::Bool:
                value.kind = AMOD_VALUE_BOOL;
                value.integer = *Field(item, descriptor.offset) != 0 ? 1 : 0;
                return true;

            case Store::Int:
                value.kind = AMOD_VALUE_INT;
                value.integer = *reinterpret_cast<const int*>(Field(item, descriptor.offset));
                return true;
            }

            return false;
        }

        bool StoreProperty(void* item, const PropertyDescriptor& descriptor, const AmodValue& value)
        {
            if (!descriptor.writable)
            {
                return false;
            }

            switch (descriptor.store)
            {
            case Store::Float:
                if (value.kind != AMOD_VALUE_FLOAT || value.count != descriptor.count)
                {
                    return false;
                }
                break;

            case Store::String:
                if (value.kind != AMOD_VALUE_STRING || value.text == nullptr)
                {
                    return false;
                }
                break;

            case Store::Bool:
            case Store::Int:
                if (value.kind != AMOD_VALUE_BOOL && value.kind != AMOD_VALUE_INT)
                {
                    return false;
                }
                break;
            }

            if (descriptor.write != nullptr)
            {
                return descriptor.write(item, value);
            }

            switch (descriptor.store)
            {
            case Store::Float:
            {
                float* const numbers = Floats(item, descriptor.offset);
                for (uint32_t i = 0; i < descriptor.count; ++i)
                {
                    numbers[i] = value.numbers[i];
                }
                return true;
            }

            case Store::String:
                strncpy_s(reinterpret_cast<char*>(Field(item, descriptor.offset)),
                          descriptor.capacity, value.text, _TRUNCATE);
                return true;

            case Store::Bool:
                *Field(item, descriptor.offset) = value.integer != 0 ? 1 : 0;
                return true;

            case Store::Int:
                *reinterpret_cast<int*>(Field(item, descriptor.offset)) = value.integer;
                return true;
            }

            return false;
        }

        // --- bookkeeping ---------------------------------------------------------------

        void ForgetMenu(void* menu)
        {
            const auto found = g_elementsByMenu.find(menu);
            if (found == g_elementsByMenu.end())
            {
                return;
            }

            for (void* element : found->second)
            {
                g_validElements.erase(element);
                g_kindByElement.erase(element);
            }
            g_elementsByMenu.erase(found);
        }

        void RecordMenu(void* menu)
        {
            ForgetMenu(menu);

            std::vector<void*> elements;
            int fromGroups = 0;

            uint8_t* const groups = *reinterpret_cast<uint8_t**>(Field(menu, GameAddress::kMenuGroupsOffset));
            const int groupCount = *reinterpret_cast<int*>(Field(menu, GameAddress::kMenuGroupCountOffset));

            if (groups != nullptr && groupCount > 0)
            {
                for (int i = 0; i < groupCount; ++i)
                {
                    uint8_t* const group = groups + static_cast<size_t>(i) * GameAddress::kMenuGroupStride;
                    void** const items = *reinterpret_cast<void***>(group + GameAddress::kMenuGroupItemsOffset);
                    const int itemCount = *reinterpret_cast<int*>(group + GameAddress::kMenuGroupItemCountOffset);

                    for (int k = 0; items != nullptr && k < itemCount; ++k)
                    {
                        if (items[k] != nullptr)
                        {
                            elements.push_back(items[k]);
                            g_validElements.insert(items[k]);
                            ++fromGroups;
                        }
                    }
                }
            }

            const struct
            {
                uintptr_t offset;
                uintptr_t stride;
                ElementKind kind;
            }
            collections[] =
            {
                { GameAddress::kMenuImagesOffset,     GameAddress::kImageStride,      ElementKind::Image },
                { GameAddress::kMenuAnimImagesOffset, GameAddress::kAnimImageStride,  ElementKind::AnimImage },
                { GameAddress::kMenuStaticTextOffset, GameAddress::kStaticTextStride, ElementKind::StaticText },
            };

            int decorations[3] = {};

            for (size_t c = 0; c < sizeof(collections) / sizeof(collections[0]); ++c)
            {
                const auto& collection = collections[c];

                uint8_t* const header =
                    *reinterpret_cast<uint8_t**>(Field(menu, collection.offset));
                if (header == nullptr)
                {
                    continue;
                }

                uint8_t* const entries =
                    *reinterpret_cast<uint8_t**>(header + GameAddress::kCollectionItemsOffset);
                const int count =
                    *reinterpret_cast<int*>(header + GameAddress::kCollectionCountOffset);

                for (int i = 0; entries != nullptr && i < count; ++i)
                {
                    void* const record = entries + static_cast<size_t>(i) * collection.stride;
                    elements.push_back(record);
                    g_validElements.insert(record);
                    g_kindByElement[record] = collection.kind;
                    ++decorations[c];
                }
            }

            void** const tables =
                *reinterpret_cast<void***>(Field(menu, GameAddress::kMenuTablesOffset));
            const int tableCount =
                *reinterpret_cast<int*>(Field(menu, GameAddress::kMenuTableCountOffset));

            int fromTables = 0;
            for (int i = 0; tables != nullptr && i < tableCount; ++i)
            {
                if (tables[i] != nullptr)
                {
                    elements.push_back(tables[i]);
                    g_validElements.insert(tables[i]);
                    ++fromTables;
                }
            }

            /*FORGE_LOG_INFO("Menu %p: %d element(s) - %d in groups, %d image, %d image_anim, "
                           "%d static, %d table", menu, static_cast<int>(elements.size()),
                           fromGroups, decorations[0], decorations[1], decorations[2],
                           fromTables);*/

            g_elementsByMenu[menu] = std::move(elements);
        }

        // --- building objects out of a script fragment ----------------------------------
        //
        // The game builds every kind the same way - alloc, construct, parse, add - so feeding
        // the parser the mod's text needs no new logic, and everything a parser derives (the
        // alignment, the drawing centre, the caption rectangle) comes out as from a menu_*.txt:
        //
        //     block = Mem_AllocTracked(size, file, line);
        //     Kind_Create(block);
        //     if (parse(block, parser)) Menu_AddElement(menu, block); else block->vtable[0x34](1);

        typedef void* (__cdecl* ParserCreateRoutine)(char* text, uint8_t keyPrefix);
        typedef void (__cdecl* ParserDestroyRoutine)(void* parser);
        typedef int (__cdecl* ParserEnterBlockRoutine)(void* parser, const char* name);
        typedef int (__cdecl* ParserNextBlockRoutine)(void* parser);
        typedef void (__cdecl* ParserLeaveBlockRoutine)(void* parser);
        typedef void* (__cdecl* AllocTrackedRoutine)(uint32_t size, const char* file, uint32_t line);
        typedef void* (__fastcall* ElementCreateRoutine)(void* block, void* unused);
        typedef void (__fastcall* AddElementRoutine)(void* menu, void* unused, void* element);

        // Each kind cleans up its own stack, so these are not interchangeable.
        typedef int (__fastcall* OffsetParseRoutine)(void* element, void* unused,
                                                     void* parser, float itemsOffs);
        typedef int (__fastcall* MenuParseRoutine)(void* element, void* unused,
                                                   void* parser, void* menu);
        typedef int (__fastcall* VirtualParseRoutine)(void* element, void* unused, void* parser);

        ParserCreateRoutine g_parserCreate = nullptr;
        ParserDestroyRoutine g_parserDestroy = nullptr;
        ParserEnterBlockRoutine g_parserEnter = nullptr;
        ParserNextBlockRoutine g_parserNext = nullptr;
        ParserLeaveBlockRoutine g_parserLeave = nullptr;
        AllocTrackedRoutine g_allocTracked = nullptr;

        // How a kind's parser is reached and what it wants besides the parser.
        enum class ParseStyle
        {
            WithOffset,     // the $items_offs of the enclosing block
            WithMenu,       // the menu itself
            Direct,         // called at a fixed address, the parser alone
            Virtual         // vtable +0x30, the parser alone
        };

        struct BlockDescriptor
        {
            const char* container;      // "$menu_items", "$sliders", ...
            const char* element;        // "$item", "$slider", ...
            uint32_t size;
            GameSymbol create;
            GameSymbol parse;           // ignored when the style is Virtual
            ParseStyle style;
            GameSymbol add;             // Menu_AddElement, or Menu_AddTable for a table
            uintptr_t destroySlot;      // vtable slot that frees a refused block
        };

        constexpr BlockDescriptor kBlocks[] =
        {
            { "menu_items",     "item",   GameAddress::kSizeMenuItem,
              GameSymbol::MenuItemCreate,      GameSymbol::MenuItemParse, ParseStyle::WithOffset,
              GameSymbol::MenuAddElement, GameAddress::kElementDestroyVtableSlot },
            { "menu_items",     "logo",   GameAddress::kSizeMenuLogo,
              GameSymbol::MenuLogoCreate,      GameSymbol::MenuLogoParse, ParseStyle::WithOffset,
              GameSymbol::MenuAddElement, GameAddress::kElementDestroyVtableSlot },
            { "text_buttons",   "button", GameAddress::kSizeTextButton,
              GameSymbol::TextButtonCreate,    GameSymbol::Count,         ParseStyle::Virtual,
              GameSymbol::MenuAddElement, GameAddress::kElementDestroyVtableSlot },
            { "img_buttons",    "button", GameAddress::kSizeImageButton,
              GameSymbol::ImageButtonCreate,   GameSymbol::Count,         ParseStyle::Virtual,
              GameSymbol::MenuAddElement, GameAddress::kElementDestroyVtableSlot },
            { "hotkey_buttons", "button", GameAddress::kSizeHotkeyButton,
              GameSymbol::HotkeyButtonCreate,  GameSymbol::Count,         ParseStyle::Virtual,
              GameSymbol::MenuAddElement, GameAddress::kElementDestroyVtableSlot },
            { "castle_buttons", "button", GameAddress::kSizeCastleButton,
              GameSymbol::CastleButtonCreate,  GameSymbol::Count,         ParseStyle::Virtual,
              GameSymbol::MenuAddElement, GameAddress::kElementDestroyVtableSlot },
            { "lists",          "list",   GameAddress::kSizeMenuList,
              GameSymbol::MenuListCreate,      GameSymbol::MenuListParse, ParseStyle::WithMenu,
              GameSymbol::MenuAddElement, GameAddress::kElementDestroyVtableSlot },
            { "sliders",        "slider", GameAddress::kSizeMenuSlider,
              GameSymbol::MenuSliderCreate,    GameSymbol::Count,         ParseStyle::Virtual,
              GameSymbol::MenuAddElement, GameAddress::kElementDestroyVtableSlot },
            { "tables",         "table",  GameAddress::kSizeMenuTable,
              GameSymbol::MenuTableCreate,     GameSymbol::MenuTableParse, ParseStyle::Direct,
              GameSymbol::MenuAddTable,   GameAddress::kTableDestroyVtableSlot },
        };

        bool CreationReady()
        {
            if (g_parserCreate == nullptr || g_parserDestroy == nullptr ||
                g_parserEnter == nullptr || g_parserNext == nullptr ||
                g_parserLeave == nullptr || g_allocTracked == nullptr)
            {
                return false;
            }

            for (const BlockDescriptor& block : kBlocks)
            {
                if (GameSymbols::Address(block.create) == nullptr ||
                    GameSymbols::Address(block.add) == nullptr)
                {
                    return false;
                }
                if (block.style != ParseStyle::Virtual &&
                    GameSymbols::Address(block.parse) == nullptr)
                {
                    return false;
                }
            }
            return true;
        }

        void DestroyObject(void* object, uintptr_t destroySlot)
        {
            void** const vtable = *reinterpret_cast<void***>(object);
            typedef void (__fastcall* DestroyRoutine)(void* self, void* unused, int flags);
            reinterpret_cast<DestroyRoutine>(vtable[destroySlot / sizeof(void*)])(
                object, nullptr, 1);
        }

        bool ParseElement(void* element, const BlockDescriptor& block, void* parser, void* menu)
        {
            switch (block.style)
            {
            case ParseStyle::WithOffset:
            {
                // The offset the enclosing block would have supplied. A fragment that wants
                // one says $pos instead, so zero is the honest default.
                const OffsetParseRoutine parse = reinterpret_cast<OffsetParseRoutine>(
                    GameSymbols::Address(block.parse));
                return Succeeded(parse(element, nullptr, parser, 0.0f));
            }

            case ParseStyle::WithMenu:
            {
                const MenuParseRoutine parse = reinterpret_cast<MenuParseRoutine>(
                    GameSymbols::Address(block.parse));
                return Succeeded(parse(element, nullptr, parser, menu));
            }

            case ParseStyle::Direct:
            {
                const VirtualParseRoutine parse = reinterpret_cast<VirtualParseRoutine>(
                    GameSymbols::Address(block.parse));
                return Succeeded(parse(element, nullptr, parser));
            }

            case ParseStyle::Virtual:
            {
                void** const vtable = *reinterpret_cast<void***>(element);
                const VirtualParseRoutine parse = reinterpret_cast<VirtualParseRoutine>(
                    vtable[GameAddress::kElementParseVtableSlot / sizeof(void*)]);
                return Succeeded(parse(element, nullptr, parser));
            }
            }

            return false;
        }

        void* BuildBlock(void* menu, void* parser, const BlockDescriptor& block, int& built)
        {
            if (!Succeeded(g_parserEnter(parser, block.element)))
            {
                return nullptr;
            }

            const ElementCreateRoutine create = reinterpret_cast<ElementCreateRoutine>(
                GameSymbols::Address(block.create));
            const AddElementRoutine add = reinterpret_cast<AddElementRoutine>(
                GameSymbols::Address(block.add));
            void* first = nullptr;

            do
            {
                void* const storage = g_allocTracked(block.size, ".\\AzForge.cpp", __LINE__);
                if (storage == nullptr)
                {
                    break;
                }

                void* const element = create(storage, nullptr);
                if (element == nullptr)
                {
                    continue;
                }

                if (!ParseElement(element, block, parser, menu))
                {
                    // Refused, most likely for want of an $id.
                    DestroyObject(element, block.destroySlot);
                    continue;
                }

                add(menu, nullptr, element);

                if (first == nullptr)
                {
                    first = element;
                }
                ++built;
            }
            while (Succeeded(g_parserNext(parser)));

            g_parserLeave(parser);
            return first;
        }

        // --- decorations ----------------------------------------------------------------
        //
        // These do not go through Menu_AddElement, because they are not elements. Each of
        // the three parsers allocates a whole collection and answers it - Menu_ParseScript
        // stores what comes back straight into the menu - so calling one on a fragment would
        // throw away whatever the menu already held.
        //
        // What makes merging honest here, and would not for an element, is that a decoration
        // record is self contained: numbers, text and resolved handles, no pointer back to
        // its collection, no navigation ring. Two arrays of them concatenate.

        typedef void* (__cdecl* ParseImagesRoutine)(void* parser, const char* blockName);
        typedef void* (__cdecl* ParseCollectionRoutine)(void* parser);
        typedef void* (__cdecl* ReallocTrackedRoutine)(void* block, uint32_t size,
                                                        const char* file, uint32_t line);
        typedef void (__cdecl* FreeTrackedRoutine)(void* block);

        ReallocTrackedRoutine g_reallocTracked = nullptr;
        FreeTrackedRoutine g_freeTracked = nullptr;

        struct DecorationDescriptor
        {
            const char* container;      // "images", "images_anim", "static_text"
            const char* element;        // the block one entry lives in
            uintptr_t collection;       // where the menu keeps it
            size_t stride;
            GameSymbol parse;
            bool takesBlockName;        // only Menu_ParseImages is told its own block
        };

        constexpr DecorationDescriptor kDecorations[] =
        {
            { "images",      "image",  GameAddress::kMenuImagesOffset,
              GameAddress::kImageStride,      GameSymbol::MenuParseImages,         true },
            { "images_anim", "image",  GameAddress::kMenuAnimImagesOffset,
              GameAddress::kAnimImageStride,  GameSymbol::MenuParseAnimatedImages, false },
            { "static_text", "static", GameAddress::kMenuStaticTextOffset,
              GameAddress::kStaticTextStride, GameSymbol::MenuParseStaticText,     false },
        };

        void* ParseDecorationGuarded(const DecorationDescriptor& decoration, void* parser)
        {
            __try
            {
                if (decoration.takesBlockName)
                {
                    return reinterpret_cast<ParseImagesRoutine>(
                        GameSymbols::Address(decoration.parse))(parser, decoration.container);
                }

                return reinterpret_cast<ParseCollectionRoutine>(
                    GameSymbols::Address(decoration.parse))(parser);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return nullptr;
            }
        }

        void* MergeDecorations(void* menu, const DecorationDescriptor& decoration,
                               void* parsed, int& built)
        {
            uint8_t* const fresh =
                *reinterpret_cast<uint8_t**>(Field(parsed, GameAddress::kCollectionItemsOffset));
            const int freshCount =
                *reinterpret_cast<int*>(Field(parsed, GameAddress::kCollectionCountOffset));

            if (fresh == nullptr || freshCount <= 0)
            {
                return nullptr;
            }

            void** const slot = reinterpret_cast<void**>(Field(menu, decoration.collection));
            uint8_t* const existing = static_cast<uint8_t*>(*slot);

            int* const count = existing != nullptr
                ? reinterpret_cast<int*>(existing + GameAddress::kCollectionCountOffset)
                : nullptr;

            // Nothing of this kind in the menu yet, so what was parsed simply becomes it.
            if (existing == nullptr || *count <= 0)
            {
                if (existing != nullptr && g_freeTracked != nullptr)
                {
                    g_freeTracked(existing);
                }
                *slot = parsed;
                built += freshCount;
                return fresh;
            }

            uint8_t** const items =
                reinterpret_cast<uint8_t**>(existing + GameAddress::kCollectionItemsOffset);
            const size_t before = static_cast<size_t>(*count) * decoration.stride;
            const size_t total = before + static_cast<size_t>(freshCount) * decoration.stride;

            uint8_t* const grown = static_cast<uint8_t*>(
                g_reallocTracked(*items, static_cast<uint32_t>(total),
                                 ".\\AzForge.cpp", __LINE__));
            if (grown == nullptr)
            {
                return nullptr;
            }

            memcpy(grown + before, fresh, static_cast<size_t>(freshCount) * decoration.stride);
            *items = grown;
            *count += freshCount;

            if (g_freeTracked != nullptr)
            {
                g_freeTracked(fresh);
                g_freeTracked(parsed);
            }

            built += freshCount;
            return grown + before;
        }

        bool FragmentHas(void* parser, const DecorationDescriptor& decoration)
        {
            if (!Succeeded(g_parserEnter(parser, decoration.container)))
            {
                return false;
            }

            const bool present = Succeeded(g_parserEnter(parser, decoration.element));
            if (present)
            {
                g_parserLeave(parser);
            }

            g_parserLeave(parser);
            return present;
        }

        void* BuildDecorations(void* menu, void* parser, int& built)
        {
            if (g_reallocTracked == nullptr)
            {
                return nullptr;
            }

            void* first = nullptr;

            for (const DecorationDescriptor& decoration : kDecorations)
            {
                if (GameSymbols::Address(decoration.parse) == nullptr ||
                    !FragmentHas(parser, decoration))
                {
                    continue;
                }

                void* const parsed = ParseDecorationGuarded(decoration, parser);
                if (parsed == nullptr)
                {
                    FORGE_LOG_WARN("A mod's $%s block was refused by the game's parser",
                                   decoration.container);
                    continue;
                }

                void* const made = MergeDecorations(menu, decoration, parsed, built);
                if (first == nullptr)
                {
                    first = made;
                }
            }

            return first;
        }

        // A back asked outside a click: the next Menu_Update is handed it as the top menu's click result.
        bool g_backLater = false;
        DWORD g_backLaterAt = 0;
        const DWORD kBackLaterPatience = 500;

        const char* g_menuLater = nullptr;
        bool g_menuLaterOverlay = false;
        DWORD g_menuLaterAt = 0;

        void* ManagerMenu(uintptr_t offset)
        {
            void* const* const slot = static_cast<void* const*>(GameModule::Resolve(GameAddress::kMenuManager));
            const uint8_t* const manager = slot != nullptr ? static_cast<const uint8_t*>(*slot) : nullptr;
            return manager != nullptr ? *reinterpret_cast<void* const*>(manager + offset) : nullptr;
        }

        // The overlay when one is up, the screen menu otherwise.
        void* TopMenu()
        {
            void* const box = ManagerMenu(GameAddress::kMenuManagerBoxOffset);
            return box != nullptr ? box : ManagerMenu(GameAddress::kMenuManagerCurrentOffset);
        }

        // Any result from an overlay closes it; a screen menu goes back on ID_BACK.
        void WriteBack(const char** result, void* menu)
        {
            const bool overlay = menu == ManagerMenu(GameAddress::kMenuManagerBoxOffset);

            result[GameAddress::kClickResultMenuId / sizeof(char*)] = static_cast<const char*>(menu) + GameAddress::kMenuIdOffset;
            result[GameAddress::kClickResultItemId / sizeof(char*)] = overlay ? kNoItem : kBackItem;
            result[GameAddress::kClickResultSubMenu / sizeof(char*)] = nullptr;
            result[GameAddress::kClickResultBoxMenu / sizeof(char*)] = nullptr;
            result[GameAddress::kClickResultRegMenu / sizeof(char*)] = nullptr;
        }

        void WriteMenu(const char** result, void* menu, const char* ownId, bool overlay)
        {
            result[GameAddress::kClickResultMenuId / sizeof(char*)] = static_cast<const char*>(menu) + GameAddress::kMenuIdOffset;
            result[GameAddress::kClickResultItemId / sizeof(char*)] = kNoItem;
            result[GameAddress::kClickResultSubMenu / sizeof(char*)] = overlay ? nullptr : ownId;
            result[GameAddress::kClickResultBoxMenu / sizeof(char*)] = overlay ? ownId : nullptr;
            result[GameAddress::kClickResultRegMenu / sizeof(char*)] = nullptr;
        }

        typedef int (__cdecl* ModelParseRoutine)(void* parser, void* model);
        // RET 8 at 0x00451B62: it clears its own arguments, so calling it as __cdecl cleared
        // them twice and tore the caller's locals off the stack.
        typedef void* (__stdcall* ModelsGrowRoutine)(void** array, int* count);

        // Models are the one block with no parser of its own: Menu_ParseScript walks $models and
        // its $item blocks itself, parsing each record onto the stack and copying it into a slot
        // the array grows by one. This is that loop, run against a mod's fragment instead.
        // ui_ModelParse reaches the model and texture loaders through global function pointers
        // and reads keys a fragment may not carry. The game only ever runs it while a menu script
        // is being loaded; a mod runs it whenever it likes, so it is held at arm's length.
        bool ParseModelGuarded(ModelParseRoutine parse, void* parser, void* record)
        {
            __try
            {
                return Succeeded(parse(parser, record));
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                FORGE_LOG_WARN("A mod's $models entry threw inside the game's parser");
                return false;
            }
        }

        void* BuildModels(void* menu, void* parser, int& built)
        {
            const ModelParseRoutine parse = reinterpret_cast<ModelParseRoutine>(
                GameSymbols::Address(GameSymbol::UiModelParse));
            const ModelsGrowRoutine grow = reinterpret_cast<ModelsGrowRoutine>(
                GameSymbols::Address(GameSymbol::MenuModelsGrow));

            if (parse == nullptr || grow == nullptr ||
                !Succeeded(g_parserEnter(parser, "models")))
            {
                return nullptr;
            }

            void* first = nullptr;

            if (Succeeded(g_parserEnter(parser, "item")))
            {
                do
                {
                    // Four byte alignment, because the game reads it as a uint array
                    uint32_t record[GameAddress::kModelStride / sizeof(uint32_t)] = {};

                    if (!ParseModelGuarded(parse, parser, record))
                    {
                        FORGE_LOG_WARN("A mod's $models entry was refused by the game's parser");
                        continue;
                    }

                    void** const array =
                        reinterpret_cast<void**>(Field(menu, GameAddress::kMenuModelsOffset));
                    int* const count =
                        reinterpret_cast<int*>(Field(menu, GameAddress::kMenuModelCountOffset));
                    void* const slot = grow(array, count);

                    if (slot == nullptr)
                    {
                        FORGE_LOG_WARN("A mod's $models entry had nowhere to go");
                        continue;
                    }

                    memcpy(slot, record, GameAddress::kModelStride);

                    if (first == nullptr)
                    {
                        first = slot;
                    }
                    ++built;
                }
                while (Succeeded(g_parserNext(parser)));

                g_parserLeave(parser);
            }

            g_parserLeave(parser);
            return first;
        }

        void* BuildFromFragment(void* menu, const char* fragment, int& built)
        {
            built = 0;

            if (!CreationReady() || menu == nullptr || fragment == nullptr ||
                fragment[0] == '\0')
            {
                return nullptr;
            }

            // The parser keeps the pointer and strips comments in place, so the text must be
            // writable and outlive it - a mod's literal is neither.
            std::vector<char> text(fragment, fragment + strlen(fragment) + 1);

            void* const parser = g_parserCreate(text.data(), GameAddress::kParserKeyPrefix);
            if (parser == nullptr)
            {
                FORGE_LOG_ERROR("A mod's fragment did not produce a parser");
                return nullptr;
            }

            void* first = nullptr;

            for (const BlockDescriptor& block : kBlocks)
            {
                if (!Succeeded(g_parserEnter(parser, block.container)))
                {
                    continue;
                }

                void* const made = BuildBlock(menu, parser, block, built);
                if (first == nullptr)
                {
                    first = made;
                }

                g_parserLeave(parser);
            }

            void* const decoration = BuildDecorations(menu, parser, built);
            if (first == nullptr)
            {
                first = decoration;
            }

            void* const model = BuildModels(menu, parser, built);
            if (first == nullptr)
            {
                first = model;
            }

            g_parserDestroy(parser);
            return first;
        }

        struct MenuCall
        {
            const Subscription* item;
            AmodMenuEvent event;
            void* data;
        };

        void __cdecl CallSubscriber(void* context)
        {
            const MenuCall* const call = static_cast<const MenuCall*>(context);
            call->item->callback(call->event, call->data, call->item->user);
        }

        // A mod that faults in its menu callback loses its menu events, not the game.
        void DropSubscriber(ModContext* owner)
        {
            for (size_t i = g_subscriptions.size(); i-- > 0;)
            {
                if (g_subscriptions[i].owner == owner)
                {
                    g_subscriptions.erase(g_subscriptions.begin() + static_cast<ptrdiff_t>(i));
                }
            }
        }

        void Dispatch(AmodMenuEvent event, void* data)
        {
            const std::vector<Subscription> snapshot = g_subscriptions;
            std::vector<ModContext*> dropped;

            for (const Subscription& item : snapshot)
            {
                if (item.event != event || item.callback == nullptr ||
                    std::find(dropped.begin(), dropped.end(), item.owner) != dropped.end())
                {
                    continue;
                }

                MenuCall call = { &item, event, data };

                if (!Crash::Guarded(&CallSubscriber, &call))
                {
                    FORGE_LOG_ERROR("[%s] faulted in its menu callback (%s); it gets no more menu events",
                        item.owner != nullptr ? item.owner->id.c_str() : "?",
                        event == AMOD_MENU_LOADED ? "loaded" : "clicked");

                    dropped.push_back(item.owner);
                    DropSubscriber(item.owner);
                }
            }
        }

        void ReleaseSubscription(void* payload)
        {
            const uint32_t id = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(payload));
            for (size_t i = 0; i < g_subscriptions.size(); ++i)
            {
                if (g_subscriptions[i].id == id)
                {
                    g_subscriptions.erase(g_subscriptions.begin() + static_cast<ptrdiff_t>(i));
                    return;
                }
            }
        }

        void Publish(void* menu)
        {
            if (menu == nullptr)
            {
                return;
            }

            RecordMenu(menu);

            const char* const menuId =
                reinterpret_cast<const char*>(Field(menu, GameAddress::kMenuIdOffset));
            if (menuId[0] == '\0')
            {
                return;
            }

            g_menusById[menuId] = menu;

            AmodMenuLoaded loaded = {};
            loaded.menuId = menuId;
            Dispatch(AMOD_MENU_LOADED, &loaded);
        }

        int __fastcall ParseScriptDetour(void* menu, void* unused, const char* scriptFile)
        {
            ForgetMenu(menu);

            g_parsingMenu = menu;
            const int result = g_originalParse(menu, unused, scriptFile);
            g_parsingMenu = nullptr;

            if (Succeeded(result))
            {
                g_lastParsedMenu = menu;
            }

            return result;
        }

        int __fastcall RegisterDetour(void* manager, void* unused, void* menu, const char* scriptFile, const char* menuId, int force)
        {
            const int result = g_originalRegister(manager, unused, menu, scriptFile, menuId, force);

            if (Succeeded(result))
            {
                Publish(menu);
            }

            return result;
        }

        void __fastcall TakeClickResultDetour(void* menu, void* unused, void* dest)
        {
            g_originalTakeClick(menu, unused, dest);

            const char** const result = static_cast<const char**>(dest);
            const char* const itemId = result[GameAddress::kClickResultItemId / sizeof(char*)];

            if (itemId == nullptr || itemId[0] == '\0')
            {
                if (menu != TopMenu())
                {
                    return;
                }

                const DWORD now = GetTickCount();

                if (g_backLater)
                {
                    g_backLater = false;

                    if (now - g_backLaterAt <= kBackLaterPatience)
                    {
                        WriteBack(result, menu);
                        return;
                    }
                }

                if (g_menuLater != nullptr)
                {
                    const char* const ownId = g_menuLater;
                    g_menuLater = nullptr;

                    if (now - g_menuLaterAt <= kBackLaterPatience)
                    {
                        WriteMenu(result, menu, ownId, g_menuLaterOverlay);
                    }
                }

                return;
            }

            const char* const menuId = result[GameAddress::kClickResultMenuId / sizeof(char*)];

            AmodMenuClicked clicked = {};
            clicked.menuId = menuId != nullptr ? menuId : "";
            clicked.itemId = itemId;
            clicked.handled = 0;

            g_askedSub = nullptr;
            g_askedBox = nullptr;
            g_askedBack = false;
            g_inClick = true;
            Dispatch(AMOD_MENU_CLICKED, &clicked);
            g_inClick = false;

            if (g_askedSub != nullptr || g_askedBox != nullptr)
            {
                result[GameAddress::kClickResultItemId / sizeof(char*)] = kNoItem;
                result[GameAddress::kClickResultSubMenu / sizeof(char*)] = g_askedSub;
                result[GameAddress::kClickResultBoxMenu / sizeof(char*)] = g_askedBox;
                result[GameAddress::kClickResultRegMenu / sizeof(char*)] = nullptr;
            }
            else if (g_askedBack)
            {
                WriteBack(result, menu);
            }
            else if (clicked.handled != 0)
            {
                result[GameAddress::kClickResultMenuId / sizeof(char*)] = nullptr;
                result[GameAddress::kClickResultItemId / sizeof(char*)] = nullptr;
                result[GameAddress::kClickResultSubMenu / sizeof(char*)] = nullptr;
                result[GameAddress::kClickResultBoxMenu / sizeof(char*)] = nullptr;
                result[GameAddress::kClickResultRegMenu / sizeof(char*)] = nullptr;
            }
        }

        int __stdcall LoadFromScriptDetour(const char* menuId, const char* scriptFile)
        {
            g_lastParsedMenu = nullptr;

            const int result = g_originalLoad(menuId, scriptFile);

            if (Succeeded(result))
            {
                Publish(g_lastParsedMenu);
            }

            return result;
        }

        void* MenuByIdOrNull(const char* menuId)
        {
            if (menuId == nullptr)
            {
                return nullptr;
            }

            const auto found = g_menusById.find(menuId);
            if (found != g_menusById.end())
            {
                return found->second;
            }

            if (g_findMenu != nullptr)
            {
                void** const slot = static_cast<void**>(
                    GameModule::Resolve(GameAddress::kMenuManager));
                void* const manager = slot != nullptr ? *slot : nullptr;
                void* const menu = manager != nullptr ? g_findMenu(manager, nullptr, menuId) : nullptr;

                if (menu != nullptr)
                {
                    RecordMenu(menu);
                    g_menusById[menuId] = menu;
                    return menu;
                }
            }

            return nullptr;
        }

        AmodToken __cdecl ApiSubscribe(AmodMod mod, AmodMenuEvent event, AmodMenuCallback callback, void* user)
        {
            ModContext* context = Resolve(mod);
            if (context == nullptr || callback == nullptr)
            {
                return AMOD_INVALID_TOKEN;
            }

            Subscription item;
            item.id = g_nextSubscriptionId++;
            item.owner = context;
            item.event = event;
            item.callback = callback;
            item.user = user;
            g_subscriptions.push_back(item);

            if (event == AMOD_MENU_LOADED)
            {
                for (const auto& entry : g_menusById)
                {
                    AmodMenuLoaded loaded = {};
                    loaded.menuId = entry.first.c_str();
                    callback(AMOD_MENU_LOADED, &loaded, user);
                }
            }

            return context->owned.Add(&ReleaseSubscription,
                                      reinterpret_cast<void*>(static_cast<uintptr_t>(item.id)));
        }

        void __cdecl ApiUnsubscribe(AmodMod mod, AmodToken token)
        {
            ModContext* context = Resolve(mod);
            if (context != nullptr)
            {
                context->owned.Remove(token);
            }
        }

        void* FindRecordedById(void* menu, const char* itemId)
        {
            const auto found = g_elementsByMenu.find(menu);
            if (found == g_elementsByMenu.end())
            {
                return nullptr;
            }

            for (void* const element : found->second)
            {
                const PropertyDescriptor* const descriptor = FindDescriptor(element, "id");
                if (descriptor != nullptr &&
                    strcmp(reinterpret_cast<const char*>(Field(element, descriptor->offset)),
                           itemId) == 0)
                {
                    return element;
                }
            }

            return nullptr;
        }

        AmodMenuItem __cdecl ApiFindItem(AmodMod mod, const char* menuId, const char* itemId)
        {
            void* const menu = Resolve(mod) != nullptr ? MenuByIdOrNull(menuId) : nullptr;
            if (menu == nullptr || itemId == nullptr || g_findItem == nullptr)
            {
                return nullptr;
            }

            void* element = g_findItem(menu, nullptr, itemId);
            if (element == nullptr)
            {
                element = FindRecordedById(menu, itemId);
            }

            return IsLive(element) ? element : nullptr;
        }

        int __cdecl ApiGetItemCount(AmodMod mod, const char* menuId)
        {
            void* const menu = Resolve(mod) != nullptr ? MenuByIdOrNull(menuId) : nullptr;
            if (menu == nullptr)
            {
                return 0;
            }

            const auto found = g_elementsByMenu.find(menu);
            return found == g_elementsByMenu.end() ? 0 : static_cast<int>(found->second.size());
        }

        AmodMenuItem __cdecl ApiGetItemAt(AmodMod mod, const char* menuId, int index)
        {
            void* const menu = Resolve(mod) != nullptr ? MenuByIdOrNull(menuId) : nullptr;
            if (menu == nullptr || index < 0)
            {
                return nullptr;
            }

            const auto found = g_elementsByMenu.find(menu);
            if (found == g_elementsByMenu.end() ||
                static_cast<size_t>(index) >= found->second.size())
            {
                return nullptr;
            }

            return found->second[static_cast<size_t>(index)];
        }

        const char* __cdecl ApiGetItemId(AmodMod mod, AmodMenuItem item)
        {
            if (Resolve(mod) == nullptr || !IsLive(item))
            {
                return "";
            }

            const PropertyDescriptor* const descriptor = FindDescriptor(item, "id");
            if (descriptor == nullptr)
            {
                return "";
            }

            return reinterpret_cast<const char*>(Field(item, descriptor->offset));
        }

        int __cdecl ApiGetProperty(AmodMod mod, AmodMenuItem item, const char* key, AmodValue* value)
        {
            if (Resolve(mod) == nullptr || !IsLive(item) || value == nullptr)
            {
                return 0;
            }

            char base[64] = {};
            int index = -1;
            if (!SplitKey(key, base, sizeof(base), index))
            {
                return 0;
            }

            const PropertyDescriptor* const descriptor = FindDescriptor(item, base);
            if (descriptor == nullptr)
            {
                return 0;
            }

            if (descriptor->stride != 0)
            {
                if (index < 0)
                {
                    *value = AmodValue();
                    value->kind = AMOD_VALUE_INT;
                    value->integer = CollectionCount(item, *descriptor);
                    return 1;
                }

                void* const record = CollectionAt(item, *descriptor, index);
                return (record != nullptr && LoadProperty(record, *descriptor, *value)) ? 1 : 0;
            }

            if (index >= 0)
            {
                return 0;
            }

            return LoadProperty(item, *descriptor, *value) ? 1 : 0;
        }

        int __cdecl ApiSetProperty(AmodMod mod, AmodMenuItem item, const char* key,
                                   const AmodValue* value)
        {
            if (Resolve(mod) == nullptr || !IsLive(item) || value == nullptr)
            {
                return 0;
            }

            char base[64] = {};
            int index = -1;
            if (!SplitKey(key, base, sizeof(base), index))
            {
                return 0;
            }

            const PropertyDescriptor* const descriptor = FindDescriptor(item, base);
            if (descriptor == nullptr)
            {
                return 0;
            }

            if (descriptor->stride != 0)
            {
                if (!descriptor->writable)
                {
                    return 0;
                }

                if (index < 0)
                {
                    if ((value->kind != AMOD_VALUE_INT && value->kind != AMOD_VALUE_BOOL) ||
                        value->integer < 0 ||
                        value->integer > CollectionCount(item, *descriptor))
                    {
                        return 0;
                    }

                    *reinterpret_cast<int*>(Field(item, descriptor->countOffset)) = value->integer;
                    return 1;
                }

                void* const record = CollectionAt(item, *descriptor, index);
                return (record != nullptr && StoreProperty(record, *descriptor, *value)) ? 1 : 0;
            }

            if (index >= 0)
            {
                return 0;
            }

            return StoreProperty(item, *descriptor, *value) ? 1 : 0;
        }

        int __cdecl ApiHasProperty(AmodMod mod, AmodMenuItem item, const char* key)
        {
            if (Resolve(mod) == nullptr || !IsLive(item))
            {
                return 0;
            }

            char base[64] = {};
            int index = -1;
            if (!SplitKey(key, base, sizeof(base), index))
            {
                return 0;
            }

            const PropertyDescriptor* const descriptor = FindDescriptor(item, base);
            if (descriptor == nullptr)
            {
                return 0;
            }

            if (index >= 0)
            {
                return (descriptor->stride != 0 &&
                        CollectionAt(item, *descriptor, index) != nullptr) ? 1 : 0;
            }

            return 1;
        }

        int __cdecl ApiPropertyCount(AmodMod mod, AmodMenuItem item)
        {
            if (Resolve(mod) == nullptr || !IsLive(item))
            {
                return 0;
            }

            const uint32_t kind = KindBit(KindOf(item));

            int count = 0;
            for (const PropertyDescriptor& descriptor : g_properties)
            {
                if ((descriptor.kinds & kind) != 0)
                {
                    ++count;
                }
            }
            return count;
        }

        const char* __cdecl ApiPropertyNameAt(AmodMod mod, AmodMenuItem item, int index)
        {
            if (Resolve(mod) == nullptr || !IsLive(item) || index < 0)
            {
                return "";
            }

            const uint32_t kind = KindBit(KindOf(item));

            int seen = 0;
            for (const PropertyDescriptor& descriptor : g_properties)
            {
                if ((descriptor.kinds & kind) == 0)
                {
                    continue;
                }

                if (seen == index)
                {
                    return descriptor.key;
                }
                ++seen;
            }

            return "";
        }

        AmodMenuItem __cdecl ApiAddObject(AmodMod mod, const char* menuId, const char* fragment)
        {
            ModContext* const context = Resolve(mod);
            void* const menu = context != nullptr ? MenuByIdOrNull(menuId) : nullptr;
            if (menu == nullptr)
            {
                return nullptr;
            }

            int built = 0;
            void* const first = BuildFromFragment(menu, fragment, built);

            if (built == 0)
            {
                FORGE_LOG_WARN("[%s] built nothing from its fragment for menu '%s'",
                                  context->id.c_str(), menuId);
                return nullptr;
            }

            RecordMenu(menu);

            FORGE_LOG_INFO("[%s] added %d object(s) to menu '%s'",
                           context->id.c_str(), built, menuId);
            return first;
        }

        void* TableOrNull(AmodMenuItem item)
        {
            return IsLive(item) && KindOf(item) == ElementKind::Table ? item : nullptr;
        }

        int __cdecl ApiTableLineCount(AmodMod mod, AmodMenuItem table)
        {
            void* const object = Resolve(mod) != nullptr ? TableOrNull(table) : nullptr;

            return object != nullptr
                ? *reinterpret_cast<const int*>(Field(object, GameAddress::kTableMadeLineCountOffset))
                : 0;
        }

        int __cdecl ApiSetTableCell(AmodMod mod, AmodMenuItem table, int column, int line, const char* text)
        {
            void* const object = Resolve(mod) != nullptr ? TableOrNull(table) : nullptr;

            if (object == nullptr || g_tableSetCell == nullptr)
            {
                return 0;
            }

            // The routine checks both bounds itself and does nothing outside them, which is no
            // answer to a mod asking whether it landed.
            const int columns = *reinterpret_cast<const int*>(Field(object, GameAddress::kTableColumnCountOffset));
            const int lines = *reinterpret_cast<const int*>(Field(object, GameAddress::kTableMadeLineCountOffset));

            if (column < 0 || column >= columns || line < 0 || line >= lines)
            {
                return 0;
            }

            g_tableSetCell(object, nullptr, column, line, text);
            return 1;
        }

        int __cdecl ApiSetTableLineDots(AmodMod mod, AmodMenuItem table, int line, int dots)
        {
            void* const object = Resolve(mod) != nullptr ? TableOrNull(table) : nullptr;

            if (object == nullptr || g_tableSetDots == nullptr)
            {
                return 0;
            }

            if (line < 0 || line >= *reinterpret_cast<const int*>(
                    Field(object, GameAddress::kTableMadeLineCountOffset)))
            {
                return 0;
            }

            g_tableSetDots(object, nullptr, line, dots);
            return 1;
        }

        int __cdecl ApiSetTableLineStyle(AmodMod mod, AmodMenuItem table, int line, const float* colour, const float* scale)
        {
            void* const object = Resolve(mod) != nullptr ? TableOrNull(table) : nullptr;

            if (object == nullptr || g_tableSetStyle == nullptr)
            {
                return 0;
            }

            if (line < 0 || line >= *reinterpret_cast<const int*>(
                    Field(object, GameAddress::kTableMadeLineCountOffset)))
            {
                return 0;
            }

            g_tableSetStyle(object, nullptr, line, colour, scale);
            return 1;
        }

        void* __cdecl ApiGetRawPointer(AmodMod mod, AmodMenuItem item, uint32_t expectedGameBuild)
        {
            if (Resolve(mod) == nullptr || !IsLive(item) ||
                expectedGameBuild != AMOD_GAME_BUILD_AZANGARA_10)
            {
                return nullptr;
            }
            return item;
        }

        AmodObjectsMenuApi g_table = {};

        void __fastcall NewGameClickDetour(void* menu, void* unused, float x, float y)
        {
            static bool said = false;

            if (g_hovered != nullptr && g_hovered(menu, nullptr, 0) == nullptr)
            {
                if (!said)
                {
                    FORGE_LOG_WARN("A NEW GAME click landed on nothing, which that screen does "
                        "not survive on its own - the click is being dropped");
                    said = true;
                }
                if (g_menuBaseClick != nullptr)
                {
                    g_menuBaseClick(menu, nullptr, x, y);
                }

                return;
            }

            if (g_originalNewGameClick != nullptr)
            {
                g_originalNewGameClick(menu, unused, x, y);
            }
        }

        bool InstallOne(InlineHook& hook, GameSymbol symbol, void* detour, const uint8_t* prologue, size_t prologueLength)
        {
            void* const target = GameSymbols::Address(symbol);
            if (target == nullptr)
            {
                FORGE_LOG_ERROR("%s was not located, the menu interface is not available",
                                GameSymbols::Name(symbol));
                return false;
            }

            std::string mismatch;
            if (!GameModule::VerifyBytes(target, prologue, prologueLength, mismatch))
            {
                FORGE_LOG_ERROR("%s does not open with the instructions AzForge steals (%s)",
                                GameSymbols::Name(symbol), mismatch.c_str());
                return false;
            }

            std::string error;
            if (!hook.Install(target, detour, prologue, prologueLength, error))
            {
                FORGE_LOG_ERROR("Cannot hook %s: %s", GameSymbols::Name(symbol), error.c_str());
                return false;
            }

            return true;
        }
    }

    bool ObjectsMenuApi::Install()
    {
        if (g_parseHook.IsInstalled())
        {
            return true;
        }

        // A table without these is still a table: it shows what its script gave it, and a mod that
        // asks to write a line is told no.
        g_tableSetCell = reinterpret_cast<TableSetCellRoutine>(
            GameSymbols::Address(GameSymbol::MenuTableSetCell));
        g_tableSetDots = reinterpret_cast<TableSetDotsRoutine>(
            GameSymbols::Address(GameSymbol::MenuTableSetLineDots));
        g_tableSetStyle = reinterpret_cast<TableSetStyleRoutine>(
            GameSymbols::Address(GameSymbol::MenuTableSetLineStyle));

        g_findItem = reinterpret_cast<FindItemRoutine>(
            GameSymbols::Address(GameSymbol::MenuFindItemById));
        if (g_findItem == nullptr)
        {
            FORGE_LOG_ERROR("Menu_FindItemById was not located, the menu interface is not available");
            return false;
        }

        g_findMenu = reinterpret_cast<FindMenuRoutine>(
            GameSymbols::Address(GameSymbol::MenuFindByMenuId));

        g_parserCreate = reinterpret_cast<ParserCreateRoutine>(
            GameSymbols::Address(GameSymbol::ParserCreateFromBuffer));
        g_parserDestroy = reinterpret_cast<ParserDestroyRoutine>(
            GameSymbols::Address(GameSymbol::ParserDestroy));
        g_parserEnter = reinterpret_cast<ParserEnterBlockRoutine>(
            GameSymbols::Address(GameSymbol::ParserEnterBlock));
        g_parserNext = reinterpret_cast<ParserNextBlockRoutine>(
            GameSymbols::Address(GameSymbol::ParserNextBlock));
        g_parserLeave = reinterpret_cast<ParserLeaveBlockRoutine>(
            GameSymbols::Address(GameSymbol::ParserLeaveBlock));
        g_allocTracked = reinterpret_cast<AllocTrackedRoutine>(
            GameSymbols::Address(GameSymbol::MemAllocTracked));
        g_reallocTracked = reinterpret_cast<ReallocTrackedRoutine>(
            GameSymbols::Address(GameSymbol::MemReallocTracked));
        g_freeTracked = reinterpret_cast<FreeTrackedRoutine>(
            GameSymbols::Address(GameSymbol::MemFreeTracked));

        if (!CreationReady())
        {
            FORGE_LOG_WARN("Not every routine object creation needs was located, "
                              "so mods can read and write menus but not add to them");
        }

        if (!InstallOne(g_parseHook, GameSymbol::MenuParseScript,
                        reinterpret_cast<void*>(&ParseScriptDetour),
                        kMenuParseScriptPrologue, sizeof(kMenuParseScriptPrologue)) ||
            !InstallOne(g_loadHook, GameSymbol::MenuLoadFromScript,
                        reinterpret_cast<void*>(&LoadFromScriptDetour),
                        kMenuLoadFromScriptPrologue, sizeof(kMenuLoadFromScriptPrologue)) ||
            !InstallOne(g_registerHook, GameSymbol::MenuRegister,
                        reinterpret_cast<void*>(&RegisterDetour),
                        kMenuRegisterPrologue, sizeof(kMenuRegisterPrologue)))
        {
            Uninstall();
            return false;
        }

        g_originalParse = reinterpret_cast<ParseScriptRoutine>(g_parseHook.Trampoline());
        g_originalLoad = reinterpret_cast<LoadFromScriptRoutine>(g_loadHook.Trampoline());
        g_originalRegister = reinterpret_cast<RegisterRoutine>(g_registerHook.Trampoline());

        if (InstallOne(g_clickHook, GameSymbol::MenuTakeClickResult,
                       reinterpret_cast<void*>(&TakeClickResultDetour),
                       kMenuTakeClickResultPrologue, sizeof(kMenuTakeClickResultPrologue)))
        {
            g_originalTakeClick =
                reinterpret_cast<TakeClickResultRoutine>(g_clickHook.Trampoline());
        }
        else
        {
            FORGE_LOG_WARN("Menu_TakeClickResult was not hooked, so click events are off");
        }

        void* const newGameTarget = GameModule::Resolve(GameAddress::kNewGameOnClick);
        std::string newGameMismatch;
        std::string newGameError;

        g_menuBaseClick = reinterpret_cast<MenuClickRoutine>(GameModule::Resolve(GameAddress::kMenuOnClick));
        g_hovered = reinterpret_cast<HoveredInGroupRoutine>(GameModule::Resolve(GameAddress::kMenuHoveredInGroup));

        if (newGameTarget != nullptr && g_hovered != nullptr && GameModule::VerifyBytes(newGameTarget, kNewGameOnClickPrologue, sizeof(kNewGameOnClickPrologue), 
            newGameMismatch) && g_newGameHook.Install(newGameTarget, reinterpret_cast<void*>(&NewGameClickDetour), kNewGameOnClickPrologue, 
                sizeof(kNewGameOnClickPrologue), newGameError))
        {
            g_originalNewGameClick = reinterpret_cast<MenuClickRoutine>(g_newGameHook.Trampoline());
        }
        else
        {
            FORGE_LOG_WARN("The NEW GAME click is not guarded, so a click landing on nothing "
                "still crashes: %s", newGameError.empty() ? newGameMismatch.c_str() : newGameError.c_str());
        }

        g_table.structSize = sizeof(AmodObjectsMenuApi);
        g_table.Subscribe = &ApiSubscribe;
        g_table.Unsubscribe = &ApiUnsubscribe;
        g_table.FindItem = &ApiFindItem;
        g_table.GetItemCount = &ApiGetItemCount;
        g_table.GetItemAt = &ApiGetItemAt;
        g_table.GetItemId = &ApiGetItemId;
        g_table.GetProperty = &ApiGetProperty;
        g_table.SetProperty = &ApiSetProperty;
        g_table.HasProperty = &ApiHasProperty;
        g_table.PropertyCount = &ApiPropertyCount;
        g_table.PropertyNameAt = &ApiPropertyNameAt;
        g_table.GetRawPointer = &ApiGetRawPointer;
        g_table.AddObject = &ApiAddObject;
        g_table.TableLineCount = &ApiTableLineCount;
        g_table.SetTableCell = &ApiSetTableCell;
        g_table.SetTableLineDots = &ApiSetTableLineDots;
        g_table.SetTableLineStyle = &ApiSetTableLineStyle;

        ValidatePropertyTable();

        int kinds = 0;
        for (int k = 1; k <= static_cast<int>(ElementKind::StaticText); ++k)
        {
            for (const PropertyDescriptor& descriptor : g_properties)
            {
                if ((descriptor.kinds & (1u << k)) != 0)
                {
                    ++kinds;
                    break;
                }
            }
        }

        FORGE_LOG_INFO("Menu interface installed, %zu propert(ies) across %d kind(s)",
                       kPropertyCount, kinds);
        return true;
    }

    bool ObjectsMenuApi::AskMenu(const char* ownId, bool overlay)
    {
        if (ownId == nullptr)
        {
            return false;
        }

        if (g_inClick)
        {
            g_askedSub = overlay ? nullptr : ownId;
            g_askedBox = overlay ? ownId : nullptr;
            return true;
        }

        if (TopMenu() == nullptr)
        {
            return false;
        }

        g_menuLater = ownId;
        g_menuLaterOverlay = overlay;
        g_menuLaterAt = GetTickCount();
        return true;
    }

    bool ObjectsMenuApi::AskBack()
    {
        if (g_inClick)
        {
            g_askedBack = true;
            return true;
        }

        if (TopMenu() == nullptr)
        {
            return false;
        }

        g_backLater = true;
        g_backLaterAt = GetTickCount();
        return true;
    }

    void ObjectsMenuApi::Uninstall()
    {
        g_registerHook.Uninstall();
        g_loadHook.Uninstall();
        g_parseHook.Uninstall();
        g_clickHook.Uninstall();
        g_newGameHook.Uninstall();

        g_originalRegister = nullptr;
        g_originalLoad = nullptr;
        g_originalParse = nullptr;
        g_originalTakeClick = nullptr;
        g_originalNewGameClick = nullptr;
        g_menuBaseClick = nullptr;
        g_hovered = nullptr;
        g_findItem = nullptr;
        g_findMenu = nullptr;

        g_subscriptions.clear();
        g_menusById.clear();
        g_elementsByMenu.clear();
        g_validElements.clear();
        g_kindByElement.clear();
        g_parsingMenu = nullptr;
        g_lastParsedMenu = nullptr;
        g_table = {};
    }

    const AmodObjectsMenuApi* ObjectsMenuApi::Table()
    {
        return g_table.structSize != 0 ? &g_table : nullptr;
    }

    void ObjectsMenuApi::ForgetMod(ModContext* owner)
    {
        for (size_t i = g_subscriptions.size(); i-- > 0;)
        {
            if (g_subscriptions[i].owner == owner)
            {
                g_subscriptions.erase(g_subscriptions.begin() + static_cast<ptrdiff_t>(i));
            }
        }
    }
}
