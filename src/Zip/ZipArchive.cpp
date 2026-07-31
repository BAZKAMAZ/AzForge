#include "src/Zip/ZipArchive.h"

#include "src/Core/ForgeText.h"
#include "src/Zip/Crc32.h"
#include "src/Zip/Inflate.h"

#include <stdio.h>

namespace Forge
{
    namespace
    {
        constexpr uint32_t kSignatureEndOfCentralDirectory = 0x06054B50;
        constexpr uint32_t kSignatureCentralFileHeader = 0x02014B50;
        constexpr uint32_t kSignatureLocalFileHeader = 0x04034B50;

        constexpr uint32_t kSizeEndOfCentralDirectory = 22;
        constexpr uint32_t kSizeCentralFileHeader = 46;
        constexpr uint32_t kSizeLocalFileHeader = 30;

        constexpr uint32_t kMaxCommentLength = 0xFFFF;
        constexpr uint32_t kMaxEntrySize = 64u * 1024u * 1024u;

        constexpr uint16_t kMethodStored = 0;
        constexpr uint16_t kMethodDeflate = 8;

        uint16_t ReadU16(const uint8_t* data)
        {
            return static_cast<uint16_t>(data[0] | (data[1] << 8));
        }

        uint32_t ReadU32(const uint8_t* data)
        {
            return static_cast<uint32_t>(data[0]) |
                   (static_cast<uint32_t>(data[1]) << 8) |
                   (static_cast<uint32_t>(data[2]) << 16) |
                   (static_cast<uint32_t>(data[3]) << 24);
        }

        std::string Describe(const char* reason, const std::string& detail)
        {
            std::string message = reason;
            if (!detail.empty())
            {
                message += ": ";
                message += detail;
            }
            return message;
        }
    }

    ZipArchive::~ZipArchive()
    {
        Close();
    }

    bool ZipArchive::Open(const std::filesystem::path& file, std::string& error)
    {
        Close();

        m_handle = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (m_handle == INVALID_HANDLE_VALUE)
        {
            char buffer[128];
            snprintf(buffer, sizeof(buffer), "cannot open the archive (error %lu)", GetLastError());
            error = buffer;
            return false;
        }

        LARGE_INTEGER size = {};
        if (!GetFileSizeEx(m_handle, &size))
        {
            error = "cannot query the archive size";
            Close();
            return false;
        }

        m_fileSize = static_cast<uint64_t>(size.QuadPart);
        m_filePath = file;

        uint32_t directoryOffset = 0;
        uint32_t entryCount = 0;
        if (!LocateEndOfCentralDirectory(directoryOffset, entryCount, error))
        {
            Close();
            return false;
        }

        if (!ReadCentralDirectory(directoryOffset, entryCount, error))
        {
            Close();
            return false;
        }

        return true;
    }

    void ZipArchive::Close()
    {
        if (m_handle != INVALID_HANDLE_VALUE)
        {
            CloseHandle(m_handle);
            m_handle = INVALID_HANDLE_VALUE;
        }

        m_filePath.clear();
        m_fileSize = 0;
        m_entries.clear();
        m_index.clear();
    }

    bool ZipArchive::IsOpen() const
    {
        return m_handle != INVALID_HANDLE_VALUE;
    }

    const std::filesystem::path& ZipArchive::FilePath() const
    {
        return m_filePath;
    }

    const std::vector<ZipEntry>& ZipArchive::Entries() const
    {
        return m_entries;
    }

    const ZipEntry* ZipArchive::Find(const std::string& name) const
    {
        const std::string key = ToLowerAscii(NormalizeSlashes(name));
        const auto found = m_index.find(key);
        return found == m_index.end() ? nullptr : &m_entries[found->second];
    }

