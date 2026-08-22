#include <windows.h>

#include "src/Mods/ModScreen.h"

#include "src/Api/ConsoleApi.h"
#include "src/Api/InputApi.h"
#include "src/Api/ModManagerApi.h"
#include "src/Api/RenderApi.h"
#include "src/Core/ForgeBoot.h"
#include "src/Core/ForgeLog.h"
#include "src/Core/ForgeText.h"
#include "src/Game/GameAddresses.h"
#include "src/Game/GameModule.h"
#include "src/Mods/ModFileSystem.h"
#include "src/Mods/ModHost.h"
#include "src/Mods/ModState.h"

#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

namespace Forge
{
    namespace
    {
        using namespace GameAddress;

        // The device is COM: every call is __stdcall with the interface first.
        typedef long(__stdcall* SetRenderStateRoutine)(void* device, uint32_t state, uint32_t value);
        typedef long(__stdcall* GetRenderStateRoutine)(void* device, uint32_t state, uint32_t* value);
        typedef long(__stdcall* SetStageStateRoutine)(void* device, uint32_t stage, uint32_t type, uint32_t value);
        typedef long(__stdcall* GetStageStateRoutine)(void* device, uint32_t stage, uint32_t type, uint32_t* value);
        typedef long(__stdcall* SetTextureRoutine)(void* device, uint32_t stage, void* texture);

        typedef void(__fastcall* BindTextureRoutine)(void* manager, void* unused, void* texture);
        typedef long(__stdcall* SetVertexShaderRoutine)(void* device, uint32_t handle);
        typedef long(__stdcall* DrawPrimitiveUpRoutine)(void* device, uint32_t primitive, uint32_t count, const void* data, uint32_t stride);

        // The game's own texture loader, which reads through Package_OpenFile
        typedef uint8_t*(__fastcall* LoadTextureRoutine)(void* manager, void* unused, const char* name, int mipmapped, int hasAlpha);
        typedef void(__fastcall* ReleaseTextureRoutine)(void* manager, void* unused, void* texture);

        // Font_DrawText and Font_Measure, __thiscall with the font in ECX.
        typedef void(__fastcall* DrawTextRoutine)(void* font, void* unused, const char* text, const float* position, const float* scale, const float* colour, int length, float fixedWidth);
        typedef void(__fastcall* MeasureRoutine)(void* font, void* unused, const char* text, float* out, int length);

        constexpr uint32_t kFvfTransformed = 0x44;  // XYZRHW | DIFFUSE, the ortho does not touch it
        constexpr uint32_t kFvfTextured = 0x144;    // the same with one set of uv
        constexpr uint32_t kPrimitiveFan = 6;

        constexpr uint32_t kStateZEnable = 7;
        constexpr uint32_t kStateZWrite = 14;
        constexpr uint32_t kStateAlphaTest = 15;
        constexpr uint32_t kStateSrcBlend = 19;
        constexpr uint32_t kStateDestBlend = 20;
        constexpr uint32_t kStateCull = 22;
        constexpr uint32_t kStateAlphaBlend = 27;
        constexpr uint32_t kStateFog = 28;
        constexpr uint32_t kStateLighting = 137;

        constexpr uint32_t kStageColourOp = 1;
        constexpr uint32_t kStageColourArg1 = 2;
        constexpr uint32_t kStageColourArg2 = 3;
        constexpr uint32_t kStageAlphaOp = 4;
        constexpr uint32_t kStageAlphaArg1 = 5;
        constexpr uint32_t kStageAlphaArg2 = 6;

        constexpr uint32_t kOpSelectArg1 = 2;
        constexpr uint32_t kOpModulate = 4;
        constexpr uint32_t kArgDiffuse = 0;
        constexpr uint32_t kArgTexture = 2;

        const uint32_t kSavedStates[] = { kStateZEnable, kStateZWrite, kStateAlphaTest, kStateSrcBlend,
                                          kStateDestBlend, kStateCull, kStateAlphaBlend, kStateFog,
                                          kStateLighting };
        const uint32_t kSavedStages[] = { kStageColourOp, kStageColourArg1, kStageColourArg2,
                                          kStageAlphaOp, kStageAlphaArg1, kStageAlphaArg2 };

        constexpr int kColumns = 3;
        constexpr int kRows = 2;
        constexpr int kPerPage = kColumns * kRows;

