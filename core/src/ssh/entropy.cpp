// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include <repomancer/ssh/entropy.h>

#include <repomancer/secret/secure_buffer.h> // ensure_initialized()

#include <sodium.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace repomancer::ssh {
namespace {

namespace fs = std::filesystem;
constexpr std::size_t kSeedBytes = crypto_sign_SEEDBYTES;   // 32
constexpr std::size_t kPubBytes = crypto_sign_PUBLICKEYBYTES; // 32
constexpr std::size_t kSecBytes = crypto_sign_SECRETKEYBYTES; // 64 = seed||pub
constexpr std::size_t kOsDrawBytes = 64;

// ---- SSH wire encoding: length-prefixed strings, big-endian u32 ------------

void put_u32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v >> 24));
    out.push_back(static_cast<std::uint8_t>(v >> 16));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
    out.push_back(static_cast<std::uint8_t>(v));
}

void put_string(std::vector<std::uint8_t>& out, const void* data, std::size_t size) {
    put_u32(out, static_cast<std::uint32_t>(size));
    const auto* p = static_cast<const std::uint8_t*>(data);
    out.insert(out.end(), p, p + size);
}

void put_string(std::vector<std::uint8_t>& out, const std::string& s) {
    put_string(out, s.data(), s.size());
}

// The "ssh-ed25519" public-key blob: [string type][string pub32]. This is both
// the .pub payload and the public section of the private file.
std::vector<std::uint8_t> ed25519_public_blob(const std::uint8_t* pub) {
    std::vector<std::uint8_t> blob;
    put_string(blob, std::string("ssh-ed25519"));
    put_string(blob, pub, kPubBytes);
    return blob;
}

std::string base64(const std::vector<std::uint8_t>& raw) {
    const std::size_t max = sodium_base64_ENCODED_LEN(raw.size(),
                                                      sodium_base64_VARIANT_ORIGINAL);
    std::string out(max, '\0');
    sodium_bin2base64(out.data(), out.size(), raw.data(), raw.size(),
                      sodium_base64_VARIANT_ORIGINAL);
    out.resize(std::strlen(out.c_str())); // drop the NUL sodium appends
    return out;
}

// The openssh-key-v1 container for one unencrypted Ed25519 key. Layout verified
// against a real ssh-keygen key: magic, cipher/kdf "none", one key, the public
// blob, then the private section (twin checkints, type, pub, seed||pub,
// comment, 1,2,3… padding to the 8-byte block size).
std::vector<std::uint8_t> openssh_private_blob(const std::uint8_t* pub,
                                               const std::uint8_t* sec,
                                               const std::string& comment) {
    std::vector<std::uint8_t> priv;
    std::uint32_t check = randombytes_random();
    put_u32(priv, check);
    put_u32(priv, check); // both checkints must match after decryption
    put_string(priv, std::string("ssh-ed25519"));
    put_string(priv, pub, kPubBytes);
    put_string(priv, sec, kSecBytes);
    put_string(priv, comment);
    for (std::uint8_t pad = 1; priv.size() % 8 != 0; ++pad) {
        priv.push_back(pad);
    }

    static const char kMagic[] = "openssh-key-v1";
    std::vector<std::uint8_t> out(kMagic, kMagic + sizeof kMagic); // includes NUL
    put_string(out, std::string("none")); // ciphername
    put_string(out, std::string("none")); // kdfname
    put_string(out, std::string());       // kdfoptions
    put_u32(out, 1);                      // number of keys
    const auto pub_blob = ed25519_public_blob(pub);
    put_string(out, pub_blob.data(), pub_blob.size());
    put_string(out, priv.data(), priv.size());
    sodium_memzero(priv.data(), priv.size());
    return out;
}

SshError io_error(std::string msg) {
    return SshError{SshError::Kind::IoError, std::move(msg)};
}

// PEM body wrapped at 70 columns, as ssh-keygen writes it.
std::string pem_wrap(const std::string& b64) {
    std::string out;
    for (std::size_t i = 0; i < b64.size(); i += 70) {
        out += b64.substr(i, 70);
        out += '\n';
    }
    return out;
}

} // namespace

EntropyPool::EntropyPool() : state_(crypto_generichash_BYTES_MAX) {
    secret::ensure_initialized();
    // Seed the pool from the OS CSPRNG — the security floor, present before any
    // user sample is absorbed.
    std::vector<std::uint8_t> os(kOsDrawBytes);
    randombytes_buf(os.data(), os.size());
    crypto_generichash(state_.data(), state_.size(), os.data(), os.size(), nullptr, 0);
    sodium_memzero(os.data(), os.size());
}

