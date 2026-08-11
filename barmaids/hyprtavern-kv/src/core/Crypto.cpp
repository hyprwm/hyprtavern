#include "Crypto.hpp"

#include "../helpers/Logger.hpp"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <array>
#include <cerrno>
#include <climits>
#include <cstring>
#include <fstream>
#include <format>
#include <memory>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace Crypto;

constexpr const size_t   SALT_LEN          = 16;
constexpr const size_t   IV_LEN            = 12;
constexpr const size_t   TAG_LEN           = 16;
constexpr const size_t   KEY_LEN           = 32;
constexpr const uint32_t PBKDF2_V1_ITERS   = 100000;
constexpr const uint32_t PBKDF2_V2_ITERS   = 210000;
constexpr const size_t   MAX_BLOB_SIZE     = 8 * 1024 * 1024 + 1024;
constexpr const uint8_t  CURRENT_VERSION   = 2;
constexpr const char*    BLOB_MAGIC        = "TAVERNKV";
constexpr const size_t   BLOB_MAGIC_LENGTH = 8;

using CCipherContext = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;

static std::array<uint8_t, BLOB_MAGIC_LENGTH + 5> authenticatedHeader(const uint8_t version, const uint32_t iterations) {
    std::array<uint8_t, BLOB_MAGIC_LENGTH + 5> header = {};
    std::memcpy(header.data(), BLOB_MAGIC, BLOB_MAGIC_LENGTH);
    header[BLOB_MAGIC_LENGTH]     = '0' + version;
    header[BLOB_MAGIC_LENGTH + 1] = (iterations >> 24) & 0xFF;
    header[BLOB_MAGIC_LENGTH + 2] = (iterations >> 16) & 0xFF;
    header[BLOB_MAGIC_LENGTH + 3] = (iterations >> 8) & 0xFF;
    header[BLOB_MAGIC_LENGTH + 4] = iterations & 0xFF;
    return header;
}

static std::vector<unsigned char> deriveKey(const std::string& password, const std::vector<uint8_t>& salt, const uint32_t iterations) {
    if (password.size() > INT_MAX || salt.size() > INT_MAX || iterations > INT_MAX)
        return {};

    std::vector<unsigned char> key(KEY_LEN);
    if (PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()), salt.data(), static_cast<int>(salt.size()), static_cast<int>(iterations), EVP_sha256(), KEY_LEN,
                          key.data()) != 1)
        return {};
    return key;
}

static bool writeAll(const int fd, const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);

    while (size > 0) {
        const auto written = write(fd, bytes, size);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0)
            return false;

        bytes += written;
        size -= static_cast<size_t>(written);
    }

    return true;
}

