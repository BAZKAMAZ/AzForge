#ifndef AZFORGE_RENDER_API_H
#define AZFORGE_RENDER_API_H

#include "ForgeTypes.h"
#include "ObjectsMenuApi.h"     /* AmodMenuItem */

#ifdef __cplusplus
extern "C" {
#endif

/* Fired once per frame, inside the open scene, before the frame is presented. The frame * pointer is valid only for the call. */
typedef void (__cdecl* AmodRenderCallback)(const AmodFrame* frame, void* user);

/* When an element-draw callback runs, relative to the game's own draw of the item */
typedef enum AmodElementPhase
{
    AMOD_ELEMENT_BEFORE  = 1,   /* before the game draws the item - draws under it */
    AMOD_ELEMENT_AFTER   = 2,   /* after the game draws the item - draws over it */
    AMOD_ELEMENT_REPLACE = 4    /* suppress the game's own draw of the item entirely */
} AmodElementPhase;

/* Fired around the game's draw of a menu item */
typedef void (__cdecl* AmodElementDrawCallback)(AmodMenuItem item, AmodElementPhase phase, void* user);

typedef void (__cdecl* AmodItemStyleCallback)(AmodMenuItem item, float rgba[4], float scale[2], void* user);

typedef int (__cdecl* AmodMenuBackgroundCallback)(const char* menuId, void* user);

/* A menu font, resolved by name through GetFont. Opaque; null means the default system font. */
typedef void* AmodFont;

typedef struct AmodRenderApi
{
    uint32_t structSize;

    /* The live IDirect3DDevice8 as a void*, or null before the device exists or while it is * lost */
    void* (__cdecl* GetDevice)(AmodMod mod);

    /* The backbuffer size in pixels. Either pointer may be null. Both are left untouched when * the device is not up yet. */
    void (__cdecl* GetViewport)(AmodMod mod, int* width, int* height);

    /* Registers a per-frame draw callback. Non-zero token on success. The callback runs until * the token is dropped or the mod is unloaded. */
    AmodToken (__cdecl* Subscribe)(AmodMod mod, AmodRenderCallback callback, void* user);
    void (__cdecl* Unsubscribe)(AmodMod mod, AmodToken token);

    
    AmodToken (__cdecl* SubscribeFrameBegin)(AmodMod mod, AmodRenderCallback callback, void* user);

    
    AmodToken (__cdecl* OnElementDraw)(AmodMod mod, const char* id, uint32_t phases,
                                       AmodElementDrawCallback callback, void* user);
    void (__cdecl* CancelElementDraw)(AmodMod mod, AmodToken token);

    
    AmodToken (__cdecl* OnItemStyle)(AmodMod mod, const char* id, AmodItemStyleCallback callback, void* user);
    void (__cdecl* CancelItemStyle)(AmodMod mod, AmodToken token);

    
    AmodToken (__cdecl* OnMenuBackground)(AmodMod mod, AmodMenuBackgroundCallback callback, void* user);
    void (__cdecl* CancelMenuBackground)(AmodMod mod, AmodToken token);

    
    /* The menu font of this name (bigitem, multy_e, and so on from menu_cfg.txt), or null for an * unkn... */
    AmodFont (__cdecl* GetFont)(AmodMod mod, const char* name);

    /* Named DrawTextLine, not DrawText, because <windows.h> defines a DrawText macro */
    void (__cdecl* DrawTextLine)(AmodMod mod, AmodFont font, float x, float y, const char* text, float scale, uint32_t color);

    /* Fields added in later API versions go here, and nowhere else. */
} AmodRenderApi;

AMOD_LAST_FIELD(AmodRenderApi, DrawTextLine, 0x34);

#ifdef __cplusplus
} /* extern "C" */
#endif

#ifdef __cplusplus

#include <math.h>

/* The device type comes from the mod's own <d3d8.h> when it includes one */
struct IDirect3DDevice8;