void EntropyPool::absorb(const void* data, std::size_t size) {
    // state = H(state || sample) — order-dependent, so the sequence of samples
    // matters, and no sample can ever remove entropy already in the pool.
    std::vector<std::uint8_t> buf;
    buf.reserve(state_.size() + size);
    buf.insert(buf.end(), state_.begin(), state_.end());
    const auto* p = static_cast<const std::uint8_t*>(data);
    buf.insert(buf.end(), p, p + size);
    crypto_generichash(state_.data(), state_.size(), buf.data(), buf.size(), nullptr, 0);
    sodium_memzero(buf.data(), buf.size());
    ++samples_;
}

void EntropyPool::absorb_point(std::int32_t x, std::int32_t y,
                               std::int64_t timestamp_us) {
    std::uint8_t sample[16];
    std::memcpy(sample, &x, 4);
    std::memcpy(sample + 4, &y, 4);
    std::memcpy(sample + 8, &timestamp_us, 8);
    absorb(sample, sizeof sample);
}

std::vector<std::uint8_t> EntropyPool::finalize_seed() {
    // Fold in a second OS draw at the end: even a hostile "sample" stream
    // cannot steer the final seed.
    std::vector<std::uint8_t> os(kOsDrawBytes);
    randombytes_buf(os.data(), os.size());
    absorb(os.data(), os.size());
    sodium_memzero(os.data(), os.size());

    std::vector<std::uint8_t> seed(kSeedBytes);
    crypto_generichash(seed.data(), seed.size(), state_.data(), state_.size(), nullptr,
                       0);
    return seed;
}

SshResult<KeyInfo> generate_from_seed(const GenerateRequest& req,
                                      const std::vector<std::uint8_t>& seed,
                                      const SshConfig& cfg) {
    if (req.type != KeyType::Ed25519) {
        return SshError{SshError::Kind::InvalidRequest,
                        "mouse-entropy generation supports Ed25519 keys only"};
    }
    if (seed.size() != kSeedBytes) {
        return SshError{SshError::Kind::InvalidRequest, "seed must be 32 bytes"};
    }
    if (req.path.empty()) {
        return SshError{SshError::Kind::InvalidRequest, "no output path given"};
    }
    std::error_code ec;
    const fs::path pub_path(req.path.string() + ".pub");
    if (fs::exists(req.path, ec) || fs::exists(pub_path, ec)) {
        return SshError{SshError::Kind::InvalidRequest,
                        "a key already exists at " + req.path.string()};
    }
    secret::ensure_initialized();

    std::uint8_t pub[kPubBytes];
    secret::SecureBuffer sec(kSecBytes); // seed||pub — guarded and wiped
    if (crypto_sign_seed_keypair(pub, sec.data(), seed.data()) != 0) {
        return SshError{SshError::Kind::Internal, "Ed25519 key derivation failed"};
    }

    if (req.path.has_parent_path()) {
        fs::create_directories(req.path.parent_path(), ec);
        fs::permissions(req.path.parent_path(), fs::perms::owner_all,
                        fs::perm_options::replace, ec);
    }

    // Private key: PEM-armoured openssh-key-v1, 0600.
    {
        auto blob = openssh_private_blob(pub, sec.data(), req.comment);
        std::string pem = "-----BEGIN OPENSSH PRIVATE KEY-----\n" +
                          pem_wrap(base64(blob)) + "-----END OPENSSH PRIVATE KEY-----\n";
        sodium_memzero(blob.data(), blob.size());
        std::ofstream out(req.path, std::ios::binary | std::ios::trunc);
        if (!out) {
            return io_error("could not write " + req.path.string());
        }
        out << pem;
        out.close();
        sodium_memzero(pem.data(), pem.size());
        if (out.fail()) {
            return io_error("could not write " + req.path.string());
        }
        fs::permissions(req.path, fs::perms::owner_read | fs::perms::owner_write,
                        fs::perm_options::replace, ec);
    }
    // Public key: "ssh-ed25519 <base64> [comment]".
    {
        std::string line = "ssh-ed25519 " + base64(ed25519_public_blob(pub));
        if (!req.comment.empty()) {
            line += " " + req.comment;
        }
        std::ofstream out(pub_path, std::ios::binary | std::ios::trunc);
        if (!out) {
            return io_error("could not write " + pub_path.string());
        }
        out << line << "\n";
        out.close();
        if (out.fail()) {
            return io_error("could not write " + pub_path.string());
        }
    }

    // Apply protection through ssh-keygen -p rather than reimplementing the
    // encrypted private-key format (bcrypt_pbkdf + AES).
    if (!req.passphrase.empty()) {
        auto protect = change_passphrase(req.path, std::string(), req.passphrase, cfg);
        if (!protect.ok()) {
            std::error_code rm;
            fs::remove(req.path, rm); // never leave an unprotected key behind
            fs::remove(pub_path, rm);
            return protect.error();
        }
    }
    return inspect(req.path, cfg);
}

} // namespace repomancer::ssh
