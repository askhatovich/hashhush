// Copyright (C) 2026 Roman Lyubimov
// SPDX-License-Identifier: GPL-3.0-or-later

#include "room.h"

#include "crowlib/crow/json.h"
#include "db.h"
#include "util.h"

#include <utility>

void RoomManager::onWsAccept(crow::websocket::connection& conn, const std::string& roomId) {
    auto p = std::make_unique<Peer>();
    p->peerId = util::randomPeerId();
    p->roomId = roomId;
    p->conn   = &conn;

    std::lock_guard<std::mutex> lk(mu_);
    byRoom_[roomId].insert(&conn);
    peers_.emplace(&conn, std::move(p));
}

void RoomManager::onWsClose(crow::websocket::connection& conn) {
    std::string roomId;
    std::string peerId;
    bool wasJoined = false;
    {
        std::lock_guard<std::mutex> lk(mu_);
        const auto it = peers_.find(&conn);
        if (it == peers_.end()) {
            return;
        }
        roomId    = it->second->roomId;
        peerId    = it->second->peerId;
        wasJoined = it->second->joined;
        const auto rit = byRoom_.find(roomId);
        if (rit != byRoom_.end()) {
            rit->second.erase(&conn);
            if (rit->second.empty()) {
                byRoom_.erase(rit);
            }
        }
        peers_.erase(it);
    }
    // Tell remaining peers the room shrank. Done outside the lock — broadcast
    // re-acquires mu_ and dispatches IO, which we don't want to block on while
    // holding it. Anonymous peers that never finalised join produce no event.
    if (wasJoined) {
        crow::json::wvalue f;
        f["type"]    = "peer_leave";
        f["peer_id"] = peerId;
        broadcast(roomId, f.dump(), nullptr);
    }
}

Peer* RoomManager::findByConn(crow::websocket::connection& conn) {
    std::lock_guard<std::mutex> lk(mu_);
    const auto it = peers_.find(&conn);
    return it == peers_.end() ? nullptr : it->second.get();
}

void RoomManager::markPeerJoined(crow::websocket::connection& conn,
                                 const std::string& peerId) {
    std::lock_guard<std::mutex> lk(mu_);
    const auto it = peers_.find(&conn);
    if (it == peers_.end()) {
        return;
    }
    it->second->peerId = peerId;
    it->second->joined = true;
}

int RoomManager::liveCount(const std::string& roomId) {
    std::lock_guard<std::mutex> lk(mu_);
    const auto it = byRoom_.find(roomId);
    if (it == byRoom_.end()) {
        return 0;
    }
    int n = 0;
    for (auto* c : it->second) {
        const auto p = peers_.find(c);
        if (p != peers_.end() && p->second->joined) {
            ++n;
        }
    }
    return n;
}

void RoomManager::broadcast(const std::string& roomId, const std::string& frame,
                            crow::websocket::connection* except) {
    std::vector<crow::websocket::connection*> targets;
    {
        std::lock_guard<std::mutex> lk(mu_);
        const auto it = byRoom_.find(roomId);
        if (it == byRoom_.end()) {
            return;
        }
        for (auto* c : it->second) {
            if (c == except) {
                continue;
            }
            const auto p = peers_.find(c);
            if (p != peers_.end() && p->second->joined) {
                targets.push_back(c);
            }
        }
    }
    // send_text is thread-safe in Crow; do it outside the lock to avoid
    // holding the manager mutex across IO.
    for (auto* c : targets) {
        c->send_text(frame);
    }
}

void RoomManager::closeRoom(const std::string& roomId, const std::string& reasonFrame) {
    std::vector<crow::websocket::connection*> targets;
    {
        std::lock_guard<std::mutex> lk(mu_);
        const auto it = byRoom_.find(roomId);
        if (it != byRoom_.end()) {
            targets.assign(it->second.begin(), it->second.end());
        }
        // Forget cached messages — the room is gone, no future joiner will
        // ever need them, and the bytes shouldn't linger in RAM.
        msgCache_.erase(roomId);
    }
    for (auto* c : targets) {
        c->send_text(reasonFrame);
        c->close("room deleted");
    }
}

void RoomManager::appendCachedMessage(const std::string& roomId,
                                      const std::string& senderPeerId,
                                      const std::string& payloadB64,
                                      int cacheSize) {
    if (cacheSize <= 0) {
        return;
    }
    std::lock_guard<std::mutex> lk(mu_);
    auto& dq = msgCache_[roomId];
    dq.push_back(CachedMessage{senderPeerId, payloadB64, util::nowSeconds()});
    while (static_cast<int>(dq.size()) > cacheSize) {
        dq.pop_front();
    }
}

std::vector<CachedMessage> RoomManager::recentMessages(const std::string& roomId) {
    std::lock_guard<std::mutex> lk(mu_);
    const auto it = msgCache_.find(roomId);
    if (it == msgCache_.end()) {
        return {};
    }
    return std::vector<CachedMessage>(it->second.begin(), it->second.end());
}

void RoomManager::dropMessageCache(const std::string& roomId) {
    std::lock_guard<std::mutex> lk(mu_);
    msgCache_.erase(roomId);
}

std::vector<std::string> RoomManager::joinedPeerIds(const std::string& roomId) {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<std::string> out;
    const auto it = byRoom_.find(roomId);
    if (it == byRoom_.end()) {
        return out;
    }
    for (auto* c : it->second) {
        const auto p = peers_.find(c);
        if (p != peers_.end() && p->second->joined) {
            out.push_back(p->second->peerId);
        }
    }
    return out;
}