namespace Amod
{
    namespace Render
    {
        inline const AmodRenderApi* g_render = nullptr;
        inline AmodMod g_self = nullptr;

        inline void                                                 Attach(const AmodRenderApi* api, AmodMod self)
        {
            g_render = api;
            g_self = self;
        }

        inline bool                                                 IsAvailable()
        {
            return g_render != nullptr;
        }

        inline IDirect3DDevice8*                                    Device()
        {
            return g_render != nullptr
                ? static_cast<IDirect3DDevice8*>(g_render->GetDevice(g_self))
                : nullptr;
        }

        inline void                                                 Viewport(int* width, int* height)
        {
            if (g_render != nullptr)
            {
                g_render->GetViewport(g_self, width, height);
            }
        }

        
        inline void                                                 MenuToScreen(float menuX, float menuY, float* screenX, float* screenY)
        {
            int width = 640;
            int height = 480;
            Viewport(&width, &height);

            if (screenX != nullptr)
            {
                *screenX = menuX * (static_cast<float>(width) / 640.0f);
            }
            if (screenY != nullptr)
            {
                *screenY = (480.0f - menuY) * (static_cast<float>(height) / 480.0f);
            }
        }

        inline void                                                 MenuToScreenSize(float menuWidth, float menuHeight, float* screenWidth, float* screenHeight)
        {
            int width = 640;
            int height = 480;
            Viewport(&width, &height);

            if (screenWidth != nullptr)
            {
                *screenWidth = menuWidth * (static_cast<float>(width) / 640.0f);
            }
            if (screenHeight != nullptr)
            {
                *screenHeight = menuHeight * (static_cast<float>(height) / 480.0f);
            }
        }

        inline AmodToken                                            OnFrame(AmodRenderCallback callback, void* user = nullptr)
        {
            return g_render != nullptr
                ? g_render->Subscribe(g_self, callback, user)
                : AMOD_INVALID_TOKEN;
        }

        inline void                                                 CancelFrame(AmodToken token)
        {
            if (g_render != nullptr)
            {
                g_render->Unsubscribe(g_self, token);
            }
        }

        /* A callback at the start of the frame, before the game draws - for whole-frame render * state (wir... */
        inline AmodToken                                            OnFrameBegin(AmodRenderCallback callback, void* user = nullptr)
        {
            return g_render != nullptr
                ? g_render->SubscribeFrameBegin(g_self, callback, user)
                : AMOD_INVALID_TOKEN;
        }

        inline AmodToken                                            OnElementDraw(const char* id, uint32_t phases, AmodElementDrawCallback callback, void* user = nullptr)
        {
            return g_render != nullptr
                ? g_render->OnElementDraw(g_self, id, phases, callback, user)
                : AMOD_INVALID_TOKEN;
        }

        inline void                                                 CancelElementDraw(AmodToken token)
        {
            if (g_render != nullptr)
            {
                g_render->CancelElementDraw(g_self, token);
            }
        }

        inline AmodToken                                            OnItemStyle(const char* id, AmodItemStyleCallback callback, void* user = nullptr)
        {
            return g_render != nullptr
                ? g_render->OnItemStyle(g_self, id, callback, user)
                : AMOD_INVALID_TOKEN;
        }

        inline void                                                 CancelItemStyle(AmodToken token)
        {
            if (g_render != nullptr)
            {
                g_render->CancelItemStyle(g_self, token);
            }
        }

        inline AmodToken                                            OnMenuBackground(AmodMenuBackgroundCallback callback, void* user = nullptr)
        {
            return g_render != nullptr
                ? g_render->OnMenuBackground(g_self, callback, user)
                : AMOD_INVALID_TOKEN;
        }

        inline void                                                 CancelMenuBackground(AmodToken token)
        {
            if (g_render != nullptr)
            {
                g_render->CancelMenuBackground(g_self, token);
            }
        }

