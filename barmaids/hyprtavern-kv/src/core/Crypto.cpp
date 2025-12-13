#include "Crypto.hpp"

#include "../helpers/Logger.hpp"

#include <openssl/evp.h>
#include <openssl/rand.h>

#include <fstream>
#include <cstring>

using namespace Crypto;

constexpr const size_t SALT_LEN     = 16;
constexpr const size_t IV_LEN       = 12;
constexpr const size_t TAG_LEN      = 16;
constexpr const size_t KEY_LEN      = 32;
constexpr const size_t PBKDF2_ITERS = 100000;

constexpr const char*  BLOB_MAGIC = "TAVERNKV";

//
static std::vector<unsigned char> deriveKey(const std::string& password, const std::vector<uint8_t>& salt) {
    std::vector<unsigned char> key(KEY_LEN);
    if (PKCS5_PBKDF2_HMAC(password.data(), password.size(), salt.data(), salt.size(), PBKDF2_ITERS, EVP_sha256(), KEY_LEN, key.data()) != 1)
        return {};
    return key;
}

CEncryptedBlob::CEncryptedBlob(const std::string& data, const std::string& pw) {
    m_salt.resize(SALT_LEN);
    m_iv.resize(IV_LEN);
    m_tag.resize(TAG_LEN);

    // generate random salt and iv
    if (RAND_bytes(m_salt.data(), SALT_LEN) != 1 || RAND_bytes(m_iv.data(), IV_LEN) != 1) {
        g_logger->log(LOG_ERR, "Crypto: failed to generate random salt or iv");
        return;
    }

    // derive a pbkdf2 key
    auto key = deriveKey(pw, m_salt);

    if (key.empty()) {
        g_logger->log(LOG_ERR, "Crypto: failed to derive a key");
        return;
    }

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        g_logger->log(LOG_ERR, "Crypto: failed to begin a cipher ctx");
        return;
    }

    m_cipher.resize(data.size() + EVP_MAX_BLOCK_LENGTH);

    // AES-256-GCM
    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 || EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, IV_LEN, nullptr) != 1 ||
        EVP_EncryptInit_ex(ctx, nullptr, nullptr, key.data(), m_iv.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        g_logger->log(LOG_ERR, "Crypto: EVP_EncryptInit_ex failed");
        return;
    }

    // encrypt
    int len = 0;
    if (EVP_EncryptUpdate(ctx, m_cipher.data(), &len, reinterpret_cast<const unsigned char*>(data.data()), data.size()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        g_logger->log(LOG_ERR, "Crypto: EVP_EncryptUpdate failed");
        return;
    }

    size_t cipherLen = sc<size_t>(len);

    // finish
    if (EVP_EncryptFinal_ex(ctx, m_cipher.data() + len, &len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        g_logger->log(LOG_ERR, "Crypto: EVP_EncryptFinal_ex failed");
        return;
    }

    cipherLen += len;
    m_cipher.resize(cipherLen);

    // get tag for auth
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, TAG_LEN, m_tag.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        g_logger->log(LOG_ERR, "Crypto: EVP_CIPHER_CTX_ctrl failed");
        return;
    }

    EVP_CIPHER_CTX_free(ctx);
    m_result = CRYPTO_RESULT_OK;
}