        constexpr float kTileSize = 96.0f;
        constexpr float kTileTall = 112.0f;
        constexpr float kIconSize = 56.0f;
        constexpr float kTileStepX = 120.0f;
        constexpr float kTileStepY = 138.0f;
        constexpr float kGridX = 40.0f;
        constexpr float kGridY = 88.0f;

        constexpr float kDetailX = 392.0f;
        constexpr float kDetailWide = 198.0f;
        constexpr float kButtonY = 398.0f;
        constexpr float kButtonTall = 32.0f;

        // fnt_sys2's cells are twice as wide as the letters in them; the game's own text halves x.
        constexpr float kSqueeze = 0.5f;

        enum Status
        {
            kStatusOff,
            kStatusOn,
            kStatusFiles,
            kStatusFailed,
            kStatusRestart
        };

        bool g_open = false;
        int g_selected = 0;
        int g_hover = -1;
        bool g_wasDown[256] = {};
        bool g_heldInput = false;
        uint8_t g_swallowWas = 0;

        void* g_device = nullptr;      // held for one draw only
        float g_across = 1.0f;
        float g_down = 1.0f;

        DrawTextRoutine g_drawText = nullptr;
        MeasureRoutine g_measure = nullptr;

        struct Icon
        {
            std::string id;
            void* texture;      // null when the mod has none; remembered so it is asked for once
        };

        std::vector<Icon> g_iconsHeld;

        const std::vector<ModRecord>& Records()
        {
            return Boot::Registry().LoadOrder();
        }

        bool InForce(const ModRecord& record)
        {
            return record.manifest.entry.empty() ? ModState::IsEnabled(record.manifest.id)
                                                 : ModHost::IsLoaded(record.manifest.id);
        }

        Status StatusOf(const ModRecord& record)
        {
            const bool wanted = ModState::IsEnabled(record.manifest.id);
            const char* const failure = ModHost::FailureFor(record.manifest.id);

            if (wanted != InForce(record))
            {
                return (wanted && failure != nullptr && failure[0] != '\0') ? kStatusFailed : kStatusRestart;
            }

            if (!wanted)
            {
                return kStatusOff;
            }

            return record.manifest.entry.empty() ? kStatusFiles : kStatusOn;
        }

        const char* StatusWord(Status status)
        {
            static const char* const words[] = { "OFF", "LOADED", "FILES", "FAILED", "RESTART" };
            return words[status];
        }

        uint32_t StatusColour(Status status)
        {
            static const uint32_t colours[] = { 0xFF909098, 0xFF80E080, 0xFF80C0E0, 0xFFE06060,
                                                0xFFE0C060 };
            return colours[status];
        }

        void* Manager()
        {
            void** const slot = static_cast<void**>(GameModule::Resolve(kRenderManagerPtr));
            return slot != nullptr ? *slot : nullptr;
        }

        void* Device()
        {
            void* const manager = Manager();

            if (manager == nullptr || *(static_cast<char*>(manager) + kRenderDeviceLostOffset) != 0)
            {
                return nullptr;
            }

            return *reinterpret_cast<void**>(static_cast<char*>(manager) + kRenderDeviceOffset);
        }

        void* Font()
        {
            void* const font = GameModule::Resolve(kSystemFont);

            return (font != nullptr && *reinterpret_cast<void**>(static_cast<uint8_t*>(font) + kFontTexturesOffset) != nullptr)
                       ? font
                       : nullptr;
        }

        HWND Window()
        {
            HWND* const slot = static_cast<HWND*>(GameModule::Resolve(kGameWindow));
            return slot != nullptr ? *slot : nullptr;
        }

        template <typename T>
        T Slot(void* object, uintptr_t offset)
        {
            void** const vtable = *static_cast<void***>(object);
            return reinterpret_cast<T>(vtable[offset / sizeof(void*)]);
        }

        void ReadViewport()
        {
            void* const manager = Manager();

            g_across = 1.0f;
            g_down = 1.0f;

            if (manager == nullptr)
            {
                return;
            }

            const uint8_t* const info = static_cast<const uint8_t*>(manager) + kRenderViewportInfoOffset;
            const int wide = *reinterpret_cast<const int*>(info + kRenderViewportWidthOffset);
            const int tall = *reinterpret_cast<const int*>(info + kRenderViewportHeightOffset);

            if (wide > 0 && tall > 0)
            {
                g_across = static_cast<float>(wide) / AMOD_MENU_WIDE;
                g_down = static_cast<float>(tall) / AMOD_MENU_TALL;
            }
        }

