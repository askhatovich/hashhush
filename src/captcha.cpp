// Copyright (C) 2026 Roman Lyubimov
// SPDX-License-Identifier: GPL-3.0-or-later

#include "captcha.h"

#include "sha256/sha256.h"
#include "util.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace captcha {

namespace {

constexpr char kHex[] = "0123456789abcdef";
constexpr std::size_t kSecretBytes = 32;

// HMAC block size for SHA-256.
constexpr std::size_t kBlockBytes = 64;

std::array<std::uint8_t, kSecretBytes>& secret() {
    static std::array<std::uint8_t, kSecretBytes> s{};
    return s;
}

std::mutex& secretMutex() {
    static std::mutex m;
    return m;
}

bool& inited() {
    static bool b = false;
    return b;
}

// Single-use ledger: nonce_hex → exp_unix. Entries past exp are GC'd lazily
// on each insert, so the map stays bounded by `rate * validity_window`.
struct Ledger {
    std::mutex mu;
    std::unordered_map<std::string, std::int64_t> seen;
};

Ledger& ledger() {
    static Ledger l;
    return l;
}

std::string toHex(const std::uint8_t* data, std::size_t n) {
    std::string s(n * 2, '0');
    for (std::size_t i = 0; i < n; ++i) {
        s[2 * i]     = kHex[data[i] >> 4];
        s[2 * i + 1] = kHex[data[i] & 0x0F];
    }
    return s;
}

bool ctEqualStr(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) {
        return false;
    }
    unsigned char diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        diff |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
    }
    return diff == 0;
}

// HMAC-SHA256 (RFC 2104) built on top of the bundled SHA-256.
std::array<std::uint8_t, 32> hmacSha256(const std::uint8_t* key, std::size_t keyLen,
                                        const std::uint8_t* msg, std::size_t msgLen) {
    std::uint8_t kPad[kBlockBytes] = {0};
    if (keyLen > kBlockBytes) {
        tools::SHA256 h;
        h.update(key, keyLen);
        auto d = h.digest();
        for (std::size_t i = 0; i < d.size(); ++i) {
            kPad[i] = d[i];
        }
    } else {
        for (std::size_t i = 0; i < keyLen; ++i) {
            kPad[i] = key[i];
        }
    }

    std::uint8_t ipad[kBlockBytes];
    std::uint8_t opad[kBlockBytes];
    for (std::size_t i = 0; i < kBlockBytes; ++i) {
        ipad[i] = kPad[i] ^ 0x36;
        opad[i] = kPad[i] ^ 0x5C;
    }

    tools::SHA256 inner;
    inner.update(ipad, kBlockBytes);
    inner.update(msg, msgLen);
    const auto innerDigest = inner.digest();

    tools::SHA256 outer;
    outer.update(opad, kBlockBytes);
    outer.update(innerDigest.data(), innerDigest.size());
    return outer.digest();
}

std::string hmacHex(const std::string& payload) {
    std::lock_guard<std::mutex> lk(secretMutex());
    auto mac = hmacSha256(secret().data(), secret().size(),
                          reinterpret_cast<const std::uint8_t*>(payload.data()),
                          payload.size());
    return toHex(mac.data(), mac.size());
}

bool meetsDifficulty(const std::uint8_t* digest, int bits) {
    int full = bits / 8;
    int rem  = bits % 8;
    for (int i = 0; i < full; ++i) {
        if (digest[i] != 0) {
            return false;
        }
    }
    if (rem > 0) {
        const std::uint8_t mask = static_cast<std::uint8_t>(0xFF << (8 - rem));
        if ((digest[full] & mask) != 0) {
            return false;
        }
    }
    return true;
}

// Split "a:b:c" into three parts. Returns false if the shape is wrong.
bool split3(const std::string& s, std::string& a, std::string& b, std::string& c) {
    const auto p1 = s.find(':');
    if (p1 == std::string::npos) {
        return false;
    }
    const auto p2 = s.find(':', p1 + 1);
    if (p2 == std::string::npos) {
        return false;
    }
    if (s.find(':', p2 + 1) != std::string::npos) {
        return false;
    }
    a = s.substr(0, p1);
    b = s.substr(p1 + 1, p2 - p1 - 1);
    c = s.substr(p2 + 1);
    return true;
}

void gcLocked(std::unordered_map<std::string, std::int64_t>& m, std::int64_t now) {
    for (auto it = m.begin(); it != m.end(); ) {
        if (it->second <= now) {
            it = m.erase(it);
        } else {
            ++it;
        }
    }
}

}  // namespace

void init() {
    std::lock_guard<std::mutex> lk(secretMutex());
    if (inited()) {
        return;
    }
    auto bytes = util::randomBytesVec(kSecretBytes);
    for (std::size_t i = 0; i < kSecretBytes; ++i) {
        secret()[i] = bytes[i];
    }
    inited() = true;
}

std::string issueChallenge(int validityWindowSeconds) {
    const auto nonceBytes = util::randomBytesVec(16);
    const std::string nonceHex = toHex(nonceBytes.data(), nonceBytes.size());
    const std::int64_t exp = util::nowSeconds() + validityWindowSeconds;
    const std::string payload = nonceHex + ":" + std::to_string(exp);
    return payload + ":" + hmacHex(payload);
}

VerifyResult verifyChallenge(const std::string& challenge,
                             const std::string& nonce,
                             int difficultyBits) {
    std::string nonceHex;
    std::string expStr;
    std::string macHex;
    if (!split3(challenge, nonceHex, expStr, macHex)) {
        return VerifyResult::Malformed;
    }
    if (nonceHex.empty() || expStr.empty() || macHex.empty()) {
        return VerifyResult::Malformed;
    }

    // HMAC check on the same payload the issuer signed.
    const std::string payload = nonceHex + ":" + expStr;
    const std::string expected = hmacHex(payload);
    if (!ctEqualStr(expected, macHex)) {
        return VerifyResult::BadHmac;
    }

    // Expiry check (after HMAC, so a forged token never reveals timing).
    std::int64_t exp = 0;
    try {
        exp = std::stoll(expStr);
    } catch (...) {
        return VerifyResult::Malformed;
    }
    const std::int64_t now = util::nowSeconds();
    if (exp <= now) {
        return VerifyResult::Expired;
    }

    // Proof check: sha256(challenge_token || nonce) has `difficultyBits`
    // leading zero bits. The full token is hashed (not just the inner
    // payload) so a client cannot pre-mine against a known payload before
    // the server signs it.
    tools::SHA256 h;
    h.update(reinterpret_cast<const std::uint8_t*>(challenge.data()), challenge.size());
    h.update(reinterpret_cast<const std::uint8_t*>(nonce.data()), nonce.size());
    const auto digest = h.digest();
    if (!meetsDifficulty(digest.data(), difficultyBits)) {
        return VerifyResult::BadProof;
    }

    // Single-use: record nonce_hex with the token's own exp so the entry
    // self-cleans at the same time the token would have expired anyway.
    auto& l = ledger();
    std::lock_guard<std::mutex> lk(l.mu);
    gcLocked(l.seen, now);
    auto [it, inserted] = l.seen.emplace(nonceHex, exp);
    if (!inserted) {
        return VerifyResult::Reused;
    }
    return VerifyResult::Ok;
}

}  // namespace captcha
