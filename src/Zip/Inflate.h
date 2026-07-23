#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

namespace Forge
{
    // Raw DEFLATE decoder (RFC 1951), which is what zip compression method 8 stores.
    //
    // Written from the specification rather than taken from a library, so the loader keeps
    // its only dependency on the operating system. Only decompression is implemented:
    // amodpack writes stored entries, while archives repacked with a general-purpose tool
    // still load.
    //
    // expectedSize comes from the zip directory. Output that does not reach exactly that
    // length is reported as an error instead of being returned truncated.
    bool                                                            Inflate(const void* input, size_t inputSize, size_t expectedSize, std::vector<uint8_t>& out, std::string& error);
}