        struct Vertex
        {
            float x;
            float y;
            float z;
            float rhw;
            uint32_t colour;
        };

        // Transformed vertices: no state blocks, no scene, nothing but the device.
        void Quad(float x, float y, float w, float h, uint32_t colour)
        {
            if (g_device == nullptr || w <= 0.0f || h <= 0.0f)
            {
                return;
            }

            const float left = x * g_across;
            const float top = y * g_down;
            const float right = (x + w) * g_across;
            const float bottom = (y + h) * g_down;

            const Vertex quad[4] =
            {
                { left,  top,    0.0f, 1.0f, colour },
                { right, top,    0.0f, 1.0f, colour },
                { right, bottom, 0.0f, 1.0f, colour },
                { left,  bottom, 0.0f, 1.0f, colour },
            };

            Slot<DrawPrimitiveUpRoutine>(g_device, kDeviceDrawPrimitiveUP)(g_device, kPrimitiveFan, 2, quad, sizeof(Vertex));
        }

        void Frame(float x, float y, float w, float h, uint32_t colour, float thickness)
        {
            Quad(x, y, w, thickness, colour);
            Quad(x, y + h - thickness, w, thickness, colour);
            Quad(x, y + thickness, thickness, h - thickness * 2.0f, colour);
            Quad(x + w - thickness, y + thickness, thickness, h - thickness * 2.0f, colour);
        }

        struct TexturedVertex
        {
            float x;
            float y;
            float z;
            float rhw;
            uint32_t colour;
            float u;
            float v;
        };

        void Bind(void* texture)
        {
            void* const manager = Manager();

            if (manager != nullptr)
            {
                Slot<BindTextureRoutine>(manager, kRenderSetTexture)(manager, nullptr, texture);
            }
        }

        // Drawn in the text pass: the stage that lets a texture through is the same one.
        void Sprite(void* texture, float x, float y, float w, float h)
        {
            if (g_device == nullptr || texture == nullptr)
            {
                return;
            }

            const float left = x * g_across;
            const float top = y * g_down;
            const float right = (x + w) * g_across;
            const float bottom = (y + h) * g_down;

            const TexturedVertex quad[4] =
            {
                { left,  top,    0.0f, 1.0f, 0xFFFFFFFF, 0.0f, 0.0f },
                { right, top,    0.0f, 1.0f, 0xFFFFFFFF, 1.0f, 0.0f },
                { right, bottom, 0.0f, 1.0f, 0xFFFFFFFF, 1.0f, 1.0f },
                { left,  bottom, 0.0f, 1.0f, 0xFFFFFFFF, 0.0f, 1.0f },
            };

            if (*reinterpret_cast<void**>(static_cast<uint8_t*>(texture) + kTextureSurfaceOffset) == nullptr)
            {
                return;
            }

            Bind(texture);
            Slot<SetVertexShaderRoutine>(g_device, kDeviceSetVertexShader)(g_device, kFvfTextured);
            Slot<DrawPrimitiveUpRoutine>(g_device, kDeviceDrawPrimitiveUP)(g_device, kPrimitiveFan, 2, quad, sizeof(TexturedVertex));
        }

        void* IconFor(const ModRecord& record)
        {
            for (const Icon& held : g_iconsHeld)
            {
                if (held.id == record.manifest.id)
                {
                    return held.texture;
                }
            }

            const std::string resource = ModFileSystem::IconResource(record);
            const LoadTextureRoutine load = reinterpret_cast<LoadTextureRoutine>(GameModule::Resolve(kLoadOrFindTexture));
            void* const manager = GameModule::Resolve(kTextureManager);
            void* texture = nullptr;

            if (!resource.empty() && load != nullptr && manager != nullptr)
            {
                const bool opaque = resource.size() > 4 && resource.compare(resource.size() - 4, 4, ".bmp") == 0;

                texture = load(manager, nullptr, resource.c_str(), 0, opaque ? 0 : 1);

                if (texture == nullptr)
                {
                    FORGE_LOG_WARN("The icon of '%s' was not loaded", record.manifest.id.c_str());
                }
            }

            g_iconsHeld.push_back(Icon{ record.manifest.id, texture });
            return texture;
        }

        void ReleaseIcons()
        {
            const ReleaseTextureRoutine release = reinterpret_cast<ReleaseTextureRoutine>(GameModule::Resolve(kTextureRelease));
            void* const manager = GameModule::Resolve(kTextureManager);

            for (const Icon& held : g_iconsHeld)
            {
                if (held.texture != nullptr && release != nullptr && manager != nullptr)
                {
                    release(manager, nullptr, held.texture);
                }
            }

            g_iconsHeld.clear();
        }

