#pragma once

#include <expected>
#include <string>
#include <filesystem>
#include <vector>
#include <cstdint>

namespace Crypto {
    enum eCryptoResult : uint8_t {
        CRYPTO_RESULT_GENERIC_ERROR = 0,
        CRYPTO_RESULT_OK,
        CRYPTO_RESULT_FILE_NOT_FOUND,
        CRYPTO_RESULT_BAD_PW,
        CRYPTO_RESULT_BAD_FILE,
    };

    class CEncryptedBlob {
      public:
        // create a blob with a password and data
        CEncryptedBlob(const std::string& data, const std::string& pw);

        // read a blob from a file
        CEncryptedBlob(const std::filesystem::path& path, const std::string& pw);

        CEncryptedBlob()  = delete;
        ~CEncryptedBlob() = default;

        CEncryptedBlob(const CEncryptedBlob&) = delete;
        CEncryptedBlob(CEncryptedBlob&)       = delete;

        // is the blob ok?
        eCryptoResult result();

        // write encrypted blob to file
        std::expected<void, std::string> writeToFile(const std::filesystem::path& path);

        // get the unencrypted data, only for reading
        std::string data() const;

      private:
        eCryptoResult        readFile(const std::filesystem::path& path);

        std::vector<uint8_t> m_salt, m_iv, m_cipher, m_tag;

        std::string          m_data;

        eCryptoResult        m_result = CRYPTO_RESULT_GENERIC_ERROR;
    };
}
