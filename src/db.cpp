// Copyright (C) 2026 Roman Lyubimov
// SPDX-License-Identifier: GPL-3.0-or-later

#include "db.h"

#include <sqlite3.h>

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace {

constexpr const char* kSchema = R"SQL(
PRAGMA foreign_keys = ON;
PRAGMA journal_mode = WAL;
PRAGMA synchronous  = NORMAL;

CREATE TABLE IF NOT EXISTS rooms (
    id                 TEXT PRIMARY KEY,
    name               TEXT NOT NULL,
    max_participants   INTEGER NOT NULL,
    created_at         INTEGER NOT NULL,
    last_active_at     INTEGER NOT NULL,
    activated          INTEGER NOT NULL DEFAULT 0,
    requires_password  INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS challenges (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    room_id      TEXT NOT NULL REFERENCES rooms(id) ON DELETE CASCADE,
    plaintext_b64  TEXT NOT NULL,
    ciphertext_b64 TEXT NOT NULL,
    nonce_b64      TEXT NOT NULL,
    used         INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX IF NOT EXISTS idx_challenges_room_unused ON challenges(room_id, used);

CREATE TABLE IF NOT EXISTS access_tokens (
    token        TEXT PRIMARY KEY,
    room_id      TEXT NOT NULL REFERENCES rooms(id) ON DELETE CASCADE,
    created_at   INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_tokens_room ON access_tokens(room_id);
)SQL";

void bindText(sqlite3_stmt* st, int i, const std::string& s) {
    sqlite3_bind_text(st, i, s.data(), static_cast<int>(s.size()), SQLITE_TRANSIENT);
}

}  // namespace

std::unique_ptr<Database> Database::open(const std::string& path) {
    std::unique_ptr<Database> d(new Database());
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
    if (sqlite3_open_v2(path.c_str(), &d->db_, flags, nullptr) != SQLITE_OK) {
        std::string msg = "sqlite3_open_v2: ";
        if (d->db_) {
            msg += sqlite3_errmsg(d->db_);
        }
        throw std::runtime_error(msg);
    }
    d->applySchema();
    return d;
}

Database::~Database() {
    if (db_) {
        sqlite3_close(db_);
    }
}

void Database::applySchema() {
    char* err = nullptr;
    if (sqlite3_exec(db_, kSchema, nullptr, nullptr, &err) != SQLITE_OK) {
        std::string msg = "schema: ";
        if (err) { msg += err; sqlite3_free(err); }
        throw std::runtime_error(msg);
    }
}

bool Database::createRoom(const RoomRow& room) {
    std::lock_guard<std::mutex> lk(mu_);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_,
        "INSERT INTO rooms (id, name, max_participants, created_at, last_active_at, activated, requires_password) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)", -1, &st, nullptr);
    bindText(st, 1, room.id);
    bindText(st, 2, room.name);
    sqlite3_bind_int  (st, 3, room.maxParticipants);
    sqlite3_bind_int64(st, 4, room.createdAt);
    sqlite3_bind_int64(st, 5, room.lastActiveAt);
    sqlite3_bind_int  (st, 6, room.activated ? 1 : 0);
    sqlite3_bind_int  (st, 7, room.requiresPassword ? 1 : 0);
    const bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    return ok;
}

std::optional<RoomRow> Database::findRoom(const std::string& id) {
    std::lock_guard<std::mutex> lk(mu_);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_,
        "SELECT id, name, max_participants, created_at, last_active_at, activated, requires_password "
        "FROM rooms WHERE id = ?", -1, &st, nullptr);
    bindText(st, 1, id);
    std::optional<RoomRow> result;
    if (sqlite3_step(st) == SQLITE_ROW) {
        RoomRow r;
        r.id   = reinterpret_cast<const char*>(sqlite3_column_text(st, 0));
        r.name = reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
        r.maxParticipants  = sqlite3_column_int(st, 2);
        r.createdAt        = sqlite3_column_int64(st, 3);
        r.lastActiveAt     = sqlite3_column_int64(st, 4);
        r.activated        = sqlite3_column_int(st, 5) != 0;
        r.requiresPassword = sqlite3_column_int(st, 6) != 0;
        result = std::move(r);
    }
    sqlite3_finalize(st);
    return result;
}

bool Database::deleteRoom(const std::string& id) {
    std::lock_guard<std::mutex> lk(mu_);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_, "DELETE FROM rooms WHERE id = ?", -1, &st, nullptr);
    bindText(st, 1, id);
    bool ok = sqlite3_step(st) == SQLITE_DONE && sqlite3_changes(db_) > 0;
    sqlite3_finalize(st);
    return ok;
}

int Database::countRooms() {
    std::lock_guard<std::mutex> lk(mu_);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM rooms", -1, &st, nullptr);
    int n = (sqlite3_step(st) == SQLITE_ROW) ? sqlite3_column_int(st, 0) : 0;
    sqlite3_finalize(st);
    return n;
}

void Database::touchRoom(const std::string& id) {
    std::lock_guard<std::mutex> lk(mu_);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_,
        "UPDATE rooms SET last_active_at = strftime('%s','now') WHERE id = ?",
        -1, &st, nullptr);
    bindText(st, 1, id);
    sqlite3_step(st);
    sqlite3_finalize(st);
}