CEncryptedBlob::CEncryptedBlob(const std::string& data, const std::string& pw) : m_kdfIterations(PBKDF2_V2_ITERS), m_version(CURRENT_VERSION) {
    if (data.size() > INT_MAX) {
        g_logger->log(LOG_ERR, "Crypto: plaintext is too large");
        return;
    }

    m_salt.resize(SALT_LEN);
    m_iv.resize(IV_LEN);
    m_tag.resize(TAG_LEN);

    if (RAND_bytes(m_salt.data(), SALT_LEN) != 1 || RAND_bytes(m_iv.data(), IV_LEN) != 1) {
        g_logger->log(LOG_ERR, "Crypto: failed to generate random salt or iv");
        return;
    }

    auto key = deriveKey(pw, m_salt, m_kdfIterations);
    if (key.empty()) {
        g_logger->log(LOG_ERR, "Crypto: failed to derive a key");
        return;
    }

    CCipherContext ctx{EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free};
    if (!ctx) {
        OPENSSL_cleanse(key.data(), key.size());
        g_logger->log(LOG_ERR, "Crypto: failed to begin a cipher ctx");
        return;
    }

    if (EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 || EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, IV_LEN, nullptr) != 1 ||
        EVP_EncryptInit_ex(ctx.get(), nullptr, nullptr, key.data(), m_iv.data()) != 1) {
        OPENSSL_cleanse(key.data(), key.size());
        g_logger->log(LOG_ERR, "Crypto: EVP_EncryptInit_ex failed");
        return;
    }
    OPENSSL_cleanse(key.data(), key.size());

    const auto header = authenticatedHeader(m_version, m_kdfIterations);
    int        len    = 0;
    if (EVP_EncryptUpdate(ctx.get(), nullptr, &len, header.data(), static_cast<int>(header.size())) != 1) {
        g_logger->log(LOG_ERR, "Crypto: failed to authenticate blob header");
        return;
    }

    m_cipher.resize(data.size() + EVP_MAX_BLOCK_LENGTH);
    if (EVP_EncryptUpdate(ctx.get(), m_cipher.data(), &len, reinterpret_cast<const unsigned char*>(data.data()), static_cast<int>(data.size())) != 1) {
        g_logger->log(LOG_ERR, "Crypto: EVP_EncryptUpdate failed");
        return;
    }

    size_t cipherLen = static_cast<size_t>(len);
    if (EVP_EncryptFinal_ex(ctx.get(), m_cipher.data() + len, &len) != 1) {
        g_logger->log(LOG_ERR, "Crypto: EVP_EncryptFinal_ex failed");
        return;
    }

    cipherLen += static_cast<size_t>(len);
    m_cipher.resize(cipherLen);

    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, TAG_LEN, m_tag.data()) != 1) {
        g_logger->log(LOG_ERR, "Crypto: failed to obtain authentication tag");
        return;
    }

    m_result = CRYPTO_RESULT_OK;
}

CEncryptedBlob::CEncryptedBlob(const std::filesystem::path& path, const std::string& pw) {
    if (const auto ret = readFile(path); ret != CRYPTO_RESULT_OK) {
        g_logger->log(LOG_ERR, "Crypto: failed to read store at {}", path.string());
        m_result = ret;
        return;
    }

    if (m_cipher.size() > INT_MAX) {
        g_logger->log(LOG_ERR, "Crypto: ciphertext is too large");
        m_result = CRYPTO_RESULT_BAD_FILE;
        return;
    }

    auto key = deriveKey(pw, m_salt, m_kdfIterations);
    if (key.empty()) {
        g_logger->log(LOG_ERR, "Crypto: failed to derive a key");
        return;
    }

    CCipherContext ctx{EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free};
    if (!ctx) {
        OPENSSL_cleanse(key.data(), key.size());
        g_logger->log(LOG_ERR, "Crypto: EVP_CIPHER_CTX_new failed");
        return;
    }

    if (EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 || EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, IV_LEN, nullptr) != 1 ||
        EVP_DecryptInit_ex(ctx.get(), nullptr, nullptr, key.data(), m_iv.data()) != 1) {
        OPENSSL_cleanse(key.data(), key.size());
        g_logger->log(LOG_ERR, "Crypto: EVP_DecryptInit_ex failed");
        return;
    }
    OPENSSL_cleanse(key.data(), key.size());

    int len = 0;
    if (m_version >= 2) {
        const auto header = authenticatedHeader(m_version, m_kdfIterations);
        if (EVP_DecryptUpdate(ctx.get(), nullptr, &len, header.data(), static_cast<int>(header.size())) != 1) {
            g_logger->log(LOG_ERR, "Crypto: failed to authenticate blob header");
            return;
        }
    }

    std::vector<uint8_t> plaintext(m_cipher.size() + EVP_MAX_BLOCK_LENGTH);
    if (EVP_DecryptUpdate(ctx.get(), plaintext.data(), &len, m_cipher.data(), static_cast<int>(m_cipher.size())) != 1) {
        g_logger->log(LOG_ERR, "Crypto: EVP_DecryptUpdate failed");
        return;
    }

    int plaintextLen = len;
    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, TAG_LEN, m_tag.data()) != 1) {
        g_logger->log(LOG_ERR, "Crypto: failed to set authentication tag");
        return;
    }

    if (EVP_DecryptFinal_ex(ctx.get(), plaintext.data() + len, &len) <= 0) {
        m_result = CRYPTO_RESULT_BAD_PW;
        return;
    }

    plaintextLen += len;
    plaintext.resize(static_cast<size_t>(plaintextLen));
    m_data.assign(reinterpret_cast<const char*>(plaintext.data()), plaintext.size());
    m_result = CRYPTO_RESULT_OK;
}

