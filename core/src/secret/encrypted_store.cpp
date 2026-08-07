// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include <repomancer/secret/encrypted_store.h>

#include <sodium.h>

#include <array>
#include <cstring>
#include <string_view>

namespace repomancer::secret {
namespace {

// Blob layout, all fixed except the trailing ciphertext:
//   magic[8] | opslimit u64-LE | memlimit u64-LE | salt[16] | nonce[24] | ct
constexpr std::array<std::uint8_t, 8> kMagic = {'R', 'M', 'S', 'E', 'C', 'v', '1', 0};
constexpr std::size_t kOpsOffset = kMagic.size();
constexpr std::size_t kMemOffset = kOpsOffset + 8;
constexpr std::size_t kSaltOffset = kMemOffset + 8;
constexpr std::size_t kNonceOffset = kSaltOffset + crypto_pwhash_SALTBYTES;
constexpr std::size_t kHeaderLen = kNonceOffset + crypto_aead_xchacha20poly1305_ietf_NPUBBYTES;
constexpr std::size_t kKeyLen = crypto_aead_xchacha20poly1305_ietf_KEYBYTES;
constexpr std::size_t kTagLen = crypto_aead_xchacha20poly1305_ietf_ABYTES;

void put_u64le(std::vector<std::uint8_t>& out, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
    }
}

std::uint64_t get_u64le(const std::uint8_t* p) {
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
        v |= static_cast<std::uint64_t>(p[i]) << (8 * i);
    }
    return v;
}

// Derive the AEAD key from the password with Argon2id. Returns false on the
// libsodium failure path (typically OOM at the requested memlimit).
bool derive_key(SecureBuffer& key, const SecretString& password,
                const unsigned char* salt, std::uint64_t ops, std::size_t mem) {
    return crypto_pwhash(key.data(), key.size(), reinterpret_cast<const char*>(password.bytes()),
                         password.size(), salt, ops, mem,
                         crypto_pwhash_ALG_ARGON2ID13) == 0;
}

} // namespace

CryptoResult<std::vector<std::uint8_t>> encrypt(const SecretString& plaintext,
                                                const SecretString& password,
                                                KdfStrength strength) {
    ensure_initialized();
    const std::uint64_t ops = strength == KdfStrength::Moderate
                                  ? crypto_pwhash_OPSLIMIT_MODERATE
                                  : crypto_pwhash_OPSLIMIT_INTERACTIVE;
    const std::size_t mem = strength == KdfStrength::Moderate
                                ? crypto_pwhash_MEMLIMIT_MODERATE
                                : crypto_pwhash_MEMLIMIT_INTERACTIVE;

    unsigned char salt[crypto_pwhash_SALTBYTES];
    unsigned char nonce[crypto_aead_xchacha20poly1305_ietf_NPUBBYTES];
    randombytes_buf(salt, sizeof salt);
    randombytes_buf(nonce, sizeof nonce);

    SecureBuffer key(kKeyLen);
    if (!derive_key(key, password, salt, ops, mem)) {
        return CryptoError{CryptoError::Kind::Internal, "key derivation failed"};
    }

    std::vector<std::uint8_t> ciphertext(plaintext.size() + kTagLen);
    unsigned long long ct_len = 0;
    crypto_aead_xchacha20poly1305_ietf_encrypt(
        ciphertext.data(), &ct_len, plaintext.bytes(), plaintext.size(), nullptr, 0,
        nullptr, nonce, key.data());
    ciphertext.resize(static_cast<std::size_t>(ct_len));

    std::vector<std::uint8_t> blob;
    blob.reserve(kHeaderLen + ciphertext.size());
    blob.insert(blob.end(), kMagic.begin(), kMagic.end());
    put_u64le(blob, ops);
    put_u64le(blob, static_cast<std::uint64_t>(mem));
    blob.insert(blob.end(), salt, salt + sizeof salt);
    blob.insert(blob.end(), nonce, nonce + sizeof nonce);
    blob.insert(blob.end(), ciphertext.begin(), ciphertext.end());
    return blob;
}

CryptoResult<SecretString> decrypt(const std::vector<std::uint8_t>& blob,
                                   const SecretString& password) {
    ensure_initialized();
    if (blob.size() < kHeaderLen + kTagLen ||
        std::memcmp(blob.data(), kMagic.data(), kMagic.size()) != 0) {
        return CryptoError{CryptoError::Kind::BadFormat, "not a recognized secret blob"};
    }

    const std::uint64_t ops = get_u64le(blob.data() + kOpsOffset);
    const std::uint64_t mem = get_u64le(blob.data() + kMemOffset);
    const unsigned char* salt = blob.data() + kSaltOffset;
    const unsigned char* nonce = blob.data() + kNonceOffset;
    const unsigned char* ct = blob.data() + kHeaderLen;
    const unsigned long long ct_len = blob.size() - kHeaderLen;

    SecureBuffer key(kKeyLen);
    if (!derive_key(key, password, salt, ops, static_cast<std::size_t>(mem))) {
        return CryptoError{CryptoError::Kind::Internal, "key derivation failed"};
    }

    SecureBuffer plain(static_cast<std::size_t>(ct_len - kTagLen));
    unsigned long long plain_len = 0;
    if (crypto_aead_xchacha20poly1305_ietf_decrypt(plain.data(), &plain_len, nullptr, ct,
                                                   ct_len, nullptr, 0, nonce,
                                                   key.data()) != 0) {
        return CryptoError{CryptoError::Kind::WrongPassword,
                           "wrong password or corrupted data"};
    }
    return SecretString(std::string_view(reinterpret_cast<const char*>(plain.data()),
                                         static_cast<std::size_t>(plain_len)));
}

} // namespace repomancer::secret