std::vector<std::string> Database::roomsIdleSince(std::int64_t cutoffEpochSec) {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<std::string> out;
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_, "SELECT id FROM rooms WHERE last_active_at < ?", -1, &st, nullptr);
    sqlite3_bind_int64(st, 1, cutoffEpochSec);
    while (sqlite3_step(st) == SQLITE_ROW) {
        out.emplace_back(reinterpret_cast<const char*>(sqlite3_column_text(st, 0)));
    }
    sqlite3_finalize(st);
    return out;
}

bool Database::activateRoom(const std::string& id) {
    std::lock_guard<std::mutex> lk(mu_);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_,
        "UPDATE rooms SET activated = 1, last_active_at = strftime('%s','now') WHERE id = ?",
        -1, &st, nullptr);
    bindText(st, 1, id);
    bool ok = sqlite3_step(st) == SQLITE_DONE && sqlite3_changes(db_) > 0;
    sqlite3_finalize(st);
    return ok;
}

bool Database::insertChallenges(const std::string& roomId, const std::vector<ChallengePair>& pairs) {
    std::lock_guard<std::mutex> lk(mu_);
    sqlite3_exec(db_, "BEGIN", nullptr, nullptr, nullptr);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_,
        "INSERT INTO challenges (room_id, plaintext_b64, ciphertext_b64, nonce_b64) "
        "VALUES (?, ?, ?, ?)", -1, &st, nullptr);
    bool ok = true;
    for (const auto& p : pairs) {
        sqlite3_reset(st);
        sqlite3_clear_bindings(st);
        bindText(st, 1, roomId);
        bindText(st, 2, p.plaintextB64);
        bindText(st, 3, p.ciphertextB64);
        bindText(st, 4, p.nonceB64);
        if (sqlite3_step(st) != SQLITE_DONE) { ok = false; break; }
    }
    sqlite3_finalize(st);
    sqlite3_exec(db_, ok ? "COMMIT" : "ROLLBACK", nullptr, nullptr, nullptr);
    return ok;
}

int Database::countUnusedChallenges(const std::string& roomId) {
    std::lock_guard<std::mutex> lk(mu_);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_,
        "SELECT COUNT(*) FROM challenges WHERE room_id = ? AND used = 0", -1, &st, nullptr);
    bindText(st, 1, roomId);
    int n = (sqlite3_step(st) == SQLITE_ROW) ? sqlite3_column_int(st, 0) : 0;
    sqlite3_finalize(st);
    return n;
}

std::optional<ChallengePair> Database::consumeChallenge(const std::string& roomId) {
    std::lock_guard<std::mutex> lk(mu_);
    sqlite3_exec(db_, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr);

    sqlite3_stmt* sel = nullptr;
    sqlite3_prepare_v2(db_,
        "SELECT id, plaintext_b64, ciphertext_b64, nonce_b64 "
        "FROM challenges WHERE room_id = ? AND used = 0 ORDER BY id LIMIT 1",
        -1, &sel, nullptr);
    bindText(sel, 1, roomId);

    std::optional<ChallengePair> result;
    std::int64_t cid = 0;
    if (sqlite3_step(sel) == SQLITE_ROW) {
        cid = sqlite3_column_int64(sel, 0);
        ChallengePair p;
        p.plaintextB64  = reinterpret_cast<const char*>(sqlite3_column_text(sel, 1));
        p.ciphertextB64 = reinterpret_cast<const char*>(sqlite3_column_text(sel, 2));
        p.nonceB64      = reinterpret_cast<const char*>(sqlite3_column_text(sel, 3));
        result = std::move(p);
    }
    sqlite3_finalize(sel);

    if (result) {
        sqlite3_stmt* upd = nullptr;
        sqlite3_prepare_v2(db_, "UPDATE challenges SET used = 1 WHERE id = ?", -1, &upd, nullptr);
        sqlite3_bind_int64(upd, 1, cid);
        sqlite3_step(upd);
        sqlite3_finalize(upd);
        sqlite3_exec(db_, "COMMIT", nullptr, nullptr, nullptr);
    } else {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
    }
    return result;
}

bool Database::insertAccessToken(const std::string& token, const std::string& roomId) {
    std::lock_guard<std::mutex> lk(mu_);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_,
        "INSERT INTO access_tokens (token, room_id, created_at) "
        "VALUES (?, ?, strftime('%s','now'))", -1, &st, nullptr);
    bindText(st, 1, token);
    bindText(st, 2, roomId);
    bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    return ok;
}

std::optional<std::string> Database::roomForToken(const std::string& token) {
    std::lock_guard<std::mutex> lk(mu_);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_,
        "SELECT room_id FROM access_tokens WHERE token = ?", -1, &st, nullptr);
    bindText(st, 1, token);
    std::optional<std::string> result;
    if (sqlite3_step(st) == SQLITE_ROW) {
        result = std::string(reinterpret_cast<const char*>(sqlite3_column_text(st, 0)));
    }
    sqlite3_finalize(st);
    return result;
}