eCryptoResult CEncryptedBlob::readFile(const std::filesystem::path& path) {
    std::error_code ec;
    const auto      fileSize = std::filesystem::file_size(path, ec);
    if (ec)
        return ec == std::errc::no_such_file_or_directory ? CRYPTO_RESULT_FILE_NOT_FOUND : CRYPTO_RESULT_GENERIC_ERROR;
    if (fileSize > MAX_BLOB_SIZE)
        return CRYPTO_RESULT_BAD_FILE;

    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.good())
        return CRYPTO_RESULT_GENERIC_ERROR;

    std::array<char, BLOB_MAGIC_LENGTH> magicCheck  = {};
    char                                versionByte = 0;
    if (!ifs.read(magicCheck.data(), magicCheck.size()) || !ifs.read(&versionByte, 1))
        return CRYPTO_RESULT_BAD_FILE;
    if (std::memcmp(magicCheck.data(), BLOB_MAGIC, magicCheck.size()) != 0)
        return CRYPTO_RESULT_BAD_FILE;

    if (versionByte == '1') {
        m_version       = 1;
        m_kdfIterations = PBKDF2_V1_ITERS;
    } else if (versionByte == '2') {
        m_version                                = 2;
        std::array<uint8_t, 4> encodedIterations = {};
        if (!ifs.read(reinterpret_cast<char*>(encodedIterations.data()), encodedIterations.size()))
            return CRYPTO_RESULT_BAD_FILE;
        m_kdfIterations = (static_cast<uint32_t>(encodedIterations[0]) << 24) | (static_cast<uint32_t>(encodedIterations[1]) << 16) |
            (static_cast<uint32_t>(encodedIterations[2]) << 8) | static_cast<uint32_t>(encodedIterations[3]);
        if (m_kdfIterations < PBKDF2_V1_ITERS || m_kdfIterations > 10000000)
            return CRYPTO_RESULT_BAD_FILE;
    } else
        return CRYPTO_RESULT_BAD_FILE;

    m_salt.resize(SALT_LEN);
    m_iv.resize(IV_LEN);
    if (!ifs.read(reinterpret_cast<char*>(m_salt.data()), m_salt.size()) || !ifs.read(reinterpret_cast<char*>(m_iv.data()), m_iv.size()))
        return CRYPTO_RESULT_BAD_FILE;

    std::vector<char> rest((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>{});
    if (!ifs.eof() || rest.size() < TAG_LEN)
        return CRYPTO_RESULT_BAD_FILE;

    const size_t ciphertextLength = rest.size() - TAG_LEN;
    m_cipher.assign(rest.begin(), rest.begin() + ciphertextLength);
    m_tag.assign(rest.begin() + ciphertextLength, rest.end());
    return CRYPTO_RESULT_OK;
}

eCryptoResult CEncryptedBlob::result() const {
    return m_result;
}

uint8_t CEncryptedBlob::version() const {
    return m_version;
}

