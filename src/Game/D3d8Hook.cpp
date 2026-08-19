#include <windows.h>

#include "src/Game/D3d8Hook.h"
#include "src/Core/ForgeLog.h"

namespace Forge
{
    namespace
    {
        constexpr size_t kReleaseSlot = 2;
        constexpr size_t kCreateDeviceSlot = 15;
        constexpr size_t kResetSlot = 14;
        constexpr size_t kSetTextureSlot = 61;
        constexpr size_t kDrawPrimitiveUpSlot = 72;
        constexpr size_t kResourceGetDeviceSlot = 3;    // on the texture, not the device

        typedef unsigned long(__stdcall* ReleaseRoutine)(void* self);
        typedef long(__stdcall* CreateDeviceRoutine)(void* self, unsigned adapter, unsigned type, void* focus, unsigned long flags, void* present, void** returned);
        typedef long(__stdcall* ResetRoutine)(void* self, void* present);
        typedef long(__stdcall* SetTextureRoutine)(void* self, unsigned long stage, void* texture);
        typedef long(__stdcall* DrawPrimitiveUpRoutine)(void* self, unsigned type, unsigned count, const void* vertices, unsigned stride);
        typedef long(__stdcall* GetDeviceRoutine)(void* self, void** device);

        void* g_d3d8 = nullptr;
        void** g_d3d8Vtable = nullptr;
        void* g_originalD3d8Release = nullptr;
        void* g_originalCreateDevice = nullptr;

        void* g_device = nullptr;
        void** g_deviceVtable = nullptr;
        void* g_originalDeviceRelease = nullptr;
        void* g_originalReset = nullptr;
        void* g_originalSetTexture = nullptr;
        void* g_originalDrawPrimitiveUp = nullptr;

        bool g_lost = false;
        bool g_droppedTexture = false;
        uint32_t g_warnedGeneration = 0;
        uint32_t g_generation = 0;
        D3d8Hook::Sink g_sink = nullptr;

        void Notify(DeviceEvent event, void* device)
        {
            if (g_sink != nullptr)
            {
                g_sink(event, device);
            }
        }

        void** VtableOf(void* object)
        {
            return *static_cast<void***>(object);
        }

        // Table slot, not a copy; the public interface calls virtuals beyond d3d8's visible class.
        bool WriteSlot(void** vtable, size_t slot, void* value, void** replaced)
        {
            DWORD previous = 0;
            if (!VirtualProtect(&vtable[slot], sizeof(void*), PAGE_READWRITE, &previous))
            {
                return false;
            }

            if (replaced != nullptr)
            {
                *replaced = vtable[slot];
            }
            vtable[slot] = value;

            VirtualProtect(&vtable[slot], sizeof(void*), previous, &previous);
            return true;
        }

        void RestoreDeviceVtable()
        {
            if (g_deviceVtable != nullptr)
            {
                WriteSlot(g_deviceVtable, kReleaseSlot, g_originalDeviceRelease, nullptr);
                WriteSlot(g_deviceVtable, kResetSlot, g_originalReset, nullptr);
                WriteSlot(g_deviceVtable, kSetTextureSlot, g_originalSetTexture, nullptr);
                WriteSlot(g_deviceVtable, kDrawPrimitiveUpSlot, g_originalDrawPrimitiveUp, nullptr);
            }

            g_device = nullptr;
            g_deviceVtable = nullptr;
            g_originalDeviceRelease = nullptr;
            g_originalReset = nullptr;
            g_originalSetTexture = nullptr;
            g_originalDrawPrimitiveUp = nullptr;
            g_lost = false;
            g_droppedTexture = false;
        }

        bool BelongsElsewhere(void* device, void* texture)
        {
            const GetDeviceRoutine getDevice = reinterpret_cast<GetDeviceRoutine>(VtableOf(texture)[kResourceGetDeviceSlot]);

            void* owner = nullptr;
            if (getDevice(texture, &owner) < 0 || owner == nullptr)
            {
                return false;
            }

            reinterpret_cast<ReleaseRoutine>(VtableOf(owner)[kReleaseSlot])(owner);
            return owner != device;
        }

        long __stdcall SetTextureDetour(void* self, unsigned long stage, void* texture)
        {
            const SetTextureRoutine set = reinterpret_cast<SetTextureRoutine>(g_originalSetTexture);

            // Nothing can be left over from an older device until there has been one.
            if (stage == 0)
            {
                g_droppedTexture = false;

                if (texture != nullptr && g_generation > 1 && self == g_device &&
                    BelongsElsewhere(self, texture))
                {
                    if (g_warnedGeneration != g_generation)
                    {
                        g_warnedGeneration = g_generation;
                        FORGE_LOG_WARN("d3d8: a texture from an older device was dropped rather "
                            "than drawn - the mod that made it has to load it again on "
                            "AMOD_DEVICE_CREATED");
                    }

                    g_droppedTexture = true;
                    texture = nullptr;
                }
            }

            return set(self, stage, texture);
        }

        // Untextured, that draw is a flat white sheet over the screen; the mod meant an image.
        long __stdcall DrawPrimitiveUpDetour(void* self, unsigned type, unsigned count, const void* vertices, unsigned stride)
        {
            if (g_droppedTexture && self == g_device)
            {
                g_droppedTexture = false;
                return 0;
            }

            return reinterpret_cast<DrawPrimitiveUpRoutine>(g_originalDrawPrimitiveUp)(
                self, type, count, vertices, stride);
        }