        /* The menu font of this name, or null if there is none; pass it to Text. */
        inline AmodFont                                             GetFont(const char* name)
        {
            return g_render != nullptr ? g_render->GetFont(g_self, name) : nullptr;
        }

        
        inline void                                                 Text(AmodFont font, float x, float y, const char* text, float scale, unsigned long color)
        {
            if (g_render != nullptr)
            {
                g_render->DrawTextLine(g_self, font, x, y, text, scale, static_cast<uint32_t>(color));
            }
        }

        /* The same in the default system font. */
        inline void                                                 Text(float x, float y, const char* text, float scale, unsigned long color)
        {
            Text(nullptr, x, y, text, scale, color);
        }

#if defined(D3DFVF_XYZRHW)

        namespace Detail
        {
            struct Vertex { float x, y, z, rhw; unsigned long color; };
            const unsigned long kFvf = D3DFVF_XYZRHW | D3DFVF_DIFFUSE;

            inline Vertex                                           At(float x, float y, unsigned long color)
            {
                Vertex v = { x, y, 0.0f, 1.0f, color };
                return v;
            }

            /* Draws untextured, alpha-blended primitives, saving and restoring the render * state and texture i... */
            inline void                                             Submit(unsigned type, unsigned primitiveCount, const Vertex* vertices)
            {
                IDirect3DDevice8* const device = Device();
                if (device == nullptr)
                {
                    return;
                }

                IDirect3DBaseTexture8* savedTexture = nullptr;
                unsigned long savedLighting = 0, savedZ = 0, savedBlend = 0, savedSrc = 0, savedDst = 0;
                unsigned long savedShader = 0;
                device->GetTexture(0, &savedTexture);
                device->GetRenderState(D3DRS_LIGHTING, &savedLighting);
                device->GetRenderState(D3DRS_ZENABLE, &savedZ);
                device->GetRenderState(D3DRS_ALPHABLENDENABLE, &savedBlend);
                device->GetRenderState(D3DRS_SRCBLEND, &savedSrc);
                device->GetRenderState(D3DRS_DESTBLEND, &savedDst);
                device->GetVertexShader(&savedShader);

                device->SetTexture(0, nullptr);
                device->SetRenderState(D3DRS_LIGHTING, FALSE);
                device->SetRenderState(D3DRS_ZENABLE, FALSE);
                device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
                device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
                device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
                device->SetVertexShader(kFvf);
                device->DrawPrimitiveUP(static_cast<D3DPRIMITIVETYPE>(type), primitiveCount,
                                        vertices, sizeof(Vertex));

                device->SetTexture(0, savedTexture);
                if (savedTexture != nullptr)
                {
                    savedTexture->Release();
                }
                device->SetRenderState(D3DRS_LIGHTING, savedLighting);
                device->SetRenderState(D3DRS_ZENABLE, savedZ);
                device->SetRenderState(D3DRS_ALPHABLENDENABLE, savedBlend);
                device->SetRenderState(D3DRS_SRCBLEND, savedSrc);
                device->SetRenderState(D3DRS_DESTBLEND, savedDst);
                device->SetVertexShader(savedShader);
            }
        }

        /* A filled triangle through three screen points. Colour is 0xAARRGGBB. */
        inline void                                                 Triangle(float x1, float y1, float x2, float y2, float x3, float y3, unsigned long color)
        {
            const Detail::Vertex tri[3] =
                { Detail::At(x1, y1, color), Detail::At(x2, y2, color), Detail::At(x3, y3, color) };
            Detail::Submit(D3DPT_TRIANGLELIST, 1, tri);
        }

