#include "pch.h"

#include "src/Api/ObjectsMenuApi.h"

#include "src/Api/ModContext.h"
#include "src/Core/ForgeLog.h"
#include "src/Game/GameAddresses.h"
#include "src/Game/GameModule.h"
#include "src/Game/GameSymbols.h"
#include "src/Hook/InlineHook.h"

#include <map>
#include <set>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

namespace Forge
{
    namespace
    {
        typedef int(__fastcall* RegisterRoutine)(void* manager, void* unused, void* menu,
                                                 const char* scriptFile, const char* menuId,
                                                 int force);
        typedef int(__stdcall* LoadFromScriptRoutine)(const char* menuId, const char* scriptFile);
        typedef int(__fastcall* ParseScriptRoutine)(void* menu, void* unused, const char* scriptFile);
        typedef void*(__fastcall* FindItemRoutine)(void* menu, void* unused, const char* itemId);

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

        RegisterRoutine g_originalRegister = nullptr;
        LoadFromScriptRoutine g_originalLoad = nullptr;
        ParseScriptRoutine g_originalParse = nullptr;
        FindItemRoutine g_findItem = nullptr;

        std::map<std::string, void*> g_menusById;
        std::map<void*, std::vector<void*>> g_elementsByMenu;
        std::set<void*> g_validElements;

        void* g_parsingMenu = nullptr;
        void* g_lastParsedMenu = nullptr;

        std::vector<Subscription> g_subscriptions;
        uint32_t g_nextSubscriptionId = 1;

        // These game routines answer in AL and leave the rest of EAX holding whatever was
        // there, so only the low byte carries the result. The full value is still what gets
        // returned to the caller, untouched.
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

        // A handle is only followed when it is still one of the elements of a parsed menu.
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

        // Read out of the allocation the element lives in rather than watched for during
        // parsing: Menu_ParseScript @0x004527a0 builds each kind at a distinct size, and
        // Mem_AllocTracked @0x00421df0 records that size in its header.
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


        // --- geometry ------------------------------------------------------------------

        // Repeats what MenuButton_ParseCommon @0x0045a600 works out once the rectangle is
        // known, with the same two expressions. This is what MenuItem_Render @0x0045f620
        // actually draws at, so a rectangle changed without this changes nothing on screen.
        void RecomputeCentre(void* item)
        {
            const float* const rect = Floats(item, GameAddress::kMenuElementRectOffset);
            float* const centre = Floats(item, GameAddress::kMenuElementCentreOffset);

            centre[0] = rect[0] + (rect[2] - rect[0]) * 0.5f;
            centre[1] = rect[1] + (rect[3] - rect[1]) * 0.5f;
        }

        // Places the rectangle from the position, a size and the alignment flags, mirroring
        // FUN_0044a450 @0x0044a450 branch for branch.
        //
        // That routine is not called instead, because it is destructive: on entry it reads
        // the first two rectangle floats as a width and a height and replaces them with
        // edges, so running it a second time would read edges as a size. Reproducing it
        // here means the size can be supplied explicitly and the element re-placed as often
        // as a mod likes.
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

        // Which kinds carry a key. The same offset means different things from one kind to
        // the next - +0x758 is the drawn text on an item and a texture handle on an image
        // button - so this is what keeps a write from landing on the wrong field.
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

        // Everything that went through MenuButton_ParseCommon and therefore shares the
        // common layout. A decorative image is deliberately not one of them: it is a forty
        // byte record with a layout of its own, so a common key read from it would run off
        // the end.
        constexpr uint32_t kElementKinds =
            kItem | kLogo | kTextBtn | kImgBtn | kHotkeyBtn | kCastleBtn | kList | kSlider;

        // The same minus the logo, which builds its rectangle by hand and so takes its own
        // rows for every key that places an element.
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

            // Set only by a key that addresses a collection: "items" answers how many there
            // are, "items[2]" reaches one of them. The offset above is then the field's
            // place inside a record rather than inside the element, which is what lets four
            // keys share one array the way a table's columns do.
            uintptr_t arrayOffset;
            uintptr_t countOffset;
            size_t stride;      // zero on every key that is not a collection
        };

