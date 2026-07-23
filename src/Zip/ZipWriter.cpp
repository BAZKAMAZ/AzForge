#include "pch.h"

#include "src/Zip/ZipWriter.h"

#include "src/Zip/Crc32.h"

#include <stdio.h>
#include <string.h>

namespace Forge
{
    namespace
    {
        constexpr uint32_t kSignatureLocalFileHeader = 0x04034B50;
        constexpr uint32_t kSignatureCentralFileHeader = 0x02014B50;
        constexpr uint32_t kSignatureEndOfCentralDirectory = 0x06054B50;

        constexpr uint16_t kVersionNeeded = 20;
        constexpr uint16_t kMethodStored = 0;

        void AppendU16(std::vector<uint8_t>& buffer, uint16_t value)
        {
            buffer.push_back(static_cast<uint8_t>(value & 0xFF));
            buffer.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
        }

        void AppendU32(std::vector<uint8_t>& buffer, uint32_t value)
        {
            buffer.push_back(static_cast<uint8_t>(value & 0xFF));
            buffer.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
            buffer.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
            buffer.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
        }
    }

    ZipWriter::~ZipWriter()
    {
        Abort();
    }

    bool ZipWriter::Open(const std::filesystem::path& file, std::string& error)
    {
        error.clear();
        Abort();

        m_handle = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (m_handle == INVALID_HANDLE_VALUE)
        {
            char buffer[128];
            snprintf(buffer, sizeof(buffer), "cannot create the archive (error %lu)", GetLastError());
            error = buffer;
            return false;
        }

        m_filePath = file;
        m_position = 0;
        m_records.clear();
        return true;
    }

    bool ZipWriter::Write(const void* data, size_t size, std::string& error)
    {
        if (size == 0)
        {
            return true;
        }

        DWORD written = 0;
        if (!WriteFile(m_handle, data, static_cast<DWORD>(size), &written, nullptr) || written != size)
        {
            error = "cannot write to the archive";
            return false;
        }

        m_position += static_cast<uint32_t>(size);
        return true;
    }

    bool ZipWriter::Add(const std::string& name, const void* data, size_t size, std::string& error)
    {
        error.clear();

        if (m_handle == INVALID_HANDLE_VALUE)
        {
            error = "the archive is not open";
            return false;
        }

        if (size > 0xFFFFFFFEull)
        {
            error = "the entry is too large for a non-ZIP64 archive";
            return false;
        }

        Record record;
        record.name = name;
        record.crc32 = Crc32(data, size);
        record.size = static_cast<uint32_t>(size);
        record.localHeaderOffset = m_position;

        std::vector<uint8_t> header;
        header.reserve(30 + name.size());
        AppendU32(header, kSignatureLocalFileHeader);
        AppendU16(header, kVersionNeeded);
        AppendU16(header, 0);                                   // flags
        AppendU16(header, kMethodStored);
        AppendU16(header, 0);                                   // modification time
        AppendU16(header, 0);                                   // modification date
        AppendU32(header, record.crc32);
        AppendU32(header, record.size);                         // compressed size
        AppendU32(header, record.size);                         // uncompressed size
        AppendU16(header, static_cast<uint16_t>(name.size()));
        AppendU16(header, 0);                                   // extra length
        header.insert(header.end(), name.begin(), name.end());

        if (!Write(header.data(), header.size(), error) || !Write(data, size, error))
        {
            return false;
        }

        m_records.push_back(std::move(record));
        return true;
    }

    bool ZipWriter::Finish(std::string& error)
    {
        error.clear();

        if (m_handle == INVALID_HANDLE_VALUE)
        {
            error = "the archive is not open";
            return false;
        }

        const uint32_t directoryOffset = m_position;

        for (const Record& record : m_records)
        {
            std::vector<uint8_t> header;
            header.reserve(46 + record.name.size());
            AppendU32(header, kSignatureCentralFileHeader);
            AppendU16(header, kVersionNeeded);                  // version made by
            AppendU16(header, kVersionNeeded);                  // version needed
            AppendU16(header, 0);                               // flags
            AppendU16(header, kMethodStored);
            AppendU16(header, 0);                               // modification time
            AppendU16(header, 0);                               // modification date
            AppendU32(header, record.crc32);
            AppendU32(header, record.size);                     // compressed size
            AppendU32(header, record.size);                     // uncompressed size
            AppendU16(header, static_cast<uint16_t>(record.name.size()));
            AppendU16(header, 0);                               // extra length
            AppendU16(header, 0);                               // comment length
            AppendU16(header, 0);                               // disk number
            AppendU16(header, 0);                               // internal attributes
            AppendU32(header, 0);                               // external attributes
            AppendU32(header, record.localHeaderOffset);
            header.insert(header.end(), record.name.begin(), record.name.end());

            if (!Write(header.data(), header.size(), error))
            {
                return false;
            }
        }

        const uint32_t directorySize = m_position - directoryOffset;

        std::vector<uint8_t> tail;
        tail.reserve(22);
        AppendU32(tail, kSignatureEndOfCentralDirectory);
        AppendU16(tail, 0);                                     // disk number
        AppendU16(tail, 0);                                     // directory start disk
        AppendU16(tail, static_cast<uint16_t>(m_records.size()));
        AppendU16(tail, static_cast<uint16_t>(m_records.size()));
        AppendU32(tail, directorySize);
        AppendU32(tail, directoryOffset);
        AppendU16(tail, 0);                                     // comment length

        if (!Write(tail.data(), tail.size(), error))
        {
            return false;
        }

        CloseHandle(m_handle);
        m_handle = INVALID_HANDLE_VALUE;
        m_records.clear();
        m_position = 0;
        return true;
    }

    void ZipWriter::Abort()
    {
        if (m_handle == INVALID_HANDLE_VALUE)
        {
            return;
        }

        CloseHandle(m_handle);
        m_handle = INVALID_HANDLE_VALUE;

        // A half-written container is worse than none: it would look installable.
        std::error_code ignored;
        std::filesystem::remove(m_filePath, ignored);

        m_records.clear();
        m_position = 0;
    }
}
