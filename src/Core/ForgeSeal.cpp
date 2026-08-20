#include "src/Core/ForgeSeal.h"
#include "src/Core/ForgeLog.h"
#include "src/Core/ForgeText.h"

#include <windows.h>
#include <bcrypt.h>

namespace Forge
{
    namespace
    {
        const char* const kSalt = "AzForge kept state v1";

        const size_t kTagBytes = 16;

        PUCHAR Bytes(const std::string& text)
        {
            return reinterpret_cast<PUCHAR>(const_cast<char*>(text.data()));
        }

        // Asked once. Without it the seal still holds, it just no longer names the machine.
        const std::string& MachineGuid()
        {
            static std::string guid;
            static bool asked = false;

            if (asked)
            {
                return guid;
            }

            asked = true;
            wchar_t text[64] = {};
            DWORD size = sizeof(text);

            const LSTATUS read = RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", L"MachineGuid", RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY,
                                              nullptr, text, &size);

            if (read == ERROR_SUCCESS)
            {
                guid = ToUtf8(text);
            }
            else
            {
                FORGE_LOG_WARN("MachineGuid could not be read, so kept state is not tied to this "
                               "machine");
            }

            return guid;
        }

        std::vector<uint8_t> Hmac(const std::string& key, const std::string& data)
        {
            BCRYPT_ALG_HANDLE algorithm = nullptr;
            std::vector<uint8_t> digest;

            if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG)))
            {
                return digest;
            }

            BCRYPT_HASH_HANDLE hash = nullptr;

            if (BCRYPT_SUCCESS(BCryptCreateHash(algorithm, &hash, nullptr, 0, Bytes(key), static_cast<ULONG>(key.size()), 0)))
            {
                digest.resize(32);

                if (!BCRYPT_SUCCESS(BCryptHashData(hash, Bytes(data), static_cast<ULONG>(data.size()), 0)) ||
                    !BCRYPT_SUCCESS(BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0)))
                {
                    digest.clear();
                }

                BCryptDestroyHash(hash);
            }

            BCryptCloseAlgorithmProvider(algorithm, 0);
            return digest;
        }

        // Length first, so two records cannot be rearranged into the same bytes.
        std::string Piece(const std::string& text)
        {
            return std::to_string(text.size()) + ":" + text;
        }

        std::string KeyMaterial(const std::string& id)
        {
            return std::string(kSalt) + Piece(MachineGuid()) + Piece(id);
        }

        std::string Hex(const std::vector<uint8_t>& bytes, size_t count)
        {
            static const char* const digits = "0123456789abcdef";
            std::string out;

            for (size_t at = 0; at < count && at < bytes.size(); ++at)
            {
                out += digits[bytes[at] >> 4];
                out += digits[bytes[at] & 0x0F];
            }

            return out;
        }

        bool FromHex(const std::string& text, std::string& out)
        {
            if ((text.size() & 1) != 0)
            {
                return false;
            }

            out.clear();

            for (size_t at = 0; at < text.size(); at += 2)
            {
                int value = 0;

                for (int half = 0; half < 2; ++half)
                {
                    const char letter = text[at + half];
                    const int digit = (letter >= '0' && letter <= '9') ? letter - '0'
                                    : (letter >= 'a' && letter <= 'f') ? letter - 'a' + 10
                                    : -1;

                    if (digit < 0)
                    {
                        return false;
                    }

                    value = (value << 4) | digit;
                }

                out += static_cast<char>(value);
            }

            return true;
        }

        std::string Stream(const std::string& id, const std::string& kind, const std::string& key, int index, size_t length)
        {
            const std::string material = KeyMaterial(id);
            std::string stream;

            for (int block = 0; stream.size() < length; ++block)
            {
                const std::string data = "mask" + Piece(kind) + Piece(key) + Piece(std::to_string(index)) + Piece(std::to_string(block));
                const std::vector<uint8_t> digest = Hmac(material, data);

                if (digest.empty())
                {
                    return std::string();
                }

                stream.append(reinterpret_cast<const char*>(digest.data()), digest.size());
            }

            return stream;
        }
    }

    bool Seal::IsAvailable()
    {
        return !Hmac(kSalt, "").empty();
    }

    std::string Seal::Tag(const std::string& id, const std::string& kind, const std::string& key, const std::vector<std::string>& values)
    {
        std::string data = "tag" + Piece(kind) + Piece(key);

        for (const std::string& value : values)
        {
            data += Piece(value);
        }

        const std::vector<uint8_t> digest = Hmac(KeyMaterial(id), data);
        return digest.empty() ? std::string() : Hex(digest, kTagBytes);
    }

    std::string Seal::Hide(const std::string& id, const std::string& kind, const std::string& key, int index, const std::string& value)
    {
        const std::string stream = Stream(id, kind, key, index, value.size());

        if (stream.empty() && !value.empty())
        {
            return std::string();
        }

        std::vector<uint8_t> hidden(value.size());

        for (size_t at = 0; at < value.size(); ++at)
        {
            hidden[at] = static_cast<uint8_t>(value[at] ^ stream[at]);
        }

        return Hex(hidden, hidden.size());
    }

    bool Seal::Show(const std::string& id, const std::string& kind, const std::string& key, int index, const std::string& hidden, std::string& out)
    {
        if (!FromHex(hidden, out))
        {
            return false;
        }

        const std::string stream = Stream(id, kind, key, index, out.size());

        if (stream.empty() && !out.empty())
        {
            return false;
        }

        for (size_t at = 0; at < out.size(); ++at)
        {
            out[at] = static_cast<char>(out[at] ^ stream[at]);
        }

        return true;
    }
}