    bool ZipArchive::ReadAt(uint64_t offset, void* buffer, uint32_t size, std::string& error)
    {
        if (offset + size > m_fileSize)
        {
            error = "read past the end of the archive";
            return false;
        }

        LARGE_INTEGER position = {};
        position.QuadPart = static_cast<LONGLONG>(offset);
        if (!SetFilePointerEx(m_handle, position, nullptr, FILE_BEGIN))
        {
            error = "cannot seek inside the archive";
            return false;
        }

        DWORD read = 0;
        if (!ReadFile(m_handle, buffer, size, &read, nullptr) || read != size)
        {
            error = "cannot read from the archive";
            return false;
        }

        return true;
    }

    bool ZipArchive::LocateEndOfCentralDirectory(uint32_t& directoryOffset, uint32_t& entryCount,
                                                 std::string& error)
    {
        if (m_fileSize < kSizeEndOfCentralDirectory)
        {
            error = "file is too small to be an archive";
            return false;
        }

        const uint64_t searchWindow = static_cast<uint64_t>(kSizeEndOfCentralDirectory) + kMaxCommentLength;
        const uint64_t searchLength = m_fileSize < searchWindow ? m_fileSize : searchWindow;
        const uint64_t searchStart = m_fileSize - searchLength;

        std::vector<uint8_t> tail(static_cast<size_t>(searchLength));
        if (!ReadAt(searchStart, tail.data(), static_cast<uint32_t>(tail.size()), error))
        {
            return false;
        }

        bool found = false;
        size_t recordOffset = 0;
        for (size_t i = tail.size() - kSizeEndOfCentralDirectory + 1; i-- > 0;)
        {
            if (ReadU32(tail.data() + i) == kSignatureEndOfCentralDirectory)
            {
                recordOffset = i;
                found = true;
                break;
            }
        }

        if (!found)
        {
            error = "end of central directory record not found";
            return false;
        }

        const uint8_t* record = tail.data() + recordOffset;
        const uint16_t diskNumber = ReadU16(record + 4);
        const uint16_t directoryDisk = ReadU16(record + 6);
        const uint16_t entriesOnDisk = ReadU16(record + 8);
        const uint16_t entriesTotal = ReadU16(record + 10);

        if (diskNumber != 0 || directoryDisk != 0 || entriesOnDisk != entriesTotal)
        {
            error = "split archives are not supported";
            return false;
        }

        if (entriesTotal == 0xFFFF || ReadU32(record + 16) == 0xFFFFFFFF)
        {
            error = "ZIP64 archives are not supported";
            return false;
        }

        entryCount = entriesTotal;
        directoryOffset = ReadU32(record + 16);
        return true;
    }

    bool ZipArchive::ReadCentralDirectory(uint32_t directoryOffset, uint32_t entryCount,
                                          std::string& error)
    {
        uint64_t cursor = directoryOffset;
        m_entries.reserve(entryCount);

        for (uint32_t i = 0; i < entryCount; ++i)
        {
            uint8_t header[kSizeCentralFileHeader];
            if (!ReadAt(cursor, header, kSizeCentralFileHeader, error))
            {
                return false;
            }

            if (ReadU32(header) != kSignatureCentralFileHeader)
            {
                error = "malformed central directory";
                return false;
            }

            const uint16_t flags = ReadU16(header + 8);
            if ((flags & 0x0001) != 0)
            {
                error = "encrypted archives are not supported";
                return false;
            }

            const uint16_t nameLength = ReadU16(header + 28);
            const uint16_t extraLength = ReadU16(header + 30);
            const uint16_t commentLength = ReadU16(header + 32);

            ZipEntry entry;
            entry.compressionMethod = ReadU16(header + 10);
            entry.crc32 = ReadU32(header + 16);
            entry.compressedSize = ReadU32(header + 20);
            entry.uncompressedSize = ReadU32(header + 24);
            entry.localHeaderOffset = ReadU32(header + 42);

            if (entry.compressedSize == 0xFFFFFFFF ||
                entry.uncompressedSize == 0xFFFFFFFF ||
                entry.localHeaderOffset == 0xFFFFFFFF)
            {
                error = "ZIP64 archives are not supported";
                return false;
            }

            std::string name(nameLength, '\0');
            if (nameLength != 0 && !ReadAt(cursor + kSizeCentralFileHeader, name.data(), nameLength, error))
            {
                return false;
            }

            cursor += static_cast<uint64_t>(kSizeCentralFileHeader) + nameLength + extraLength + commentLength;

            // Directory records carry a trailing separator and no payload.
            if (!name.empty() && (name.back() == '/' || name.back() == '\\'))
            {
                continue;
            }

            entry.name = name;
            entry.lookupName = ToLowerAscii(NormalizeSlashes(name));

            if (entry.lookupName.find("..") != std::string::npos)
            {
                error = Describe("archive entry escapes the container", name);
                return false;
            }

            if (m_index.find(entry.lookupName) != m_index.end())
            {
                error = Describe("duplicate archive entry", name);
                return false;
            }

            m_index.emplace(entry.lookupName, m_entries.size());
            m_entries.push_back(std::move(entry));
        }

        return true;
    }

