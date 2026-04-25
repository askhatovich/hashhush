// Copyright (C) 2026 Roman Lyubimov
// SPDX-License-Identifier: GPL-3.0-or-later

#include "config.h"
#include "config/inireader.h"

#include <iostream>

Config& Config::instance() {
    static Config c;
    return c;
}

bool Config::loadFromFile(const std::string& path) {
    INIReader r(path);
    if (r.ParseError() < 0) {
        std::cerr << "Failed to load config: " << path << std::endl;
        return false;
    }
    logLevel       = r.Get("server", "log_level",     logLevel);
    bindAddress    = r.Get("server", "bind_address",  bindAddress);
    bindPort       = static_cast<uint16_t>(r.GetInteger("server", "bind_port", bindPort));
    appName        = r.Get("server", "app_name", appName);

    dbPath         = r.Get("storage", "db_path", dbPath);

    defaultMaxParticipants = static_cast<int>(r.GetInteger("room", "default_max_participants", defaultMaxParticipants));
    maxParticipantsCap     = static_cast<int>(r.GetInteger("room", "max_participants_cap",     maxParticipantsCap));
    idleTtlSeconds         = static_cast<int>(r.GetInteger("room", "idle_ttl_seconds",         idleTtlSeconds));
    totalRoomsCap          = static_cast<int>(r.GetInteger("room", "total_rooms_cap",          totalRoomsCap));
    messageCacheSize       = static_cast<int>(r.GetInteger("room", "message_cache_size",       messageCacheSize));
    wsMaxPayloadBytes      = static_cast<int>(r.GetInteger("room", "ws_max_payload_bytes",     wsMaxPayloadBytes));
    challengeMaxBlobBytes  = static_cast<int>(r.GetInteger("room", "challenge_max_blob_bytes", challengeMaxBlobBytes));

    return true;
}
