#include "src/Zip/Inflate.h"

namespace Forge
{
    namespace
    {
        constexpr int kMaxBits = 15;
        constexpr int kLiteralSymbols = 288;
        constexpr int kDistanceSymbols = 30;

        // Base lengths and extra bits for the length codes 257..285 (RFC 1951, 3.2.5).
        constexpr uint16_t kLengthBase[29] = {
            3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
            35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
        };
        constexpr uint8_t kLengthExtra[29] = {
            0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
            3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
        };

        // Base distances and extra bits for the distance codes 0..29.
        constexpr uint16_t kDistanceBase[30] = {
            1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
            257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
        };
        constexpr uint8_t kDistanceExtra[30] = {
            0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
            7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
        };

        // Order in which the code-length code lengths appear in a dynamic block header.
        constexpr uint8_t kCodeLengthOrder[19] = {
            16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
        };

        // Canonical Huffman table: how many codes of each bit length, and the symbols
        // ordered by code.
        struct HuffmanTable
        {
            uint16_t count[kMaxBits + 1];
            uint16_t symbol[kLiteralSymbols];
        };

        class BitReader
        {
        public:
            BitReader(const uint8_t* data, size_t size)
                : m_data(data)
                , m_size(size)
            {
            }

            bool Read(int bits, uint32_t& value)
            {
                while (m_bitCount < bits)
                {
                    if (m_position >= m_size)
                    {
                        return false;
                    }
                    m_bitBuffer |= static_cast<uint32_t>(m_data[m_position++]) << m_bitCount;
                    m_bitCount += 8;
                }

                value = m_bitBuffer & ((1u << bits) - 1u);
                m_bitBuffer >>= bits;
                m_bitCount -= bits;
                return true;
            }

            bool ReadBit(uint32_t& value)
            {
                return Read(1, value);
            }

            void AlignToByte()
            {
                m_bitBuffer = 0;
                m_bitCount = 0;
            }

            bool CopyBytes(uint8_t* destination, size_t count)
            {
                if (m_position + count > m_size)
                {
                    return false;
                }
                for (size_t i = 0; i < count; ++i)
                {
                    destination[i] = m_data[m_position + i];
                }
                m_position += count;
                return true;
            }

            bool Skip(size_t count)
            {
                if (m_position + count > m_size)
                {
                    return false;
                }
                m_position += count;
                return true;
            }

            size_t Position() const
            {
                return m_position;
            }

        private:
            const uint8_t* m_data;
            size_t m_size;
            size_t m_position = 0;
            uint32_t m_bitBuffer = 0;
            int m_bitCount = 0;
        };

        // Builds a canonical table from code lengths. An over-subscribed or incomplete set
        // is rejected, except for the single-code case the format allows for distances.
        bool BuildTable(HuffmanTable& table, const uint16_t* lengths, int symbolCount)
        {
            for (int i = 0; i <= kMaxBits; ++i)
            {
                table.count[i] = 0;
            }
            for (int symbol = 0; symbol < symbolCount; ++symbol)
            {
                table.count[lengths[symbol]] += 1;
            }

            if (table.count[0] == symbolCount)
            {
                return true;
            }

            int left = 1;
            for (int length = 1; length <= kMaxBits; ++length)
            {
                left <<= 1;
                left -= table.count[length];
                if (left < 0)
                {
                    return false;
                }
            }

            uint16_t offsets[kMaxBits + 1];
            offsets[0] = 0;
            offsets[1] = 0;
            for (int length = 1; length < kMaxBits; ++length)
            {
                offsets[length + 1] = static_cast<uint16_t>(offsets[length] + table.count[length]);
            }

            for (int symbol = 0; symbol < symbolCount; ++symbol)
            {
                if (lengths[symbol] != 0)
                {
                    table.symbol[offsets[lengths[symbol]]++] = static_cast<uint16_t>(symbol);
                }
            }

            return true;
        }