        float LineHeight(float scale)
        {
            void* const font = Font();
            const float height = font != nullptr
                    ? *reinterpret_cast<float*>(static_cast<uint8_t*>(font) + kFontLineHeightOffset)
                    : 16.0f;

            return (height > 0.0f ? height : 16.0f) * scale;
        }

        float Measure(const char* text, float scale)
        {
            void* const font = Font();
            float out[2] = {};

            if (g_measure != nullptr && font != nullptr && text != nullptr)
            {
                g_measure(font, nullptr, text, out, 0);
            }

            return out[0] * scale * kSqueeze;
        }

        void Text(float x, float y, float scale, uint32_t colour, const char* text)
        {
            void* const font = Font();

            if (g_drawText == nullptr || font == nullptr || text == nullptr || text[0] == '\0')
            {
                return;
            }

            const float position[2] = { x * g_across, (AMOD_MENU_TALL - y - LineHeight(scale)) * g_down };
            const float scaling[2] = { scale * kSqueeze * g_across, scale * g_down };
            const float rgba[4] =
            {
                ((colour >> 16) & 0xFF) / 255.0f,
                ((colour >> 8) & 0xFF) / 255.0f,
                (colour & 0xFF) / 255.0f,
                ((colour >> 24) & 0xFF) / 255.0f,
            };

            g_drawText(font, nullptr, text, position, scaling, rgba, 0, 0.0f);
        }

        void TextCentred(float centre, float y, float scale, uint32_t colour, const char* text)
        {
            Text(centre - Measure(text, scale) * 0.5f, y, scale, colour, text);
        }

        // Broken on spaces, and on the letter when one word is wider than the box.
        std::vector<std::string> Wrap(const std::string& text, float scale, float wide, size_t limit)
        {
            std::vector<std::string> lines;
            std::string line;

            for (size_t at = 0; at <= text.size() && lines.size() < limit; ++at)
            {
                const bool end = at == text.size();
                const char letter = end ? ' ' : text[at];

                if (letter == '\n' || end)
                {
                    lines.push_back(line);
                    line.clear();
                    continue;
                }

                const std::string grown = line.empty() ? std::string(1, letter) : line + letter;

                if (Measure(grown.c_str(), scale) <= wide)
                {
                    line = grown;
                    continue;
                }

                const size_t space = line.find_last_of(' ');

                if (space == std::string::npos)
                {
                    lines.push_back(line);
                    line = std::string(1, letter);
                    continue;
                }

                lines.push_back(line.substr(0, space));
                line = line.substr(space + 1) + letter;
            }

            return lines;
        }

        struct SavedStates
        {
            uint32_t state[sizeof(kSavedStates) / sizeof(kSavedStates[0])];
            uint32_t stage[sizeof(kSavedStages) / sizeof(kSavedStages[0])];
        };

        SavedStates g_saved = {};

        void BeginDrawing()
        {
            const SetRenderStateRoutine set = Slot<SetRenderStateRoutine>(g_device, kDeviceSetRenderState);
            const GetRenderStateRoutine get = Slot<GetRenderStateRoutine>(g_device, kDeviceGetRenderState);
            const SetStageStateRoutine setStage = Slot<SetStageStateRoutine>(g_device, kDeviceSetStageState);
            const GetStageStateRoutine getStage = Slot<GetStageStateRoutine>(g_device, kDeviceGetStageState);

            for (size_t at = 0; at < sizeof(kSavedStates) / sizeof(kSavedStates[0]); ++at)
            {
                get(g_device, kSavedStates[at], &g_saved.state[at]);
            }
            for (size_t at = 0; at < sizeof(kSavedStages) / sizeof(kSavedStages[0]); ++at)
            {
                getStage(g_device, 0, kSavedStages[at], &g_saved.stage[at]);
            }

            set(g_device, kStateZEnable, 0);
            set(g_device, kStateZWrite, 0);
            set(g_device, kStateAlphaTest, 0);
            set(g_device, kStateCull, 1);
            set(g_device, kStateFog, 0);
            set(g_device, kStateLighting, 0);
            set(g_device, kStateAlphaBlend, 1);
            set(g_device, kStateSrcBlend, 5);
            set(g_device, kStateDestBlend, 6);

            // No texture, so the panels take their colour from the vertex and nothing else.
            Bind(nullptr);
            setStage(g_device, 0, kStageColourOp, kOpSelectArg1);
            setStage(g_device, 0, kStageColourArg1, kArgDiffuse);
            setStage(g_device, 0, kStageAlphaOp, kOpSelectArg1);
            setStage(g_device, 0, kStageAlphaArg1, kArgDiffuse);

            Slot<SetVertexShaderRoutine>(g_device, kDeviceSetVertexShader)(g_device, kFvfTransformed);
        }