        /* A straight line from (x1, y1) to (x2, y2), drawn as a thin quad. */
        inline void                                                 Line(float x1, float y1, float x2, float y2, float thickness, unsigned long color)
        {
            const float dx = x2 - x1, dy = y2 - y1;
            const float length = static_cast<float>(sqrt(static_cast<double>(dx * dx + dy * dy)));
            if (length <= 0.0f)
            {
                return;
            }
            const float nx = -dy / length * thickness * 0.5f;
            const float ny = dx / length * thickness * 0.5f;

            const Detail::Vertex quad[4] =
            {
                Detail::At(x1 + nx, y1 + ny, color), Detail::At(x2 + nx, y2 + ny, color),
                Detail::At(x1 - nx, y1 - ny, color), Detail::At(x2 - nx, y2 - ny, color),
            };
            Detail::Submit(D3DPT_TRIANGLESTRIP, 2, quad);
        }

        /* The four corners of a rectangle centred at (cx, cy), rotated by angle radians * (positive is cloc... */
        inline void                                                 RectCorners(float cx, float cy, float width, float height, float angle, float outX[4], float outY[4])
        {
            const float hw = width * 0.5f, hh = height * 0.5f;
            const float c = static_cast<float>(cos(static_cast<double>(angle)));
            const float s = static_cast<float>(sin(static_cast<double>(angle)));
            const float ox[4] = { -hw, hw, -hw, hw };
            const float oy[4] = { -hh, -hh, hh, hh };
            for (int i = 0; i < 4; ++i)
            {
                outX[i] = cx + ox[i] * c - oy[i] * s;
                outY[i] = cy + ox[i] * s + oy[i] * c;
            }
        }

        /* A filled rectangle centred at (cx, cy), rotated by angle radians. Colour is * 0xAARRGGBB. */
        inline void                                                 Rect(float cx, float cy, float width, float height, float angle, unsigned long color)
        {
            float px[4], py[4];
            RectCorners(cx, cy, width, height, angle, px, py);
            const Detail::Vertex quad[4] =
            {
                Detail::At(px[0], py[0], color), Detail::At(px[1], py[1], color),
                Detail::At(px[2], py[2], color), Detail::At(px[3], py[3], color),
            };
            Detail::Submit(D3DPT_TRIANGLESTRIP, 2, quad);
        }

        /* The outline of the same rectangle, four lines of the given thickness. */
        inline void                                                 RectOutline(float cx, float cy, float width, float height, float angle, float thickness, unsigned long color)
        {
            float px[4], py[4];
            RectCorners(cx, cy, width, height, angle, px, py);
            Line(px[0], py[0], px[1], py[1], thickness, color); // top
            Line(px[1], py[1], px[3], py[3], thickness, color); // right
            Line(px[3], py[3], px[2], py[2], thickness, color); // bottom
            Line(px[2], py[2], px[0], py[0], thickness, color); // left
        }

        /* --- post-processing: capturing and redrawing the finished frame ----------------- */

        namespace Detail
        {
            struct TexVertex { float x, y, z, rhw; unsigned long color; float u, v; };
            const unsigned long kTexFvf = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;

            /* Draws a textured quad, saving and restoring the state it touches */
            inline void                                             SubmitTextured(IDirect3DTexture8* tex, const TexVertex* quad)
            {
                IDirect3DDevice8* const device = Device();
                if (device == nullptr || tex == nullptr)
                {
                    return;
                }

                IDirect3DBaseTexture8* savedTexture = nullptr;
                unsigned long savedLighting = 0, savedZ = 0, savedBlend = 0, savedSrc = 0, savedDst = 0;
                unsigned long savedShader = 0, savedAlphaOp = 0, savedAlphaArg1 = 0;
                device->GetTexture(0, &savedTexture);
                device->GetRenderState(D3DRS_LIGHTING, &savedLighting);
                device->GetRenderState(D3DRS_ZENABLE, &savedZ);
                device->GetRenderState(D3DRS_ALPHABLENDENABLE, &savedBlend);
                device->GetRenderState(D3DRS_SRCBLEND, &savedSrc);
                device->GetRenderState(D3DRS_DESTBLEND, &savedDst);
                device->GetVertexShader(&savedShader);
                device->GetTextureStageState(0, D3DTSS_ALPHAOP, &savedAlphaOp);
                device->GetTextureStageState(0, D3DTSS_ALPHAARG1, &savedAlphaArg1);

                device->SetRenderState(D3DRS_LIGHTING, FALSE);
                device->SetRenderState(D3DRS_ZENABLE, FALSE);
                device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
                device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
                device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
                // Alpha from the vertex colour, not the texture: a captured backbuffer is often
                // an X8 format whose sampled alpha would otherwise leave the quad invisible.
                device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
                device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
                device->SetTexture(0, tex);
                device->SetVertexShader(kTexFvf);
                device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(TexVertex));