        // Walks the code one bit at a time, comparing against the first code of each length.
        bool DecodeSymbol(BitReader& reader, const HuffmanTable& table, int& symbol)
        {
            int code = 0;
            int first = 0;
            int index = 0;

            for (int length = 1; length <= kMaxBits; ++length)
            {
                uint32_t bit = 0;
                if (!reader.ReadBit(bit))
                {
                    return false;
                }

                code |= static_cast<int>(bit);

                const int count = table.count[length];
                if (code - first < count)
                {
                    symbol = table.symbol[index + (code - first)];
                    return true;
                }

                index += count;
                first = (first + count) << 1;
                code <<= 1;
            }

            return false;
        }

        void BuildFixedTables(HuffmanTable& literals, HuffmanTable& distances)
        {
            uint16_t lengths[kLiteralSymbols];

            for (int symbol = 0; symbol < 144; ++symbol) lengths[symbol] = 8;
            for (int symbol = 144; symbol < 256; ++symbol) lengths[symbol] = 9;
            for (int symbol = 256; symbol < 280; ++symbol) lengths[symbol] = 7;
            for (int symbol = 280; symbol < 288; ++symbol) lengths[symbol] = 8;
            BuildTable(literals, lengths, kLiteralSymbols);

            for (int symbol = 0; symbol < kDistanceSymbols; ++symbol) lengths[symbol] = 5;
            BuildTable(distances, lengths, kDistanceSymbols);
        }

        bool ReadDynamicTables(BitReader& reader, HuffmanTable& literals, HuffmanTable& distances)
        {
            uint32_t hlit = 0;
            uint32_t hdist = 0;
            uint32_t hclen = 0;
            if (!reader.Read(5, hlit) || !reader.Read(5, hdist) || !reader.Read(4, hclen))
            {
                return false;
            }

            const int literalCount = static_cast<int>(hlit) + 257;
            const int distanceCount = static_cast<int>(hdist) + 1;
            const int codeLengthCount = static_cast<int>(hclen) + 4;

            if (literalCount > kLiteralSymbols || distanceCount > kDistanceSymbols)
            {
                return false;
            }

            uint16_t codeLengths[19] = {};
            for (int i = 0; i < codeLengthCount; ++i)
            {
                uint32_t value = 0;
                if (!reader.Read(3, value))
                {
                    return false;
                }
                codeLengths[kCodeLengthOrder[i]] = static_cast<uint16_t>(value);
            }

            HuffmanTable lengthTable;
            if (!BuildTable(lengthTable, codeLengths, 19))
            {
                return false;
            }

            uint16_t lengths[kLiteralSymbols + kDistanceSymbols] = {};
            int filled = 0;

            while (filled < literalCount + distanceCount)
            {
                int symbol = 0;
                if (!DecodeSymbol(reader, lengthTable, symbol))
                {
                    return false;
                }

                if (symbol < 16)
                {
                    lengths[filled++] = static_cast<uint16_t>(symbol);
                    continue;
                }

                uint16_t repeated = 0;
                int repeat = 0;
                uint32_t extra = 0;

                if (symbol == 16)
                {
                    if (filled == 0 || !reader.Read(2, extra))
                    {
                        return false;
                    }
                    repeated = lengths[filled - 1];
                    repeat = 3 + static_cast<int>(extra);
                }
                else if (symbol == 17)
                {
                    if (!reader.Read(3, extra))
                    {
                        return false;
                    }
                    repeat = 3 + static_cast<int>(extra);
                }
                else
                {
                    if (!reader.Read(7, extra))
                    {
                        return false;
                    }
                    repeat = 11 + static_cast<int>(extra);
                }

                if (filled + repeat > literalCount + distanceCount)
                {
                    return false;
                }

                for (int i = 0; i < repeat; ++i)
                {
                    lengths[filled++] = repeated;
                }
            }

            if (lengths[256] == 0)
            {
                return false;
            }

            return BuildTable(literals, lengths, literalCount) &&
                   BuildTable(distances, lengths + literalCount, distanceCount);
        }