        void BeginText()
        {
            const SetStageStateRoutine setStage = Slot<SetStageStateRoutine>(g_device, kDeviceSetStageState);

            setStage(g_device, 0, kStageColourOp, kOpModulate);
            setStage(g_device, 0, kStageColourArg1, kArgTexture);
            setStage(g_device, 0, kStageColourArg2, kArgDiffuse);
            setStage(g_device, 0, kStageAlphaOp, kOpModulate);
            setStage(g_device, 0, kStageAlphaArg1, kArgTexture);
            setStage(g_device, 0, kStageAlphaArg2, kArgDiffuse);
        }

        void EndDrawing()
        {
            const SetRenderStateRoutine set = Slot<SetRenderStateRoutine>(g_device, kDeviceSetRenderState);
            const SetStageStateRoutine setStage = Slot<SetStageStateRoutine>(g_device, kDeviceSetStageState);

            for (size_t at = 0; at < sizeof(kSavedStates) / sizeof(kSavedStates[0]); ++at)
            {
                set(g_device, kSavedStates[at], g_saved.state[at]);
            }
            for (size_t at = 0; at < sizeof(kSavedStages) / sizeof(kSavedStages[0]); ++at)
            {
                setStage(g_device, 0, kSavedStages[at], g_saved.stage[at]);
            }
        }

        void HoldGameInput(bool hold)
        {
            uint8_t* const input = static_cast<uint8_t*>(GameModule::Resolve(kInput));

            if (input == nullptr)
            {
                return;
            }

            if (hold && !g_heldInput)
            {
                g_swallowWas = *(input + kInputSwallowingOffset);
                g_heldInput = true;
            }

            *(input + kInputSwallowingOffset) = hold ? 1 : g_swallowWas;

            if (!hold)
            {
                g_heldInput = false;
            }
        }

        void Open(bool open)
        {
            if (g_open == open)
            {
                return;
            }

            g_open = open;
            g_hover = -1;
            HoldGameInput(open);
            InputApi::HoldSystemCursor(open);

            if (open && Font() == nullptr)
            {
                FORGE_LOG_WARN("The mod screen is open, but the game's system font is not loaded "
                               "yet, so it has no text to draw");
            }
        }

        bool Focused()
        {
            const HWND window = Window();
            return window != nullptr && GetForegroundWindow() == window;
        }

        bool Pressed(int key)
        {
            const bool down = (GetAsyncKeyState(key) & 0x8000) != 0;
            const bool edge = down && !g_wasDown[key & 0xFF];

            g_wasDown[key & 0xFF] = down;
            return edge;
        }

        bool Cursor(float& x, float& y)
        {
            const HWND window = Window();
            POINT point = {};
            RECT client = {};

            if (window == nullptr || GetCursorPos(&point) == 0 ||
                ScreenToClient(window, &point) == 0 || GetClientRect(window, &client) == 0 ||
                client.right <= 0 || client.bottom <= 0)
            {
                return false;
            }

            x = static_cast<float>(point.x) / client.right * AMOD_MENU_WIDE;
            y = static_cast<float>(point.y) / client.bottom * AMOD_MENU_TALL;
            return true;
        }

        bool Inside(float x, float y, float left, float top, float wide, float tall)
        {
            return x >= left && x < left + wide && y >= top && y < top + tall;
        }

        void TilePlace(int at, float& x, float& y)
        {
            x = kGridX + (at % kColumns) * kTileStepX;
            y = kGridY + (at / kColumns) * kTileStepY;
        }

        void Toggle(int index)
        {
            const std::vector<ModRecord>& records = Records();
            const ModRecord* const record = (index >= 0 && index < static_cast<int>(records.size())) ? &records[index] : nullptr;

            if (record == nullptr)
            {
                return;
            }

            const bool wanted = !ModState::IsEnabled(record->manifest.id);
            std::string error;

            if (!ModManagerApi::SetEnabled(index, wanted, error))
            {
                FORGE_LOG_WARN("'%s' was not switched %s: %s", record->manifest.id.c_str(), wanted ? "on" : "off", error.c_str());
            }
        }

