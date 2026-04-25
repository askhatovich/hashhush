// Copyright (C) 2026 Roman Lyubimov
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;

struct RoomRow {
    std::string id;
    std::string name;
    int         maxParticipants = 0;
    std::int64_t createdAt = 0;
    std::int64_t lastActiveAt = 0;
    bool        activated = false;
    bool        requiresPassword = false;
};

struct ChallengePair {
    std::string plaintextB64;
    std::string ciphertextB64;
    std::string nonceB64;
};

class Database {
public:
    static std::unique_ptr<Database> open(const std::string& path);
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    // Rooms.
    bool createRoom(const RoomRow& room);
    std::optional<RoomRow> findRoom(const std::string& id);
    bool deleteRoom(const std::string& id);
    int  countRooms();
    void touchRoom(const std::string& id);
    std::vector<std::string> roomsIdleSince(std::int64_t cutoffEpochSec);
    bool activateRoom(const std::string& id);

    // Challenges.
    bool insertChallenges(const std::string& roomId, const std::vector<ChallengePair>& pairs);
    int  countUnusedChallenges(const std::string& roomId);
    // Atomically picks one unused challenge, marks it used, returns the pair.
    std::optional<ChallengePair> consumeChallenge(const std::string& roomId);

    // Access tokens.
    bool insertAccessToken(const std::string& token, const std::string& roomId);
    // Returns the room id that owns the token, or empty if invalid/unknown.
    std::optional<std::string> roomForToken(const std::string& token);

    // Message cache lives in RoomManager (RAM only) — persisting encrypted
    // payloads to disk would tax the FS for every send and offers no real
    // value, since the server never reads the messages and a fresh joiner
    // only ever needs the last few.

private:
    Database() = default;
    void applySchema();

    sqlite3*   db_ = nullptr;
    std::mutex mu_;
};
