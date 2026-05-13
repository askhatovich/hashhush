// Copyright (C) 2026 Roman Lyubimov
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <string>

class Config {
public:
    static Config& instance();

    bool loadFromFile(const std::string& path);

    std::string logLevel        = "info";
    std::string bindAddress     = "0.0.0.0";
    uint16_t    bindPort        = 8080;
    std::string appName         = "HashHush";
    // Free-form announcement rendered on the home page below the server-info
    // block. Empty by default so the panel disappears entirely; otherwise the
    // string is injected as raw HTML, so the operator can include links and
    // basic formatting. Treated as trusted: anyone who can edit config.ini
    // can run arbitrary JS in visitors' browsers.
    std::string adminText;

    std::string dbPath          = "/var/lib/hashhush/hashhush.sqlite";

    int maxParticipants         = 10;
    int maxParticipantsCap      = 50;
    int idleTtlSeconds          = 86400;
    int totalRoomsCap           = 0;
    int messageCacheSize        = 5;
    int wsMaxPayloadBytes       = 8192;
    int challengeMaxBlobBytes   = 256;
    // Proof-of-work captcha gating POST /api/rooms. 16 bits ≈ 65k SHA-256
    // tries — fractions of a second client-side, ample friction for bots.
    int powDifficultyBits       = 16;
};