                device->SetTexture(0, savedTexture);
                if (savedTexture != nullptr)
                {
                    savedTexture->Release();
                }
                device->SetRenderState(D3DRS_LIGHTING, savedLighting);
                device->SetRenderState(D3DRS_ZENABLE, savedZ);
                device->SetRenderState(D3DRS_ALPHABLENDENABLE, savedBlend);
                device->SetRenderState(D3DRS_SRCBLEND, savedSrc);
                device->SetRenderState(D3DRS_DESTBLEND, savedDst);
                device->SetVertexShader(savedShader);
                device->SetTextureStageState(0, D3DTSS_ALPHAOP, savedAlphaOp);
                device->SetTextureStageState(0, D3DTSS_ALPHAARG1, savedAlphaArg1);
            }
        }

        /* Draws a texture as a quad centred at (cx, cy), rotated by angle radians, tinted by * colour (0xFF... */
        inline void                                                 DrawTexture(IDirect3DTexture8* texture, float cx, float cy, float width, float height, float angle, unsigned long color)
        {
            float px[4], py[4];
            RectCorners(cx, cy, width, height, angle, px, py);
            const float u[4] = { 0.0f, 1.0f, 0.0f, 1.0f };
            const float v[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
            Detail::TexVertex quad[4];
            for (int i = 0; i < 4; ++i)
            {
                quad[i].x = px[i];
                quad[i].y = py[i];
                quad[i].z = 0.0f;
                quad[i].rhw = 1.0f;
                quad[i].color = color;
                quad[i].u = u[i];
                quad[i].v = v[i];
            }
            Detail::SubmitTextured(texture, quad);
        }

        
        class FrameCapture
        {
        public:
            FrameCapture() = default;
            ~FrameCapture() { Release(); }

            FrameCapture(const FrameCapture&) = delete;
            FrameCapture& operator=(const FrameCapture&) = delete;

            IDirect3DTexture8*                                      Capture()
            {
                IDirect3DDevice8* const device = Device();
                if (device == nullptr)
                {
                    return nullptr;
                }

                IDirect3DSurface8* back = nullptr;
                if (device->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &back) < 0 || back == nullptr)
                {
                    return nullptr;
                }

                D3DSURFACE_DESC desc;
                back->GetDesc(&desc);
                if (m_texture == nullptr || m_width != static_cast<int>(desc.Width) ||
                    m_height != static_cast<int>(desc.Height))
                {
                    Release();
                    if (device->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET,
                                              desc.Format, D3DPOOL_DEFAULT, &m_texture) < 0)
                    {
                        m_texture = nullptr;
                        back->Release();
                        return nullptr;
                    }
                    m_width = static_cast<int>(desc.Width);
                    m_height = static_cast<int>(desc.Height);
                }

                IDirect3DSurface8* destination = nullptr;
                if (m_texture->GetSurfaceLevel(0, &destination) >= 0 && destination != nullptr)
                {
                    device->CopyRects(back, nullptr, 0, destination, nullptr);
                    destination->Release();
                }
                back->Release();
                return m_texture;
            }

            void                                                   Release()
            {
                if (m_texture != nullptr)
                {
                    m_texture->Release();
                    m_texture = nullptr;
                }
                m_width = 0;
                m_height = 0;
            }

