// Copyright (C) 2026 Roman Lyubimov
// SPDX-License-Identifier: GPL-3.0-or-later

#include "util.h"

#include "sha256/sha256.h"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <random>

namespace util {

namespace {

// Single global RNG seeded from std::random_device. Mutex-guarded so callers
// from any thread get independent draws without corrupting state.
std::mt19937_64& rng() {
    static std::mt19937_64 instance{std::random_device{}()};
    return instance;
}
std::mutex& rngMutex() {
    static std::mutex m;
    return m;
}

std::vector<std::uint8_t> randomBytes(std::size_t n) {
    std::vector<std::uint8_t> out(n);
    std::lock_guard<std::mutex> lk(rngMutex());
    auto& r = rng();
    for (std::size_t i = 0; i < n; i += 8) {
        std::uint64_t v = r();
        for (std::size_t j = 0; j < 8 && i + j < n; ++j) {
            out[i + j] = static_cast<std::uint8_t>((v >> (j * 8)) & 0xFFu);
        }
    }
    return out;
}

constexpr char kRoomAlphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-";

constexpr char kHex[] = "0123456789abcdef";

constexpr char kB64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

}  // namespace

std::string randomRoomId() {
    const auto bytes = randomBytes(8);
    std::string s(8, '0');
    for (std::size_t i = 0; i < 8; ++i) {
        s[i] = kRoomAlphabet[bytes[i] & 0x3F];
    }
    return s;
}

std::string randomTokenHex() {
    const auto bytes = randomBytes(32);
    std::string s(64, '0');
    for (std::size_t i = 0; i < 32; ++i) {
        s[2 * i]     = kHex[bytes[i] >> 4];
        s[2 * i + 1] = kHex[bytes[i] & 0x0F];
    }
    return s;
}

std::string randomPeerId() {
    const auto bytes = randomBytes(8);
    std::string s(16, '0');
    for (std::size_t i = 0; i < 8; ++i) {
        s[2 * i]     = kHex[bytes[i] >> 4];
        s[2 * i + 1] = kHex[bytes[i] & 0x0F];
    }
    return s;
}

std::string peerIdFromToken(const std::string& token) {
    tools::SHA256 hasher;
    hasher.update(token);
    const auto digest = hasher.digest();
    std::string s(16, '0');
    for (std::size_t i = 0; i < 8; ++i) {
        s[2 * i]     = kHex[digest[i] >> 4];
        s[2 * i + 1] = kHex[digest[i] & 0x0F];
    }
    return s;
}

std::string base64Encode(const std::vector<std::uint8_t>& bytes) {
    std::string out;
    out.reserve(((bytes.size() + 2) / 3) * 4);
    std::size_t i = 0;
    for (; i + 3 <= bytes.size(); i += 3) {
        std::uint32_t v = (static_cast<std::uint32_t>(bytes[i])     << 16)
                        | (static_cast<std::uint32_t>(bytes[i + 1]) << 8)
                        |  static_cast<std::uint32_t>(bytes[i + 2]);
        out.push_back(kB64[(v >> 18) & 0x3F]);
        out.push_back(kB64[(v >> 12) & 0x3F]);
        out.push_back(kB64[(v >> 6)  & 0x3F]);
        out.push_back(kB64[v         & 0x3F]);
    }
    if (i < bytes.size()) {
        std::uint32_t v = static_cast<std::uint32_t>(bytes[i]) << 16;
        const bool two = (i + 1 < bytes.size());
        if (two) {
            v |= static_cast<std::uint32_t>(bytes[i + 1]) << 8;
        }
        out.push_back(kB64[(v >> 18) & 0x3F]);
        out.push_back(kB64[(v >> 12) & 0x3F]);
        out.push_back(two ? kB64[(v >> 6) & 0x3F] : '=');
        out.push_back('=');
    }
    return out;
}

std::optional<std::vector<std::uint8_t>> base64Decode(const std::string& s) {
    static int8_t table[256];
    static bool inited = [] {
        for (auto& x : table) {
            x = -1;
        }
        for (int i = 0; i < 64; ++i) {
            table[static_cast<unsigned char>(kB64[i])] = static_cast<int8_t>(i);
        }
        return true;
    }();
    (void)inited;

    std::vector<std::uint8_t> out;
    out.reserve(s.size() * 3 / 4);
    std::uint32_t buf = 0;
    int bits = 0;
    for (char c : s) {
        if (c == '=') {
            break;
        }
        if (c == '\n' || c == '\r' || c == ' ' || c == '\t') {
            continue;
        }
        const int8_t v = table[static_cast<unsigned char>(c)];
        if (v < 0) {
            return std::nullopt;
        }
        buf = (buf << 6) | static_cast<std::uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<std::uint8_t>((buf >> bits) & 0xFFu));
        }
    }
    return out;
}

bool ctEqual(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    unsigned char diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        diff |= a[i] ^ b[i];
    }
    return diff == 0;
}

std::int64_t nowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

}  // namespace util