std::expected<void, std::string> CEncryptedBlob::writeToFile(const std::filesystem::path& path) {
    if (m_result != CRYPTO_RESULT_OK)
        return std::unexpected("cannot write an invalid encrypted blob");

    const auto directory = path.parent_path();
    const int  dirFd     = open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (dirFd < 0)
        return std::unexpected(std::format("failed to open store directory: {}", std::strerror(errno)));

    std::filesystem::path temporaryPath;
    int                   temporaryFd = -1;
    for (size_t attempt = 0; attempt < 8 && temporaryFd < 0; ++attempt) {
        std::array<uint8_t, 8> random = {};
        if (RAND_bytes(random.data(), random.size()) != 1) {
            close(dirFd);
            return std::unexpected("failed to generate temporary filename");
        }

        uint64_t suffix = 0;
        std::memcpy(&suffix, random.data(), random.size());
        temporaryPath = directory / std::format("{}.tmp.{}.{}", path.filename().string(), getpid(), suffix);
        temporaryFd   = open(temporaryPath.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, S_IRUSR | S_IWUSR);
        if (temporaryFd < 0 && errno != EEXIST) {
            const auto error = std::string{std::strerror(errno)};
            close(dirFd);
            return std::unexpected(std::format("failed to create temporary store: {}", error));
        }
    }

    if (temporaryFd < 0) {
        close(dirFd);
        return std::unexpected("failed to allocate a temporary store filename");
    }

    auto fail = [&](const std::string& error) -> std::expected<void, std::string> {
        close(temporaryFd);
        unlink(temporaryPath.c_str());
        close(dirFd);
        return std::unexpected(error);
    };

    if (fchmod(temporaryFd, S_IRUSR | S_IWUSR) < 0)
        return fail(std::format("failed to secure temporary store: {}", std::strerror(errno)));

    const char versionByte = '0' + m_version;
    if (!writeAll(temporaryFd, BLOB_MAGIC, BLOB_MAGIC_LENGTH) || !writeAll(temporaryFd, &versionByte, 1))
        return fail(std::format("failed to write store header: {}", std::strerror(errno)));

    if (m_version >= 2) {
        const std::array<uint8_t, 4> encodedIterations = {
            static_cast<uint8_t>((m_kdfIterations >> 24) & 0xFF),
            static_cast<uint8_t>((m_kdfIterations >> 16) & 0xFF),
            static_cast<uint8_t>((m_kdfIterations >> 8) & 0xFF),
            static_cast<uint8_t>(m_kdfIterations & 0xFF),
        };
        if (!writeAll(temporaryFd, encodedIterations.data(), encodedIterations.size()))
            return fail(std::format("failed to write KDF parameters: {}", std::strerror(errno)));
    }

    if (!writeAll(temporaryFd, m_salt.data(), m_salt.size()) || !writeAll(temporaryFd, m_iv.data(), m_iv.size()) || !writeAll(temporaryFd, m_cipher.data(), m_cipher.size()) ||
        !writeAll(temporaryFd, m_tag.data(), m_tag.size()))
        return fail(std::format("failed to write encrypted store: {}", std::strerror(errno)));

    if (fsync(temporaryFd) < 0)
        return fail(std::format("failed to sync encrypted store: {}", std::strerror(errno)));

    if (close(temporaryFd) < 0) {
        temporaryFd = -1;
        unlink(temporaryPath.c_str());
        close(dirFd);
        return std::unexpected(std::format("failed to close encrypted store: {}", std::strerror(errno)));
    }
    temporaryFd = -1;

    if (rename(temporaryPath.c_str(), path.c_str()) < 0) {
        const auto error = std::string{std::strerror(errno)};
        unlink(temporaryPath.c_str());
        close(dirFd);
        return std::unexpected(std::format("failed to atomically replace encrypted store: {}", error));
    }

    if (fsync(dirFd) < 0) {
        const auto error = std::string{std::strerror(errno)};
        close(dirFd);
        return std::unexpected(std::format("failed to sync store directory: {}", error));
    }

    if (close(dirFd) < 0)
        return std::unexpected(std::format("failed to close store directory: {}", std::strerror(errno)));

    return {};
}

std::string CEncryptedBlob::data() const {
    return m_data;
}
