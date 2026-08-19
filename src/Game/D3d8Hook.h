#pragma once

#include <stdint.h>

namespace Forge
{
    enum class DeviceEvent
    {
        Created = 1,
        Lost = 2,       // before Reset: every D3DPOOL_DEFAULT resource has to go
        Reset = 3,      // after a Reset that worked: the same device, build them again
        Released = 4
    };

    class D3d8Hook
    {
    public:
        typedef void (*Sink)(DeviceEvent event, void* device);

        static void*                                                Attach(void* d3d8);
        static void                                                 Detach();
        static void                                                 SetSink(Sink sink);
        static void*                                                Device();

        static uint32_t                                             Generation();
    };
}