        // "items[3]" names one entry of a collection. Splits into the key the table holds
        // and the index to hand its accessor, and leaves the index negative when there is
        // no subscript at all.
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

        // Alignment decides how the rectangle hangs off the position, so changing it has to
        // place the element again. The size is what the element currently occupies, which is
        // the same thing $rect_size gave the parser.
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

            // The position at +0x5b8 is deliberately left alone: it is what the script asked
            // for, while the rectangle is what the element ended up occupying, and a caller
            // that sets the edges outright is speaking in the second of those.
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

            // What the parser does with the caption rectangle, whether or not the logo has
            // a $caption block to put there.
            float* const copy = Floats(item, GameAddress::kMenuElementCaptionRectOffset);
            for (int i = 0; i < 4; ++i)
            {
                copy[i] = rect[i];
            }

            // The parser leaves the drawing centre where MenuButton_ParseCommon put it, so
            // the game itself lets the two disagree. Bringing it along costs nothing if a
            // logo draws from its edges, and is the difference between a move showing and
            // not showing if it does not.
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

        // Every string capacity below is the distance to the next field the parser writes,
        // so none of them is a guess.
        constexpr PropertyDescriptor g_properties[] =
        {
            // key                  offset store          n  cap    kinds                          writable read        write
            { "kind",               0,     Store::String, 0, 0,     kAnyKind,     false,   &ReadKind,  nullptr },

            // Everything MenuButton_ParseCommon @0x0045a600 gives every element.
            //
            // The parser reads every string with a limit of 0x100 whatever field it fills,
            // so a long $id in a script overruns into what follows it. The capacities here
            // are the distance to the next field instead, stricter than the game is with
            // itself.
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
            // What $rect_size meant. The parser consumes that key and keeps only the edges
            // it produced, so this reads back the width and height those edges describe.
            { "rect_size",          0,     Store::Float,  2, 0,     kAlignedKinds,true,    &ReadSize,  &WriteSize },
            { "centre",             0x5C0, Store::Float,  2, 0,     kElementKinds,false,   nullptr,    nullptr },
            // 1 left, 2 right, 4 centred, 8 top, 0x20 centred vertically, as
            // Layout_ApplyAlignment @0x0044a450 reads them.
            { "align",              0x5C8, Store::Int,    0, 0,     kAlignedKinds,true,    nullptr,    &WriteAlign },
            { "font_id",            0x5F8, Store::Int,    0, 0,     kElementKinds,false,   nullptr,    nullptr },
            { "caption.text",       0x5FF, Store::String, 0, 0x101, kElementKinds,                 true,    nullptr,    nullptr },
            { "caption.offs",       0x700, Store::Float,  2, 0,     kElementKinds,true,    nullptr,    nullptr },
            { "caption.rect_len",   0x708, Store::Float,  1, 0,     kElementKinds,true,    nullptr,    nullptr },
            // Derived from $rect_len when the $caption block is there. A logo puts its own
            // edges here instead, which is the only thing it uses the field for.
            { "caption.rect",       0x2A4, Store::Float,  4, 0,     kElementKinds,true,    nullptr,    nullptr },
            { "caption.enabled",    0x2B4, Store::Bool,   0, 0,     kElementKinds,true,    nullptr,    nullptr },
            { "visible",            0x714, Store::Bool,   0, 0,     kElementKinds,true,    nullptr,    nullptr },

            // The drawn label. MenuItem_Parse and TextButton_Parse @0x0045ec60 both put it
            // at +0x758, and a hotkey button inherits it by running the text button parser.
            // An image button keeps a texture handle at that same offset, which is why this
            // is not offered to every kind.
            { "text",               0x758, Store::String, 0, 0x40,  kItem | kTextBtn | kHotkeyBtn, true,    nullptr,    nullptr },
            { "plank_width",        0x798, Store::Int,    0, 0,     kItem,                         true,    nullptr,    nullptr },
            // The $cursor3d block: whether the script gave one, and where the model sits.
            // Both are three floats, not two - this is the 3d cursor beside the item.
            { "cursor3d",           0x79C, Store::Bool,   0, 0,     kItem,                         true,    nullptr,    nullptr },
            { "cursor3d.orient",    0x7A4, Store::Float,  3, 0,     kItem,                         true,    nullptr,    nullptr },
            { "cursor3d.pos",       0x7B0, Store::Float,  3, 0,     kItem,                         true,    nullptr,    nullptr },

            // ImageButton_Parse @0x0045e700, which the castle button inherits.
            { "img_enabled",        0x758, Store::Int,    0, 0,     kImgBtn | kCastleBtn,          true,    nullptr,    nullptr },
            { "img_disabled",       0x75C, Store::Int,    0, 0,     kImgBtn | kCastleBtn,          true,    nullptr,    nullptr },
            { "horz_flip",          0x760, Store::Bool,   0, 0,     kImgBtn | kCastleBtn,          true,    nullptr,    nullptr },

            // MenuLogo_Parse @0x0045f2d0. A logo is a link rather than a button: activating
            // it opens website.url next to the executable. It carries two sprites and swaps
            // between them as the cursor comes and goes, so $sp2 is the focused one.
            //
            // Its geometry keys are its own because it does not use $align: the parser puts
            // the edges at $pos and extends them by $size, and keeps a copy of them at
            // +0x2a4. Writing "pos" or "size" here rebuilds both the way the parser does.
            { "sp1",                0x758, Store::Int,    0, 0,     kLogo,                         true,    nullptr,    nullptr },
            { "sp2",                0x75C, Store::Int,    0, 0,     kLogo,                         true,    nullptr,    nullptr },
            { "sprite",             0x760, Store::Int,    0, 0,     kLogo,                         true,    nullptr,    nullptr },
            { "two_sprites",        0x764, Store::Bool,   0, 0,     kLogo,                         true,    nullptr,    nullptr },
            { "swap_on_focus",      0x765, Store::Bool,   0, 0,     kLogo,                         true,    nullptr,    nullptr },
            { "pos",                0x5B8, Store::Float,  2, 0,     kLogo,                         true,    nullptr,    &WriteLogoPosition },
            { "size",               0x768, Store::Float,  2, 0,     kLogo,                         true,    nullptr,    &WriteLogoSize },
            // Present because MenuButton_ParseCommon filled it, and read back for what the
            // script said, but nothing draws by it on a logo.
            { "align",              0x5C8, Store::Int,    0, 0,     kLogo,                         true,    nullptr,    nullptr },

            // MenuList_Parse @0x0045cae0. A list is the horizontal chooser the options
            // screens are built from: an arrow, the current entry, an arrow.
            //
            // "items" answers how many entries there are, and "items[2]" reaches one of
            // them. Writing the count can only drop entries from the end; adding one is an
            // allocation, and belongs with creating objects rather than editing them.
            { "items",              0x00,  Store::String, 0, 0x40,  kList,                         true,    nullptr,    nullptr,    0x758, 0x75C, 0x48 },
            // How many entries fit across, which the parser works out from the width.
            { "columns",            0x764, Store::Int,    0, 0,     kList,                         true,    nullptr,    nullptr },
            // The three sub-buttons the list draws itself from, and the keys that work
            // them: VK_LEFT, space and VK_RIGHT as the parser bound them.
            { "left.rect",          0x7B4, Store::Float,  4, 0,     kList,                         true,    nullptr,    nullptr },
            { "left.key",           0x7C8, Store::Int,    0, 0,     kList,                         true,    nullptr,    nullptr },
            { "text.rect",          0x820, Store::Float,  4, 0,     kList,                         true,    nullptr,    nullptr },
            { "text.key",           0x834, Store::Int,    0, 0,     kList,                         true,    nullptr,    nullptr },
            { "right.rect",         0x88C, Store::Float,  4, 0,     kList,                         true,    nullptr,    nullptr },
            { "right.key",          0x8A0, Store::Int,    0, 0,     kList,                         true,    nullptr,    nullptr },

            // HotkeyButton_Parse @0x0045e410.
            { "help_key",           0xAB8, Store::String, 0, 0x100, kHotkeyBtn,                    true,    nullptr,    nullptr },

            // MenuSlider_Parse @0x0045b4b0.
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

            // $item_anim, laid out by ButtonAnim_Parse @0x004445e0. Five colours and five
            // scales, one pair for each state a button can be in.
            //
            // A castle button parses the block twice: once as the image button it is built
            // on, into "anim", and once as itself, into "anim_castle". The script fills both
            // from the same $item_anim, and each falls back to its own template out of
            // menu.cfg when there is none.
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

            // A $table, laid out by MenuTable_Parse @0x0045a0b0. It is not an element and
            // shares nothing with one: no position, no rectangle, no alignment. Its rows are
            // placed from $line_pos and $line_step, and its columns are a collection, so
            // "col.title" answers how many there are and "col.title[1]" names the second.
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

            // A decorative $image, laid out by FUN_00449950 @0x00449950. Forty bytes with
            // nothing in common with the elements above, which is why every key here is
            // gated to this kind alone.
            //
            // "pos" holds $offs instead when "is_offset" is set, exactly as the script chose.
            { "texture",            0x00,  Store::Int,    0, 0,     kImage,                        true,    nullptr,    nullptr },
            { "pos",                0x04,  Store::Float,  2, 0,     kImage,                        true,    nullptr,    nullptr },
            { "is_offset",          0x0C,  Store::Bool,   0, 0,     kImage,                        false,   nullptr,    nullptr },
            { "size",               0x10,  Store::Float,  2, 0,     kImage,                        true,    nullptr,    nullptr },
            { "horz_flip",          0x18,  Store::Bool,   0, 0,     kImage,                        true,    nullptr,    nullptr },
            { "vert_flip",          0x19,  Store::Bool,   0, 0,     kImage,                        true,    nullptr,    nullptr },
            { "horz_rept",          0x1C,  Store::Float,  1, 0,     kImage,                        true,    nullptr,    nullptr },
            { "vert_rept",          0x20,  Store::Float,  1, 0,     kImage,                        true,    nullptr,    nullptr },
            { "unreg_only",         0x24,  Store::Int,    0, 0,     kImage,                        true,    nullptr,    nullptr },

            // An $images_anim entry, from Menu_ParseAnimatedImages @0x0044b5c0. Twenty four
            // bytes, written field by field at the end of that routine: where it sits, how
            // big it is, $unreg_only, and the animation the $frames block built.
            { "pos",                0x00,  Store::Float,  2, 0,     kAnimImage,                    true,    nullptr,    nullptr },
            { "size",               0x08,  Store::Float,  2, 0,     kAnimImage,                    true,    nullptr,    nullptr },
            { "unreg_only",         0x10,  Store::Int,    0, 0,     kAnimImage,                    true,    nullptr,    nullptr },
            { "animation",          0x14,  Store::Int,    0, 0,     kAnimImage,                    true,    nullptr,    nullptr },

            // A $static_text entry, from Menu_ParseStaticText @0x0044b7e0. The same 0x470
            // byte block the castle button keeps for its own $text_castle.
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

        // A key may appear more than once when different kinds keep it at different offsets,
        // as pos and size do for a decorative image. That only works while the kind masks of
        // those rows are disjoint: lookup answers with the first row that matches, so an
        // overlap would silently resolve to whichever comes first in the table and read the
        // wrong offset. Checked once, because that is the mistake this table invites.
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

        // A collection is a pointer to records laid end to end with the number of them in a
        // field beside it. The game builds every one of them that way - the decorations on a
        // menu, the entries of a list, the columns of a table - because they all grow
        // through the same pair of helpers.
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

                // Keep looking rather than giving up here. A key can appear more than once,
                // for kinds that keep it at different offsets, so a row that does not apply
                // to this kind only rules out that row. Ending the search on it would hide
                // the row that does apply, which is what "pos" on an image would hit: the
                // common row is reached first and is not the one wanted.
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
            // Publish reaches here from both Menu_Register and Menu_LoadFromScript, and the
            // same menu can go through both. Dropping what was recorded before makes a
            // second pass replace the previous one instead of piling on top of it.
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

            // The decorative $images are not in the groups. The menu keeps them in a
            // collection of its own at +0x74: eight bytes of { record* items; int count; },
            // with the records laid out end to end, forty bytes each.
            //
            // Confirmed against a running game rather than inferred: the collection block
            // carries the ui_items.cpp:157 stamp Menu_ParseImages @0x0044b320 allocates it
            // with, and for the main menu the array block is 0x118 bytes for a count of 7,
            // which is exactly seven records of forty.
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

            // A $table is not in the groups either, and unlike the decorations it is an
            // allocation of its own: Menu_AddTable @0x00452780 keeps an array of pointers to
            // them rather than the tables themselves, so this walk is one level deeper.
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

            FORGE_LOG_INFO("Menu %p: %d element(s) - %d in groups, %d image, %d image_anim, "
                           "%d static, %d table", menu, static_cast<int>(elements.size()),
                           fromGroups, decorations[0], decorations[1], decorations[2],
                           fromTables);

            g_elementsByMenu[menu] = std::move(elements);
        }