        unsigned long __stdcall DeviceReleaseDetour(void* self)
        {
            const ReleaseRoutine release = reinterpret_cast<ReleaseRoutine>(g_originalDeviceRelease);
            if (self != g_device)
            {
                return release(self);
            }

            const unsigned long remaining = release(self);
            if (remaining != 0)
            {
                return remaining;
            }

            FORGE_LOG_INFO("d3d8: device released");
            void* const released = g_device;
            RestoreDeviceVtable();
            Notify(DeviceEvent::Released, released);
            return 0;
        }

        long __stdcall DeviceResetDetour(void* self, void* present)
        {
            const ResetRoutine reset = reinterpret_cast<ResetRoutine>(g_originalReset);
            if (self != g_device)
            {
                return reset(self, present);
            }

            if (!g_lost)
            {
                g_lost = true;
                FORGE_LOG_INFO("d3d8: device lost");
                Notify(DeviceEvent::Lost, self);
            }

            const long result = reset(self, present);

            // Logs and callbacks only when the device is first taken.
            if (result >= 0)
            {
                g_lost = false;
                FORGE_LOG_INFO("d3d8: device reset");
                Notify(DeviceEvent::Reset, self);
            }

            return result;
        }

        void HookDevice(void* device)
        {
            void** const vtable = VtableOf(device);

            // d3d8 shares one table between its devices, so a second one arrives already hooked.
            if (vtable != g_deviceVtable)
            {
                RestoreDeviceVtable();

                if (!WriteSlot(vtable, kReleaseSlot, reinterpret_cast<void*>(&DeviceReleaseDetour),
                               &g_originalDeviceRelease) ||
                    !WriteSlot(vtable, kResetSlot, reinterpret_cast<void*>(&DeviceResetDetour),
                               &g_originalReset) ||
                    !WriteSlot(vtable, kSetTextureSlot, reinterpret_cast<void*>(&SetTextureDetour),
                               &g_originalSetTexture) ||
                    !WriteSlot(vtable, kDrawPrimitiveUpSlot,
                               reinterpret_cast<void*>(&DrawPrimitiveUpDetour),
                               &g_originalDrawPrimitiveUp))
                {
                    FORGE_LOG_WARN("d3d8: the device was not hooked, its events are lost");
                    return;
                }

                g_deviceVtable = vtable;
            }

            g_device = device;
            g_lost = false;
            ++g_generation;

            FORGE_LOG_INFO("d3d8: device %p created, incarnation %u", device, g_generation);
            Notify(DeviceEvent::Created, device);
        }

        long __stdcall CreateDeviceDetour(void* self, unsigned adapter, unsigned type, void* focus,
                                          unsigned long flags, void* present, void** returned)
        {
            const CreateDeviceRoutine create =
                reinterpret_cast<CreateDeviceRoutine>(g_originalCreateDevice);
            const long result = create(self, adapter, type, focus, flags, present, returned);

            if (result >= 0 && returned != nullptr && *returned != nullptr)
            {
                HookDevice(*returned);
            }

            return result;
        }

        unsigned long __stdcall D3d8ReleaseDetour(void* self)
        {
            const ReleaseRoutine release = reinterpret_cast<ReleaseRoutine>(g_originalD3d8Release);
            if (self != g_d3d8)
            {
                return release(self);
            }

            const unsigned long remaining = release(self);
            if (remaining == 0)
            {
                D3d8Hook::Detach();
            }
            return remaining;
        }
    }

    void* D3d8Hook::Attach(void* d3d8)
    {
        if (d3d8 == nullptr || d3d8 == g_d3d8)
        {
            return d3d8;
        }

        void** const vtable = VtableOf(d3d8);
        if (vtable != g_d3d8Vtable)
        {
            if (!WriteSlot(vtable, kReleaseSlot, reinterpret_cast<void*>(&D3d8ReleaseDetour),
                           &g_originalD3d8Release) ||
                !WriteSlot(vtable, kCreateDeviceSlot, reinterpret_cast<void*>(&CreateDeviceDetour),
                           &g_originalCreateDevice))
            {
                FORGE_LOG_WARN("d3d8: Direct3D8 was not hooked, device events are lost");
                return d3d8;
            }

            g_d3d8Vtable = vtable;
        }

        g_d3d8 = d3d8;

        FORGE_LOG_INFO("d3d8: Direct3D8 %p hooked", d3d8);
        return d3d8;
    }

    void D3d8Hook::Detach()
    {
        RestoreDeviceVtable();

        if (g_d3d8Vtable != nullptr)
        {
            WriteSlot(g_d3d8Vtable, kReleaseSlot, g_originalD3d8Release, nullptr);
            WriteSlot(g_d3d8Vtable, kCreateDeviceSlot, g_originalCreateDevice, nullptr);
        }

        g_d3d8 = nullptr;
        g_d3d8Vtable = nullptr;
        g_originalD3d8Release = nullptr;
        g_originalCreateDevice = nullptr;
    }

    void D3d8Hook::SetSink(Sink sink)
    {
        g_sink = sink;
    }

    void* D3d8Hook::Device()
    {
        return g_device;
    }

    uint32_t D3d8Hook::Generation()
    {
        return g_generation;
    }
}