        void Keys()
        {
            const int count = static_cast<int>(Records().size());
            const bool focused = Focused();

            const bool toggle = Pressed(VK_F10);
            const bool escape = Pressed(VK_ESCAPE);
            const bool left = Pressed(VK_LEFT);
            const bool right = Pressed(VK_RIGHT);
            const bool up = Pressed(VK_UP);
            const bool down = Pressed(VK_DOWN);
            const bool enter = Pressed(VK_RETURN) || Pressed(VK_SPACE);
            const bool pageUp = Pressed(VK_PRIOR);
            const bool pageDown = Pressed(VK_NEXT);
            const bool click = Pressed(VK_LBUTTON);
            const int wheel = InputApi::WheelDelta();

            if (!focused)
            {
                return;
            }

            if (toggle)
            {
                Open(!g_open);
            }

            if (!g_open)
            {
                return;
            }

            if (escape)
            {
                Open(false);
                return;
            }

            if (count == 0)
            {
                return;
            }

            g_selected += right ? 1 : 0;
            g_selected -= left ? 1 : 0;
            g_selected += down ? kColumns : 0;
            g_selected -= up ? kColumns : 0;
            g_selected += pageDown ? kPerPage : 0;
            g_selected -= pageUp ? kPerPage : 0;
            g_selected -= wheel * kColumns;      // a notch is a row

            g_selected = g_selected < 0 ? 0 : g_selected;
            g_selected = g_selected >= count ? count - 1 : g_selected;

            float mouseX = 0.0f;
            float mouseY = 0.0f;
            const int page = g_selected / kPerPage;

            g_hover = -1;

            if (Cursor(mouseX, mouseY))
            {
                for (int at = 0; at < kPerPage && page * kPerPage + at < count; ++at)
                {
                    float x = 0.0f;
                    float y = 0.0f;

                    TilePlace(at, x, y);

                    if (Inside(mouseX, mouseY, x, y, kTileSize, kTileTall))
                    {
                        g_hover = page * kPerPage + at;
                    }
                }

                if (click && g_hover >= 0)
                {
                    g_selected = g_hover;
                }
                else if (click && Inside(mouseX, mouseY, kDetailX, kButtonY, kDetailWide, kButtonTall))
                {
                    Toggle(g_selected);
                }
            }

            if (enter)
            {
                Toggle(g_selected);
            }
        }

        void DrawTileBox(const ModRecord& record, int index, float x, float y)
        {
            const bool chosen = index == g_selected;
            const bool under = index == g_hover;

            (void)record;

            Quad(x, y, kTileSize, kTileTall, chosen ? 0xFF263048 : (under ? 0xFF1E2638 : 0xFF161C28));
            Frame(x, y, kTileSize, kTileTall, chosen ? 0xFFFFD070 : (under ? 0xFF909CB0 : 0xFF606C80), 2.0f);
        }

        void DrawTileText(const ModRecord& record, int index, float x, float y, bool hasIcon)
        {
            const Status status = StatusOf(record);
            const bool chosen = index == g_selected;
            const float scale = hasIcon ? 0.55f : 0.6f;

            // Without an icon the name has the whole tile to itself.
            float line = hasIcon ? y + kIconSize + 12.0f : y + 14.0f;

            for (const std::string& text : Wrap(record.manifest.name, scale, kTileSize - 10.0f, hasIcon ? 2 : 3))
            {
                TextCentred(x + kTileSize * 0.5f, line, scale, chosen ? 0xFFFFFFFF : 0xFFC8CCD8, text.c_str());
                line += LineHeight(scale);
            }

            TextCentred(x + kTileSize * 0.5f, y + kTileTall - 18.0f, 0.5f, StatusColour(status), StatusWord(status));
        }