    bool ZipArchive::Read(const ZipEntry& entry, std::vector<uint8_t>& out, std::string& error)
    {
        out.clear();

        if (entry.compressionMethod != kMethodStored && entry.compressionMethod != kMethodDeflate)
        {
            char buffer[128];
            snprintf(buffer, sizeof(buffer), "unsupported compression method %u", entry.compressionMethod);
            error = Describe(buffer, entry.name);
            return false;
        }

        if (entry.compressionMethod == kMethodStored && entry.compressedSize != entry.uncompressedSize)
        {
            error = Describe("stored entry declares mismatched sizes", entry.name);
            return false;
        }

        if (entry.uncompressedSize > kMaxEntrySize || entry.compressedSize > kMaxEntrySize)
        {
            error = Describe("entry is larger than the loader accepts", entry.name);
            return false;
        }

        uint8_t header[kSizeLocalFileHeader];
        if (!ReadAt(entry.localHeaderOffset, header, kSizeLocalFileHeader, error))
        {
            return false;
        }

        if (ReadU32(header) != kSignatureLocalFileHeader)
        {
            error = Describe("malformed local header", entry.name);
            return false;
        }

        // Name and extra lengths are taken from the local header: an archiver is allowed to
        // store a different extra field there than in the directory.
        const uint16_t nameLength = ReadU16(header + 26);
        const uint16_t extraLength = ReadU16(header + 28);
        const uint64_t dataOffset = static_cast<uint64_t>(entry.localHeaderOffset) +
                                    kSizeLocalFileHeader + nameLength + extraLength;

        if (entry.uncompressedSize == 0)
        {
            return true;
        }

        if (entry.compressionMethod == kMethodStored)
        {
            out.resize(entry.uncompressedSize);
            if (!ReadAt(dataOffset, out.data(), entry.uncompressedSize, error))
            {
                return false;
            }
        }
        else
        {
            std::vector<uint8_t> compressed(entry.compressedSize);
            if (!ReadAt(dataOffset, compressed.data(), entry.compressedSize, error))
            {
                return false;
            }

            std::string reason;
            if (!Inflate(compressed.data(), compressed.size(), entry.uncompressedSize, out, reason))
            {
                error = Describe(reason.c_str(), entry.name);
                return false;
            }
        }

        if (Crc32(out.data(), out.size()) != entry.crc32)
        {
            error = Describe("entry fails its CRC check", entry.name);
            out.clear();
            return false;
        }

        return true;
    }

    bool ZipArchive::ReadByName(const std::string& name, std::vector<uint8_t>& out, std::string& error)
    {
        const ZipEntry* entry = Find(name);
        if (entry == nullptr)
        {
            error = Describe("entry not found in the archive", name);
            return false;
        }

        return Read(*entry, out, error);
    }
}
