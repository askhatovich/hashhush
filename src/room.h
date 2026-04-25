// Copyright (C) 2026 Roman Lyubimov
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "crowlib/crow/websocket.h"

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class Database;

// One live WebSocket peer. Lives in RAM only; the persistent record of who is
// allowed to be here is the access_tokens row keyed by token.
struct Peer {
    std::string                  peerId;
    std::string                  roomId;
    crow::websocket::connection* conn = nullptr;
    bool                         joined = false;   // passed challenge / token check
    std::string                  pendingToken;       // token to issue once the challenge is answered
    std::string                  expectedPlaintext;  // base64 plaintext we expect back
};

// One cached encrypted message. Sender id is included so a returning client
// can identify "fromSelf" history without the server inspecting payloads.
struct CachedMessage {
    std::string  senderPeerId;
    std::string  payloadB64;
    std::int64_t createdAt = 0;
};

// Tracks live peers per room (for broadcast and capacity checks).
// Persistent state — rooms, challenges, tokens, message cache — lives in DB.
class RoomManager {
public:
    explicit RoomManager(Database& db) : db_(db) {}

    // Connection lifecycle.
    void onWsAccept(crow::websocket::connection& conn, const std::string& roomId);
    void onWsClose(crow::websocket::connection& conn);

    Peer* findByConn(crow::websocket::connection& conn);
    int   liveCount(const std::string& roomId);

    // Atomically promote a peer to "joined" and stamp its (token-derived)
    // peer id. Done through RoomManager so the write happens under mu_ and
    // other threads doing broadcast/joinedPeerIds see a consistent state
    // (a plain assignment from the WS handler thread would race with
    // those reads).
    void markPeerJoined(crow::websocket::connection& conn,
                        const std::string& peerId);

    // Broadcast a raw text frame to every joined peer of room. If `except` is
    // not null, that connection is skipped (typical for relay).
    void broadcast(const std::string& roomId, const std::string& frame,
                   crow::websocket::connection* except = nullptr);

    // Force-close every peer of a room — used on DELETE.
    void closeRoom(const std::string& roomId, const std::string& reasonFrame);

    // List of joined peer ids in a room (for member-list events).
    std::vector<std::string> joinedPeerIds(const std::string& roomId);

    // RAM-only message cache (ring buffer per room). Persistent storage of
    // encrypted payloads would tax the FS for every send and offers no real
    // value: the server cannot read them, and a fresh joiner only ever
    // needs the last few to reconstruct context.
    void appendCachedMessage(const std::string& roomId,
                             const std::string& senderPeerId,
                             const std::string& payloadB64,
                             int cacheSize);
    std::vector<CachedMessage> recentMessages(const std::string& roomId);
    void dropMessageCache(const std::string& roomId);

private:
    Database& db_;
    std::mutex mu_;
    std::unordered_map<crow::websocket::connection*, std::unique_ptr<Peer>> peers_;
    std::unordered_map<std::string, std::unordered_set<crow::websocket::connection*>> byRoom_;
    std::unordered_map<std::string, std::deque<CachedMessage>>                        msgCache_;
};