        // --- building objects out of a script fragment ----------------------------------
        //
        // The game turns text into objects in one uniform shape, whatever the kind:
        //
        //     block = Mem_AllocTracked(size, file, line);
        //     Kind_Create(block);
        //     if (parse(block, parser)) Menu_AddElement(menu, block);
        //     else                      block->vtable[0x34](1);
        //
        // so creating one needs no new logic, only the parser pointed at the mod's text.
        // Everything a parser derives on the way - the alignment, the drawing centre, the
        // caption rectangle - therefore comes out exactly as it would from a menu_*.txt,
        // which is the whole reason this goes through text rather than assembling fields.

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

        // The nine rows Menu_ParseScript @0x004527a0 walks, in the same order. A table is on
        // the list now that its parser is confirmed to answer a bool and clean up its own
        // stack, but it is not an element: it takes Menu_AddTable and a different destructor.
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

        // The game disposes of an object whose block the parser refused through its scalar
        // deleting destructor, which frees the block as well as running it. The slot differs
        // by kind - 0x34 for an element, 0 for a table - so it comes from the descriptor.
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

        // Builds every element of one block into the menu. The parser is already sitting on
        // the container.
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

        // Kept apart from everything else because a function using __try may hold nothing
        // that needs unwinding. The guard is here for one case the fragment cannot be
        // inspected for: Menu_ParseAnimatedImages throws when its block is present but
        // yields no image at all.
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

