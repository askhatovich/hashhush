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

    std::string dbPath          = "/var/lib/hashhush/hashhush.sqlite";

    int defaultMaxParticipants  = 10;
    int maxParticipantsCap      = 50;
    int idleTtlSeconds          = 86400;
    int totalRoomsCap           = 0;
    int messageCacheSize        = 5;
    int wsMaxPayloadBytes       = 8192;
    int challengeMaxBlobBytes   = 256;
};