        void DrawDetails(const ModRecord& record)
        {
            const ModManifest& manifest = record.manifest;
            const Status status = StatusOf(record);
            const float centre = kDetailX + kDetailWide * 0.5f;
            const float scale = 0.6f;
            char line[192];

            float y = 96.0f;

            for (const std::string& text : Wrap(manifest.name, 0.85f, kDetailWide - 16.0f, 2))
            {
                TextCentred(centre, y, 0.85f, 0xFFFFD070, text.c_str());
                y += LineHeight(0.85f);
            }

            y += 4.0f;
            TextCentred(centre, y, scale, StatusColour(status), StatusWord(status));
            y += LineHeight(scale) * 1.6f;

            snprintf(line, sizeof(line), "id      %s", manifest.id.c_str());
            Text(kDetailX + 8.0f, y, scale, 0xFFC8CCD8, line);
            y += LineHeight(scale);

            snprintf(line, sizeof(line), "version %s", manifest.version.c_str());
            Text(kDetailX + 8.0f, y, scale, 0xFFC8CCD8, line);
            y += LineHeight(scale);

            if (!manifest.author.empty())
            {
                snprintf(line, sizeof(line), "author  %s", manifest.author.c_str());
                Text(kDetailX + 8.0f, y, scale, 0xFFC8CCD8, line);
                y += LineHeight(scale);
            }

            snprintf(line, sizeof(line), "api     %d", manifest.apiVersion);
            Text(kDetailX + 8.0f, y, scale, 0xFFC8CCD8, line);
            y += LineHeight(scale);

            snprintf(line, sizeof(line), "file    %s",
                     PathToUtf8(record.archivePath.filename()).c_str());
            Text(kDetailX + 8.0f, y, scale, 0xFFC8CCD8, line);
            y += LineHeight(scale) * 1.4f;

            std::string capabilities;
            for (const std::string& capability : manifest.capabilities)
            {
                capabilities += capabilities.empty() ? capability : ", " + capability;
            }

            if (!capabilities.empty())
            {
                for (const std::string& text : Wrap(capabilities, 0.55f, kDetailWide - 16.0f, 3))
                {
                    Text(kDetailX + 8.0f, y, 0.55f, 0xFF80C0E0, text.c_str());
                    y += LineHeight(0.55f);
                }
                y += 6.0f;
            }

            for (const std::string& text : Wrap(manifest.description, 0.55f, kDetailWide - 16.0f, 8))
            {
                Text(kDetailX + 8.0f, y, 0.55f, 0xFFB0B8C8, text.c_str());
                y += LineHeight(0.55f);
            }

            const char* const failure = ModHost::FailureFor(manifest.id);

            if (failure != nullptr && failure[0] != '\0')
            {
                y += 6.0f;
                for (const std::string& text : Wrap(failure, 0.55f, kDetailWide - 16.0f, 4))
                {
                    Text(kDetailX + 8.0f, y, 0.55f, 0xFFE06060, text.c_str());
                    y += LineHeight(0.55f);
                }
            }
        }

        void __cdecl Draw()
        {
            ReadViewport();
            Keys();

            if (!g_open)
            {
                return;
            }

            HoldGameInput(true);

            g_device = Device();

            if (g_device == nullptr)
            {
                return;
            }

            const std::vector<ModRecord>& records = Records();
            const int count = static_cast<int>(records.size());
            char line[128];

            g_selected = g_selected >= count ? count - 1 : g_selected;
            g_selected = g_selected < 0 ? 0 : g_selected;

            const int page = count != 0 ? g_selected / kPerPage : 0;
            const int shown = count - page * kPerPage < kPerPage ? count - page * kPerPage : kPerPage;
            const bool on = count != 0 && ModState::IsEnabled(records[g_selected].manifest.id);

            BeginDrawing();

            // Opaque: the screen exists for the times the game behind it is broken.
            Quad(0.0f, 0.0f, AMOD_MENU_WIDE, AMOD_MENU_TALL, 0xFF080A10);
            Quad(20.0f, 30.0f, 600.0f, 420.0f, 0xFF161C28);
            Frame(20.0f, 30.0f, 600.0f, 420.0f, 0xFF707C90, 2.0f);
            Quad(20.0f, 68.0f, 600.0f, 2.0f, 0xFF707C90);

            for (int at = 0; at < shown; ++at)
            {
                float x = 0.0f;
                float y = 0.0f;

                TilePlace(at, x, y);
                DrawTileBox(records[page * kPerPage + at], page * kPerPage + at, x, y);
            }

            if (count != 0)
            {
                Quad(kDetailX - 8.0f, 84.0f, kDetailWide + 16.0f, 300.0f, 0xFF101828);
                Quad(kDetailX, kButtonY, kDetailWide, kButtonTall, 0xFF304058);
                Frame(kDetailX, kButtonY, kDetailWide, kButtonTall, 0xFFFFD070, 2.0f);
            }

            BeginText();

            TextCentred(320.0f, 32.0f, 1.1f, 0xFFFFD070, "MODS");

            if (count == 0)
            {
                TextCentred(320.0f, 200.0f, 0.8f, 0xFFC8CCD8, "no mods are installed");
            }
            else
            {
                for (int at = 0; at < shown; ++at)
                {
                    const ModRecord& record = records[page * kPerPage + at];
                    float x = 0.0f;
                    float y = 0.0f;

                    TilePlace(at, x, y);

                    void* const icon = IconFor(record);
                    Sprite(icon, x + (kTileSize - kIconSize) * 0.5f, y + 6.0f, kIconSize, kIconSize);
                    DrawTileText(record, page * kPerPage + at, x, y, icon != nullptr);
                }

                DrawDetails(records[g_selected]);
                TextCentred(kDetailX + kDetailWide * 0.5f, kButtonY + 8.0f, 0.7f, 0xFFFFFFFF,
                            on ? "DISABLE" : "ENABLE");

                snprintf(line, sizeof(line), "%d of %d      page %d of %d", g_selected + 1, count,
                         page + 1, (count + kPerPage - 1) / kPerPage);
                Text(kGridX, 350.0f, 0.6f, 0xFFC8CCD8, line);
            }

            Text(kGridX, 408.0f, 0.55f, 0xFFB0B8C8, "arrows or mouse pick, enter switches, "
                                                    "pgup/pgdn pages");

            Text(kGridX, 426.0f, 0.55f, 0xFFB0B8C8, "esc or f10 closes; what cannot change while "
                                                    "the game runs waits for a restart");

            EndDrawing();
            g_device = nullptr;
        }