            // The records were copied out of it, so the collection the parser made is ours
            // to release.
            if (g_freeTracked != nullptr)
            {
                g_freeTracked(fresh);
                g_freeTracked(parsed);
            }

            built += freshCount;
            return grown + before;
        }

        // Answers true when the fragment really carries this block, which is what keeps the
        // two "missing block" throws out of reach rather than caught.
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

        void* BuildFromFragment(void* menu, const char* fragment, int& built)
        {
            built = 0;

            if (!CreationReady() || menu == nullptr || fragment == nullptr ||
                fragment[0] == '\0')
            {
                return nullptr;
            }

            // Parser_CreateFromBuffer keeps the pointer it is given rather than a copy, and
            // strips the comments out of it in place first, so the text has to be writable
            // and has to outlive the parser. A mod's literal is neither.
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

            g_parserDestroy(parser);
            return first;
        }

        void Dispatch(AmodMenuEvent event, void* data)
        {
            const std::vector<Subscription> snapshot = g_subscriptions;
            for (const Subscription& item : snapshot)
            {
                if (item.event == event && item.callback != nullptr)
                {
                    item.callback(event, data, item.user);
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

        // --- detours -------------------------------------------------------------------

        // The id is read out of the object, never taken from the caller's argument: a menu
        // built from a script gets it from the $menu_id of its own $header, and the argument
        // is null in that case. Menu_Register has already copied the argument in by the time
        // this runs, so the object is right either way.
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
            // Rebuilding a menu frees whatever it held, so the old handles stop being live
            // before the new ones appear.
            ForgetMenu(menu);

            g_parsingMenu = menu;
            const int result = g_originalParse(menu, unused, scriptFile);
            g_parsingMenu = nullptr;

            // Nothing is read out of the object here. This routine also serves callers that
            // pass something other than a menu, and only Menu_Register tells the two apart.
            if (Succeeded(result))
            {
                g_lastParsedMenu = menu;
            }

            return result;
        }

        int __fastcall RegisterDetour(void* manager, void* unused, void* menu,
                                      const char* scriptFile, const char* menuId, int force)
        {
            const int result = g_originalRegister(manager, unused, menu, scriptFile, menuId, force);

            if (Succeeded(result))
            {
                Publish(menu);
            }

            return result;
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
            return found == g_menusById.end() ? nullptr : found->second;
        }

        // --- the table the mods call through -------------------------------------------

        AmodToken __cdecl ApiSubscribe(AmodMod mod, AmodMenuEvent event,
                                       AmodMenuCallback callback, void* user)
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

        AmodMenuItem __cdecl ApiFindItem(AmodMod mod, const char* menuId, const char* itemId)
        {
            void* const menu = Resolve(mod) != nullptr ? MenuByIdOrNull(menuId) : nullptr;
            if (menu == nullptr || itemId == nullptr || g_findItem == nullptr)
            {
                return nullptr;
            }

            void* const element = g_findItem(menu, nullptr, itemId);
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
            // Answered through the table rather than from a fixed offset: a static text
            // entry also declares an $id, but keeps it somewhere else entirely, and a kind
            // with no id at all has to answer an empty string rather than hand back
            // whatever bytes follow a shorter record.
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

            // A subscript on a key that names one field and not a collection of them is a
            // mistake rather than something to quietly ignore.
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

                // Writing the count can only drop records from the end. Making one means
                // reallocating the array, which is creating an object rather than editing
                // one, so a larger count is refused outright rather than half honoured.
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

            // A subscript is only there if that record is, so a mod can walk a collection
            // until it runs out instead of asking for the count first.
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

            // Menu_AddElement put them in a group, so walking the menu again is all it takes
            // for them to become handles the property interface accepts. Without this it
            // would refuse the very handle it just returned.
            RecordMenu(menu);

            FORGE_LOG_INFO("[%s] added %d object(s) to menu '%s'",
                           context->id.c_str(), built, menuId);
            return first;
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

        bool InstallOne(InlineHook& hook, GameSymbol symbol, void* detour,
                        const uint8_t* prologue, size_t prologueLength)
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

        g_findItem = reinterpret_cast<FindItemRoutine>(
            GameSymbols::Address(GameSymbol::MenuFindItemById));
        if (g_findItem == nullptr)
        {
            FORGE_LOG_ERROR("Menu_FindItemById was not located, the menu interface is not available");
            return false;
        }

        // Object creation is a separate capability: it calls into the game rather than
        // detouring it, so a build where one of these cannot be located still gets a working
        // interface for everything a menu already holds.
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

        ValidatePropertyTable();

        // Counted rather than assumed: a kind with no row of its own is one the interface
        // does not actually reach, whatever the enumeration says.
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

    void ObjectsMenuApi::Uninstall()
    {
        g_registerHook.Uninstall();
        g_loadHook.Uninstall();
        g_parseHook.Uninstall();

        g_originalRegister = nullptr;
        g_originalLoad = nullptr;
        g_originalParse = nullptr;
        g_findItem = nullptr;

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
