#pragma once

#include <windows.h>

#include <filesystem>
#include <stdint.h>
#include <string>
#include <vector>

namespace Forge
{
    // Writes the .amod container.
    //
    // Entries are stored uncompressed. The result is an ordinary zip that any archiver can
    // open, and the loader reads it without decompressing anything. Mods that want smaller
    // downloads can be repacked with a general-purpose tool: the loader inflates those too.
    class ZipWriter
    {
    public:
        ZipWriter() = default;
        ~ZipWriter();

        ZipWriter(const ZipWriter&) = delete;
        ZipWriter& operator=(const ZipWriter&) = delete;

        bool                                                        Open(const std::filesystem::path& file, std::string& error);
        bool                                                        Add(const std::string& name, const void* data, size_t size, std::string& error);
        bool                                                        Finish(std::string& error);
        void                                                        Abort();

    private:
        struct Record
        {
            std::string name;
            uint32_t crc32;
            uint32_t size;
            uint32_t localHeaderOffset;
        };

        bool                                                        Write(const void* data, size_t size, std::string& error);

        std::filesystem::path m_filePath;
        HANDLE m_handle = INVALID_HANDLE_VALUE;
        uint32_t m_position = 0;
        std::vector<Record> m_records;
    };
}