        bool InflateBlockBody(BitReader& reader, const HuffmanTable& literals,
                              const HuffmanTable& distances, size_t expectedSize,
                              std::vector<uint8_t>& out)
        {
            for (;;)
            {
                int symbol = 0;
                if (!DecodeSymbol(reader, literals, symbol))
                {
                    return false;
                }

                if (symbol < 256)
                {
                    if (out.size() >= expectedSize)
                    {
                        return false;
                    }
                    out.push_back(static_cast<uint8_t>(symbol));
                    continue;
                }

                if (symbol == 256)
                {
                    return true;
                }

                symbol -= 257;
                if (symbol >= 29)
                {
                    return false;
                }

                uint32_t extra = 0;
                if (!reader.Read(kLengthExtra[symbol], extra))
                {
                    return false;
                }
                const size_t length = kLengthBase[symbol] + extra;

                int distanceSymbol = 0;
                if (!DecodeSymbol(reader, distances, distanceSymbol) || distanceSymbol >= kDistanceSymbols)
                {
                    return false;
                }

                if (!reader.Read(kDistanceExtra[distanceSymbol], extra))
                {
                    return false;
                }
                const size_t distance = kDistanceBase[distanceSymbol] + extra;

                if (distance > out.size() || out.size() + length > expectedSize)
                {
                    return false;
                }

                // The copy may overlap the output it is producing, which is how deflate
                // encodes runs, so it has to advance one byte at a time.
                size_t source = out.size() - distance;
                for (size_t i = 0; i < length; ++i)
                {
                    out.push_back(out[source + i]);
                }
            }
        }
    }

    bool Inflate(const void* input, size_t inputSize, size_t expectedSize,
                 std::vector<uint8_t>& out, std::string& error)
    {
        error.clear();
        out.clear();
        out.reserve(expectedSize);

        BitReader reader(static_cast<const uint8_t*>(input), inputSize);

        for (;;)
        {
            uint32_t final = 0;
            uint32_t type = 0;
            if (!reader.ReadBit(final) || !reader.Read(2, type))
            {
                error = "the compressed stream ends inside a block header";
                return false;
            }

            if (type == 0)
            {
                reader.AlignToByte();

                uint32_t low = 0;
                uint32_t high = 0;
                uint32_t lowComplement = 0;
                uint32_t highComplement = 0;
                if (!reader.Read(8, low) || !reader.Read(8, high) ||
                    !reader.Read(8, lowComplement) || !reader.Read(8, highComplement))
                {
                    error = "a stored block header is truncated";
                    return false;
                }

                const uint32_t length = low | (high << 8);
                const uint32_t complement = lowComplement | (highComplement << 8);
                if ((length ^ 0xFFFFu) != complement)
                {
                    error = "a stored block declares an inconsistent length";
                    return false;
                }

                if (out.size() + length > expectedSize)
                {
                    error = "the stream produces more data than the directory declares";
                    return false;
                }

                const size_t start = out.size();
                out.resize(start + length);
                if (length != 0 && !reader.CopyBytes(out.data() + start, length))
                {
                    error = "a stored block is truncated";
                    return false;
                }
            }
            else if (type == 1 || type == 2)
            {
                HuffmanTable literals = {};
                HuffmanTable distances = {};

                if (type == 1)
                {
                    BuildFixedTables(literals, distances);
                }
                else if (!ReadDynamicTables(reader, literals, distances))
                {
                    error = "the block declares malformed Huffman tables";
                    return false;
                }

                if (!InflateBlockBody(reader, literals, distances, expectedSize, out))
                {
                    error = "the compressed data is malformed";
                    return false;
                }
            }
            else
            {
                error = "the stream contains a reserved block type";
                return false;
            }

            if (final != 0)
            {
                break;
            }
        }

        if (out.size() != expectedSize)
        {
            error = "the stream produced a different amount of data than the directory declares";
            return false;
        }

        return true;
    }
}