        private:
            IDirect3DTexture8* m_texture = nullptr;
            int m_width = 0;
            int m_height = 0;
        };

        
        class Texture
        {
        public:
            Texture() = default;
            ~Texture() { Release(); }

            Texture(const Texture&) = delete;
            Texture& operator=(const Texture&) = delete;

            IDirect3DTexture8*                                      Get() const { return m_texture; }
            int                                                    Width() const { return m_width; }
            int                                                    Height() const { return m_height; }

            /* Loads the file into a texture */
            bool                                                   Load(const char* path)
            {
                Release();
                IDirect3DDevice8* const device = Device();
                if (device == nullptr || path == nullptr)
                {
                    return false;
                }

                unsigned char* file = nullptr;
                unsigned long fileSize = 0;
                if (!ReadWholeFile(path, file, fileSize) || fileSize < 54)
                {
                    delete[] file;
                    return false;
                }

                // BMP header: 'BM', pixel-array offset at +10, header size +14, width +18,
                // height +22 (negative is top-down), bit count +28, compression +30.
                if (file[0] != 'B' || file[1] != 'M')
                {
                    delete[] file;
                    return false;
                }
                const unsigned long pixelOffset = ReadU32(file + 10);
                const unsigned long headerSize = ReadU32(file + 14);
                const int rawWidth = static_cast<int>(ReadU32(file + 18));
                const int rawHeight = static_cast<int>(ReadU32(file + 22));
                const unsigned short bits = static_cast<unsigned short>(file[28] | (file[29] << 8));
                const unsigned long compression = ReadU32(file + 30);

                const bool topDown = rawHeight < 0;
                const int width = rawWidth;
                const int height = topDown ? -rawHeight : rawHeight;
                const int bytesPerPixel = bits / 8;

                // 0 is BI_RGB, 3 is BI_BITFIELDS - the one an editor writes for 32 bits with
                // alpha, where the channel masks follow the header and say where each one is.
                const bool bitfields = compression == 3 && bits == 32;
                if ((compression != 0 && !bitfields) || (bits != 24 && bits != 32) ||
                    width <= 0 || height <= 0)
                {
                    delete[] file;
                    return false;
                }

                unsigned long maskR = 0x00FF0000;
                unsigned long maskG = 0x0000FF00;
                unsigned long maskB = 0x000000FF;
                unsigned long maskA = 0xFF000000;
                if (bitfields)
                {
                    if (fileSize < 66)
                    {
                        delete[] file;
                        return false;
                    }
                    maskR = ReadU32(file + 54);
                    maskG = ReadU32(file + 58);
                    maskB = ReadU32(file + 62);
                    // Only a V4 or V5 header carries a fourth mask; BITMAPINFOHEADER stops at
                    // three, and then there is no alpha to read.
                    maskA = (headerSize >= 108 && fileSize >= 70) ? ReadU32(file + 66) : 0;
                }

                const int srcPitch = ((width * bytesPerPixel + 3) / 4) * 4; // BMP rows pad to 4
                if (static_cast<unsigned long>(pixelOffset) +
                        static_cast<unsigned long>(srcPitch) * height > fileSize)
                {
                    delete[] file;
                    return false;
                }

                if (device->CreateTexture(width, height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED,
                                          &m_texture) < 0 || m_texture == nullptr)
                {
                    m_texture = nullptr;
                    delete[] file;
                    return false;
                }

                D3DLOCKED_RECT locked;
                if (m_texture->LockRect(0, &locked, nullptr, 0) < 0)
                {
                    Release();
                    delete[] file;
                    return false;
                }

                // An image whose alpha is zero in every pixel has no alpha channel worth
                // honouring, whatever its header says; drawing it as written would draw
                // nothing at all.
                bool anyAlpha = false;
                if (bytesPerPixel == 4 && maskA != 0)
                {
                    for (int y = 0; y < height && !anyAlpha; ++y)
                    {
                        const unsigned char* const src = file + pixelOffset + y * srcPitch;
                        for (int x = 0; x < width; ++x)
                        {
                            if (Channel(ReadU32(src + x * 4), maskA) != 0)
                            {
                                anyAlpha = true;
                                break;
                            }
                        }
                    }
                }

                for (int y = 0; y < height; ++y)
                {
                    const int srcY = topDown ? y : (height - 1 - y); // BMP is bottom-up
                    const unsigned char* const src = file + pixelOffset + srcY * srcPitch;
                    unsigned long* const dst =
                        reinterpret_cast<unsigned long*>(static_cast<unsigned char*>(locked.pBits) +
                                                         y * locked.Pitch);
                    for (int x = 0; x < width; ++x)
                    {
                        const unsigned char* const p = src + x * bytesPerPixel;
                        if (bytesPerPixel == 4)
                        {
                            const unsigned long pixel = ReadU32(p);
                            const unsigned long a = anyAlpha ? Channel(pixel, maskA) : 0xFF;
                            dst[x] = (a << 24) | (Channel(pixel, maskR) << 16) |
                                     (Channel(pixel, maskG) << 8) | Channel(pixel, maskB);
                        }
                        else
                        {
                            dst[x] = 0xFF000000ul | (static_cast<unsigned long>(p[2]) << 16) |
                                     (static_cast<unsigned long>(p[1]) << 8) | p[0];
                        }
                    }
                }

                m_texture->UnlockRect(0);
                delete[] file;
                m_width = width;
                m_height = height;
                return true;
            }

            void                                                   Release()
            {
                if (m_texture != nullptr)
                {
                    m_texture->Release();
                    m_texture = nullptr;
                }
                m_width = 0;
                m_height = 0;
            }

        private:
            static unsigned long                                   ReadU32(const unsigned char* p)
            {
                return static_cast<unsigned long>(p[0]) | (static_cast<unsigned long>(p[1]) << 8) |
                       (static_cast<unsigned long>(p[2]) << 16) | (static_cast<unsigned long>(p[3]) << 24);
            }

            /* One channel out of a pixel, given the mask that says where it is, scaled to eight * bits whatever width the mask happens to be. */
            static unsigned long                                   Channel(unsigned long pixel, unsigned long mask)
            {
                if (mask == 0)
                {
                    return 0;
                }

                unsigned long value = pixel & mask;
                while ((mask & 1) == 0)
                {
                    mask >>= 1;
                    value >>= 1;
                }

                unsigned long width = 0;
                for (unsigned long m = mask; m != 0; m >>= 1)
                {
                    ++width;
                }

                return width >= 8 ? ((value >> (width - 8)) & 0xFF) : (value * 255) / mask;
            }

            // Reads the whole file with the Win32 API, so no CRT file calls are needed in the
            // header. The caller frees out with delete[].
            static bool                                            ReadWholeFile(const char* path, unsigned char*& out, unsigned long& size)
            {
                out = nullptr;
                size = 0;
                const HANDLE handle = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (handle == INVALID_HANDLE_VALUE)
                {
                    return false;
                }
                const DWORD fileSize = GetFileSize(handle, nullptr);
                if (fileSize == INVALID_FILE_SIZE || fileSize == 0)
                {
                    CloseHandle(handle);
                    return false;
                }
                unsigned char* const buffer = new unsigned char[fileSize];
                DWORD read = 0;
                const BOOL ok = ReadFile(handle, buffer, fileSize, &read, nullptr);
                CloseHandle(handle);
                if (!ok || read != fileSize)
                {
                    delete[] buffer;
                    return false;
                }
                out = buffer;
                size = fileSize;
                return true;
            }

            IDirect3DTexture8* m_texture = nullptr;
            int m_width = 0;
            int m_height = 0;
        };

#endif /* D3DFVF_XYZRHW */
    }
}

#endif /* __cplusplus */

#endif /* AZFORGE_RENDER_API_H */