CEncryptedBlob::CEncryptedBlob(const std::filesystem::path& path, const std::string& pw) {

    // first, read the file
    if (const auto ret = readFile(path); ret != CRYPTO_RESULT_OK) {
        g_logger->log(LOG_ERR, "Crypto: failed to read store at {}", path.string());
        m_result = ret;
        return;
    }

    auto            key = deriveKey(pw, m_salt);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        g_logger->log(LOG_ERR, "Crypto: EVP_CIPHER_CTX_new failed");
        return;
    }

    std::vector<uint8_t> plaintext(m_cipher.size());

    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 || EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, IV_LEN, nullptr) != 1 ||
        EVP_DecryptInit_ex(ctx, nullptr, nullptr, key.data(), m_iv.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        g_logger->log(LOG_ERR, "Crypto: EVP_DecryptInit_ex failed");
        return;
    }

    int len = 0;
    if (EVP_DecryptUpdate(ctx, plaintext.data(), &len, m_cipher.data(), m_cipher.size()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        g_logger->log(LOG_ERR, "Crypto: EVP_DecryptInit_ex failed");
        return;
    }

    int plaintextLen = len;

    // set tag to verify
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, TAG_LEN, const_cast<unsigned char*>(m_tag.data())) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        g_logger->log(LOG_ERR, "Crypto: EVP_CIPHER_CTX_ctrl failed");
        return;
    }

    // final, verify ret
    int ret = EVP_DecryptFinal_ex(ctx, plaintext.data() + len, &len);
    EVP_CIPHER_CTX_free(ctx);

    if (ret <= 0) {
        m_result = CRYPTO_RESULT_BAD_PW;
        g_logger->log(LOG_ERR, "Crypto: EVP_DecryptFinal_ex failed");
        return;
    }

    plaintextLen += len;
    plaintext.resize(plaintextLen);

    m_data = std::string(rc<char*>(plaintext.data()), plaintext.size());

    m_result = CRYPTO_RESULT_OK;
}

eCryptoResult CEncryptedBlob::readFile(const std::filesystem::path& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.good())
        return CRYPTO_RESULT_FILE_NOT_FOUND;

    m_salt.resize(SALT_LEN);
    m_iv.resize(IV_LEN);
    m_tag.resize(TAG_LEN);

    std::vector<char> magicCheck;
    magicCheck.resize(std::string_view{BLOB_MAGIC}.size());
    char versionByte = 0;

    ifs.read(magicCheck.data(), magicCheck.size());
    ifs.read(&versionByte, 1);

    if (memcmp(magicCheck.data(), BLOB_MAGIC, magicCheck.size()) != 0) {
        g_logger->log(LOG_ERR, "failed to read store: invalid magic");
        return CRYPTO_RESULT_BAD_FILE;
    }

    if (versionByte != '1') {
        g_logger->log(LOG_ERR, "failed to read store: invalid version");
        return CRYPTO_RESULT_BAD_FILE;
    }

    ifs.read(reinterpret_cast<char*>(m_salt.data()), SALT_LEN);
    ifs.read(reinterpret_cast<char*>(m_iv.data()), IV_LEN);

    if (ifs.eof()) {
        g_logger->log(LOG_ERR, "failed to read store: corrupt file");
        return CRYPTO_RESULT_BAD_FILE;
    }

    std::vector<char> rest((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>{});
    if (rest.size() < TAG_LEN) {
        g_logger->log(LOG_ERR, "failed to read store: corrupt file");
        return CRYPTO_RESULT_BAD_FILE;
    }

    size_t ctLen = rest.size() - TAG_LEN;
    m_cipher.assign(rest.begin(), rest.begin() + ctLen);
    m_tag.assign(rest.begin() + ctLen, rest.end());

    return CRYPTO_RESULT_OK;
}

eCryptoResult CEncryptedBlob::result() {
    return m_result;
}

std::expected<void, std::string> CEncryptedBlob::writeToFile(const std::filesystem::path& path) {
    std::ofstream ofs(path, std::ios::trunc | std::ios::binary);
    if (!ofs.good())
        return std::unexpected("failed to open file for write");

    ofs.write(BLOB_MAGIC, std::strlen(BLOB_MAGIC));
    ofs.write("1", 1); // version
    ofs.write(rc<const char*>(m_salt.data()), m_salt.size());
    ofs.write(rc<const char*>(m_iv.data()), m_iv.size());
    ofs.write(rc<const char*>(m_cipher.data()), m_cipher.size());
    ofs.write(rc<const char*>(m_tag.data()), m_tag.size());

    ofs.close();

    return {};
}

std::string CEncryptedBlob::data() const {
    return m_data;
}