        void __cdecl ModsCommand(int argc, const char** argv, void* user)
        {
            const std::vector<ModRecord>& records = Records();
            char line[256];

            (void)user;

            if (argc >= 3 && (strcmp(argv[1], "on") == 0 || strcmp(argv[1], "off") == 0))
            {
                const bool on = strcmp(argv[1], "on") == 0;
                const int index = ModManagerApi::Find(argv[2]);
                std::string error;

                if (index < 0)
                {
                    snprintf(line, sizeof(line), "there is no mod called '%s'", argv[2]);
                }
                else if (!ModManagerApi::SetEnabled(index, on, error))
                {
                    snprintf(line, sizeof(line), "'%s' was not switched %s: %s", argv[2],
                             on ? "on" : "off", error.c_str());
                }
                else
                {
                    snprintf(line, sizeof(line), "'%s' is %s", argv[2], on ? "on" : "off");
                }

                ConsoleApi::Print(line);
                return;
            }

            if (argc >= 2 && strcmp(argv[1], "screen") == 0)
            {
                Open(!g_open);
                return;
            }

            if (records.empty())
            {
                ConsoleApi::Print("no mods are installed");
                return;
            }

            for (size_t at = 0; at < records.size(); ++at)
            {
                const ModManifest& manifest = records[at].manifest;

                snprintf(line, sizeof(line), "%2zu  %-22s %-3s %-8s %s", at + 1, manifest.id.c_str(),
                         ModState::IsEnabled(manifest.id) ? "on" : "off", StatusWord(StatusOf(records[at])), manifest.name.c_str());

                ConsoleApi::Print(line);
            }

            ConsoleApi::Print("mods on <id> / mods off <id> switch one, mods screen opens the "
                              "screen (F10)");
        }
    }

    bool ModScreen::Install()
    {
        g_drawText = reinterpret_cast<DrawTextRoutine>(GameModule::Resolve(kFontDrawText));
        g_measure = reinterpret_cast<MeasureRoutine>(GameModule::Resolve(kFontMeasure));

        if (g_drawText == nullptr || g_measure == nullptr)
        {
            FORGE_LOG_WARN("The font routines were not located, so the loader's mod screen has "
                           "no text and is not offered");
            return false;
        }

        RenderApi::AddInternalPresent(&Draw);
        ConsoleApi::AddInternalCommand("mods", &ModsCommand, nullptr);

        FORGE_LOG_INFO("The loader's own mod screen is on F10");
        return true;
    }

    void ModScreen::Uninstall()
    {
        if (g_open)
        {
            HoldGameInput(false);
            InputApi::HoldSystemCursor(false);
        }

        ReleaseIcons();

        g_open = false;
        g_selected = 0;
        g_hover = -1;
        g_device = nullptr;
        g_drawText = nullptr;
        g_measure = nullptr;
    }

    bool ModScreen::IsOpen()
    {
        return g_open;
    }
}
