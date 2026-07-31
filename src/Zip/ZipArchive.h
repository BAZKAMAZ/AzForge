#pragma once

#include <windows.h>

#include <filesystem>
#include <map>
#include <stdint.h>
#include <string>
#include <vector>

namespace Forge
{
    struct ZipEntry
    {
        std::string name;               // as stored in the archive
        std::string lookupName;         // lowercased, forward slashes
        uint16_t compressionMethod = 0; // 0 = stored, 8 = deflate
        uint32_t crc32 = 0;
        uint32_t compressedSize = 0;
        uint32_t uncompressedSize = 0;
        uint32_t localHeaderOffset = 0;
    };

    class ZipArchive
    {
    public:
        ZipArchive() = default;
        ~ZipArchive();

        ZipArchive(const ZipArchive&) = delete;
        ZipArchive& operator=(const ZipArchive&) = delete;

        bool                                                        Open(const std::filesystem::path& file, std::string& error);
        void                                                        Close();
        bool                                                        IsOpen() const;

        const std::filesystem::path&                                FilePath() const;
        const std::vector<ZipEntry>&                                Entries() const;
        const ZipEntry*                                             Find(const std::string& name) const;

        bool                                                        Read(const ZipEntry& entry, std::vector<uint8_t>& out, std::string& error);
        bool                                                        ReadByName(const std::string& name, std::vector<uint8_t>& out, std::string& error);

    private:
        bool                                                        ReadAt(uint64_t offset, void* buffer, uint32_t size, std::string& error);
        bool                                                        LocateEndOfCentralDirectory(uint32_t& directoryOffset, uint32_t& entryCount, std::string& error);
        bool                                                        ReadCentralDirectory(uint32_t directoryOffset, uint32_t entryCount, std::string& error);

        std::filesystem::path m_filePath;
        HANDLE m_handle = INVALID_HANDLE_VALUE;
        uint64_t m_fileSize = 0;
        std::vector<ZipEntry> m_entries;
        std::map<std::string, size_t> m_index;
    };
}
