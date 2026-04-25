// Copyright (C) 2026 Roman Lyubimov
// SPDX-License-Identifier: GPL-3.0-or-later

#include "webapi.h"

#include "config.h"
#include "db.h"
#include "generated_index_html.h"
#include "log.h"
#include "room.h"
#include "util.h"
#include "version.h"

#include "crowlib/crow/json.h"
#include "crowlib/crow/websocket.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>

namespace {

// Lightweight FNV-1a over the bundle. Cheap and good enough for an ETag —
// any change to the embedded bytes flips it. Hex-encoded.
std::string bundleEtagFor(const std::string& html) {
    std::uint64_t h = 0xcbf29ce484222325ULL;
    for (unsigned char c : html) {
        h ^= c;
        h *= 0x100000001b3ULL;
    }
    char buf[20];
    std::snprintf(buf, sizeof(buf), "\"%016llx\"", static_cast<unsigned long long>(h));
    return buf;
}

crow::response jsonError(int status, const char* code, const char* msg = nullptr) {
    crow::json::wvalue v;
    v["error_code"] = code;
    if (msg) {
        v["message"] = msg;
    }
    crow::response r{status, v};
    r.add_header("Content-Type", "application/json");
    return r;
}

// Read a string field from a JSON value, with a default.
std::string js_str(const crow::json::rvalue& v, const char* key, const std::string& def = "") {
    if (v.has(key) && v[key].t() == crow::json::type::String) {
        return std::string(v[key].s());
    }
    return def;
}
int js_int(const crow::json::rvalue& v, const char* key, int def) {
    if (v.has(key) && v[key].t() == crow::json::type::Number) {
        return static_cast<int>(v[key].i());
    }
    return def;
}
bool js_bool(const crow::json::rvalue& v, const char* key, bool def) {
    if (v.has(key)) {
        const auto t = v[key].t();
        if (t == crow::json::type::True)  return true;
        if (t == crow::json::type::False) return false;
    }
    return def;
}

}  // namespace

WebAPI::WebAPI(Database& db, RoomManager& rooms)
    : db_(db), rooms_(rooms) {
    bundleHtml_ = getIndexHtml();
    bundleEtag_ = bundleEtagFor(bundleHtml_);
    initRoutes();
}

void WebAPI::run() {
    auto& cfg = Config::instance();
    app_.signal_clear();   // main installs SIGINT/SIGTERM handlers explicitly.
    app_.bindaddr(cfg.bindAddress).port(cfg.bindPort).multithreaded().run();
}

void WebAPI::stop() {
    app_.stop();
}

void WebAPI::initRoutes() {
    // NB: lambdas below capture `this` and read member references / the
    // Config singleton on demand. Capturing local aliases would dangle once
    // initRoutes() returns.

    // --- Static: SPA bundle at root, with ETag-based caching. ---
    const auto serveBundle = [this](const crow::request& req) {
        const auto inm = req.get_header_value("If-None-Match");
        if (!inm.empty() && inm == bundleEtag_) {
            crow::response r304(304);
            r304.add_header("ETag", bundleEtag_);
            r304.add_header("Cache-Control", "public, max-age=0, must-revalidate");
            return r304;
        }
        crow::response r(bundleHtml_);
        r.add_header("Content-Type", "text/html; charset=utf-8");
        r.add_header("ETag", bundleEtag_);
        r.add_header("Cache-Control", "public, max-age=0, must-revalidate");
        return r;
    };
    // Single canonical path. The room id and the encryption-key seed both
    // live in the URL fragment (`#<id>:<seed>`), never in the request line,
    // so the server's HTTP layer sees no room-specific information here.
    CROW_ROUTE(app_, "/")(serveBundle);

    // --- POST /api/rooms — create room, return required challenge count. ---
    CROW_ROUTE(app_, "/api/rooms").methods(crow::HTTPMethod::POST)
    ([this](const crow::request& req) {
        auto& db = db_;
        const auto& cfg = Config::instance();
        auto body = crow::json::load(req.body);
        if (!body) {
            return jsonError(400, "bad_json");
        }

        auto name = js_str(body, "name", "Secret chat");
        if (name.size() > 80) {
            name.resize(80);
        }
        // Capacity is server-side policy — clients have no say in it.
        const int capacity = std::min(cfg.defaultMaxParticipants, cfg.maxParticipantsCap);
        const bool requiresPassword = js_bool(body, "requires_password", false);
        // Bigger challenge pool when a password gates the room. The pool also
        // bounds password-guessing attempts (each wrong guess consumes one
        // challenge), so a wider pool is the trade we accept for the extra
        // security factor.
        const int poolMultiplier = requiresPassword ? 10 : 5;

        if (cfg.totalRoomsCap > 0 && db.countRooms() >= cfg.totalRoomsCap) {
            return jsonError(503, "rooms_capacity_reached");
        }

        RoomRow r;
        r.id   = util::randomRoomId();
        r.name = name;
        r.maxParticipants  = capacity;
        r.createdAt = r.lastActiveAt = util::nowSeconds();
        r.activated        = false;
        r.requiresPassword = requiresPassword;
        if (!db.createRoom(r)) {
            return jsonError(500, "db_create_failed");
        }

        crow::json::wvalue out;
        out["room_id"]            = r.id;
        out["max_participants"]   = r.maxParticipants;
        out["challenges_required"] = r.maxParticipants * poolMultiplier;
        out["requires_password"]   = r.requiresPassword;
        crow::response resp{out};
        resp.add_header("Content-Type", "application/json");
        return resp;
    });

    // --- POST /api/rooms/<id>/activate — submit challenge pool, then room is live. ---
    CROW_ROUTE(app_, "/api/rooms/<string>/activate").methods(crow::HTTPMethod::POST)
    ([this](const crow::request& req, const std::string& id) {
        auto& db = db_;
        const auto& cfg = Config::instance();
        const auto roomOpt = db.findRoom(id);
        if (!roomOpt) {
            return jsonError(404, "room_not_found");
        }
        if (roomOpt->activated) {
            return jsonError(409, "already_activated");
        }

        auto body = crow::json::load(req.body);
        if (!body || !body.has("challenges") || body["challenges"].t() != crow::json::type::List) {
            return jsonError(400, "bad_json");
        }
        const int required = roomOpt->maxParticipants * (roomOpt->requiresPassword ? 10 : 5);
        const auto& arr = body["challenges"];
        if (static_cast<int>(arr.size()) != required) {
            return jsonError(400, "wrong_challenge_count");
        }

        std::vector<ChallengePair> pairs;
        pairs.reserve(required);
        for (size_t i = 0; i < arr.size(); ++i) {
            const auto& el = arr[i];
            ChallengePair p;
            p.plaintextB64  = js_str(el, "plaintext");
            p.ciphertextB64 = js_str(el, "ciphertext");
            p.nonceB64      = js_str(el, "nonce");
            if (p.plaintextB64.empty() || p.ciphertextB64.empty() || p.nonceB64.empty()) {
                return jsonError(400, "bad_challenge_field");
            }
            const int cap = cfg.challengeMaxBlobBytes * 2;  // base64 inflates ~4/3, slack
            if (static_cast<int>(p.plaintextB64.size())  > cap ||
                static_cast<int>(p.ciphertextB64.size()) > cap ||
                static_cast<int>(p.nonceB64.size())      > cap) {
                return jsonError(400, "challenge_too_large");
            }
            pairs.push_back(std::move(p));
        }

        if (!db.insertChallenges(id, pairs)) {
            return jsonError(500, "db_insert_failed");
        }
        if (!db.activateRoom(id)) {
            return jsonError(500, "db_activate_failed");
        }

        crow::json::wvalue out;
        out["ok"] = true;
        crow::response resp{out};
        resp.add_header("Content-Type", "application/json");
        return resp;
    });

    // --- POST /api/is_alive — bulk room status check. ---
    CROW_ROUTE(app_, "/api/is_alive").methods(crow::HTTPMethod::POST)
    ([this](const crow::request& req) {
        auto& db = db_;
        auto body = crow::json::load(req.body);
        crow::json::wvalue out;
        std::vector<crow::json::wvalue> alive;
        std::vector<crow::json::wvalue> gone;
        if (body && body.has("room_ids") && body["room_ids"].t() == crow::json::type::List) {
            for (size_t i = 0; i < body["room_ids"].size(); ++i) {
                if (body["room_ids"][i].t() != crow::json::type::String) {
                    continue;
                }
                const std::string id(body["room_ids"][i].s());
                if (db.findRoom(id)) {
                    alive.emplace_back(id);
                } else {
                    gone.emplace_back(id);
                }
            }
        }
        out["alive"] = std::move(alive);
        out["gone"]  = std::move(gone);
        crow::response resp{out};
        resp.add_header("Content-Type", "application/json");
        return resp;
    });

    // --- DELETE /api/rooms/<id> — any holder of a valid token may delete. ---
    CROW_ROUTE(app_, "/api/rooms/<string>").methods(crow::HTTPMethod::DELETE)
    ([this](const crow::request& req, const std::string& id) {
        auto& db = db_;
        auto& room = rooms_;
        const auto body = crow::json::load(req.body);
        if (!body) {
            return jsonError(400, "bad_json");
        }
        const auto token = js_str(body, "token");
        if (token.empty()) {
            return jsonError(401, "missing_token");
        }
        const auto owner = db.roomForToken(token);
        if (!owner || *owner != id) {
            return jsonError(401, "invalid_token");
        }

        // Build "deleted" frame for live peers, then remove DB rows.
        crow::json::wvalue frame;
        frame["type"] = "deleted";
        std::string frameStr = frame.dump();

        room.closeRoom(id, frameStr);
        db.deleteRoom(id);

        crow::json::wvalue out;
        out["ok"] = true;
        crow::response resp{out};
        resp.add_header("Content-Type", "application/json");
        return resp;
    });

    // --- GET /api/info — public configuration the home page renders. ---
    CROW_ROUTE(app_, "/api/info")
    ([](const crow::request&) {
        const auto& cfg = Config::instance();
        crow::json::wvalue v;
        v["app_name"]                 = cfg.appName;
        v["default_max_participants"] = cfg.defaultMaxParticipants;
        v["idle_ttl_seconds"]         = cfg.idleTtlSeconds;
        v["message_cache_size"]       = cfg.messageCacheSize;
        crow::response r{v};
        r.add_header("Content-Type", "application/json");
        return r;
    });

    // --- GET /api/version — kept for ops; the frontend uses its bundled version. ---
    CROW_ROUTE(app_, "/api/version")
    ([](const crow::request&) {
        crow::json::wvalue v;
        v["version"]   = HASHHUSH_VERSION;
        v["git_short"] = HASHHUSH_GIT_SHORT;
        crow::response r{v};
        r.add_header("Content-Type", "application/json");
        return r;
    });

    // --- WebSocket: /ws?room=<id>[&token=<t>] ---
    // Authorization: a returning client passes ?token=, and the upgrade is
    // refused if the token doesn't belong to the room. A first-time joiner
    // connects without a token and is gated by the in-protocol challenge —
    // the upgrade succeeds, but any auth failure inside the protocol closes
    // the connection (see onmessage below).
    CROW_WEBSOCKET_ROUTE(app_, "/ws")
        .onaccept([](const crow::request& req, void** userdata) {
            // Accept the upgrade for any non-empty room id, even if the
            // room does not exist. All "room missing / wrong key / wrong
            // token / pool empty" outcomes are then funnelled through the
            // same in-protocol `invalid_key` close, so an attacker probing
            // random room ids cannot enumerate which ones are real by
            // observing the HTTP-upgrade response.
            const std::string roomId = req.url_params.get("room")
                ? std::string(req.url_params.get("room"))
                : "";
            if (roomId.empty()) {
                return false;
            }
            *userdata = new std::string(roomId);
            return true;
        })
        .onopen([this](crow::websocket::connection& conn) {
            auto* roomIdPtr = reinterpret_cast<std::string*>(conn.userdata());
            if (!roomIdPtr) {
                conn.close("no room");
                return;
            }
            rooms_.onWsAccept(conn, *roomIdPtr);
        })
        .onclose([this](crow::websocket::connection& conn, const std::string&, uint16_t) {
            rooms_.onWsClose(conn);
            // Free the userdata once. If Crow ever fires onclose twice for the
            // same connection (we observed this on the path "server-initiated
            // close → peer TCP RST"), a naive delete would double-free. Null
            // the slot so the second pass is a no-op.
            auto* roomIdPtr = reinterpret_cast<std::string*>(conn.userdata());
            if (roomIdPtr) {
                conn.userdata(nullptr);
                delete roomIdPtr;
            }
        })
        .onerror([this](crow::websocket::connection& conn, const std::string&) {
            rooms_.onWsClose(conn);
        })
        .onmessage([this](crow::websocket::connection& conn, const std::string& data, bool /*is_binary*/) {
            auto& db = db_;
            auto& room = rooms_;
            const auto& cfg = Config::instance();
            if (static_cast<int>(data.size()) > cfg.wsMaxPayloadBytes) {
                conn.close("payload too large");
                return;
            }
            auto* peer = room.findByConn(conn);
            if (!peer) {
                conn.close("no peer");
                return;
            }

            const auto j = crow::json::load(data);
            if (!j || !j.has("type")) {
                conn.send_text(R"({"type":"error","code":"bad_frame"})");
                return;
            }
            std::string type(j["type"].s());

            // Helper: send canonical error frame.
            auto sendErr = [&](const char* code) {
                crow::json::wvalue f;
                f["type"] = "error";
                f["code"] = code;
                conn.send_text(f.dump());
            };
            // Helper: send error then close — used for any auth failure to
            // prevent unauthorised peers from holding a connection or
            // retrying against the same socket.
            auto sendErrAndClose = [&](const char* code) {
                sendErr(code);
                conn.close(code);
            };

            // Helper: finalize join — issue/return token, send peer_join, history.
            auto finalizeJoin = [&](const std::string& token, bool isFresh) {
                // Stable peer id derived from the token. A returning client
                // (token survives reload in localStorage) gets the same id,
                // so cached history rows can be flagged as "fromSelf" purely
                // by id-match without any guessing.
                const std::string newPeerId = util::peerIdFromToken(token);
                room.markPeerJoined(conn, newPeerId);

                const auto roomRow = db.findRoom(peer->roomId);
                const auto history = room.recentMessages(peer->roomId);

                crow::json::wvalue joined;
                joined["type"]    = "joined";
                joined["peer_id"] = newPeerId;
                if (roomRow) {
                    joined["room_name"] = roomRow->name;
                }
                if (isFresh) {
                    joined["token"] = token;
                }
                std::vector<crow::json::wvalue> hist;
                hist.reserve(history.size());
                for (const auto& m : history) {
                    crow::json::wvalue row;
                    row["payload"] = m.payloadB64;
                    row["ts"]      = m.createdAt;
                    row["peer_id"] = m.senderPeerId;
                    hist.push_back(std::move(row));
                }
                joined["history"] = std::move(hist);

                // Member list snapshot.
                auto members = room.joinedPeerIds(peer->roomId);
                std::vector<crow::json::wvalue> mlist;
                mlist.reserve(members.size());
                for (const auto& p : members) {
                    mlist.emplace_back(p);
                }
                joined["members"] = std::move(mlist);

                conn.send_text(joined.dump());

                // Notify others.
                crow::json::wvalue pj;
                pj["type"]    = "peer_join";
                pj["peer_id"] = peer->peerId;
                room.broadcast(peer->roomId, pj.dump(), &conn);

                db.touchRoom(peer->roomId);
            };

            if (!peer->joined) {
                if (type == "hello") {
                    // Existing token path. Any failure (token mismatch, room
                    // gone) collapses into the generic invalid_key response
                    // so the result is observationally identical to a wrong
                    // key in the challenge flow.
                    const auto token = js_str(j, "token");
                    if (!token.empty()) {
                        const auto owner = db.roomForToken(token);
                        if (owner && *owner == peer->roomId) {
                            finalizeJoin(token, /*isFresh=*/false);
                            return;
                        }
                        sendErrAndClose("invalid_key");
                        return;
                    }
                    // No token → challenge flow. We send a challenge frame
                    // unconditionally — a real one when the room exists and
                    // can admit a peer, a random one otherwise. The joiner
                    // can solve only the real case; everything else falls
                    // through to invalid_key on response.
                    const auto roomRow = db.findRoom(peer->roomId);
                    const bool roomFull = roomRow
                        && room.liveCount(peer->roomId) >= roomRow->maxParticipants;
                    std::optional<ChallengePair> pair;
                    if (roomRow && !roomFull) {
                        pair = db.consumeChallenge(peer->roomId);
                    }
                    if (!pair) {
                        // Decoy challenge with random bytes. Designed never
                        // to round-trip back to the stored expected value,
                        // and timing-equivalent to consuming a real one so
                        // the response is indistinguishable from the real
                        // path to an enumeration-probing attacker.
                        ChallengePair fake;
                        fake.plaintextB64  = util::base64Encode(util::randomBytesVec(32));
                        fake.ciphertextB64 = util::base64Encode(util::randomBytesVec(48));
                        fake.nonceB64      = util::base64Encode(util::randomBytesVec(24));
                        pair = std::move(fake);
                    }
                    peer->pendingToken      = util::randomTokenHex();
                    peer->expectedPlaintext = pair->plaintextB64;
                    crow::json::wvalue ch;
                    ch["type"]       = "challenge";
                    ch["ciphertext"] = pair->ciphertextB64;
                    ch["nonce"]      = pair->nonceB64;
                    conn.send_text(ch.dump());
                    return;
                }
                if (type == "challenge_response") {
                    const auto answer = js_str(j, "plaintext");
                    if (peer->pendingToken.empty() || peer->expectedPlaintext.empty()) {
                        sendErrAndClose("no_challenge");
                        return;
                    }
                    const std::string token    = std::move(peer->pendingToken);
                    const std::string expected = std::move(peer->expectedPlaintext);
                    peer->pendingToken.clear();
                    peer->expectedPlaintext.clear();
                    if (answer.empty() || answer != expected) {
                        sendErrAndClose("invalid_key");
                        return;
                    }
                    if (!db.insertAccessToken(token, peer->roomId)) {
                        sendErrAndClose("internal");
                        return;
                    }
                    finalizeJoin(token, /*isFresh=*/true);
                    return;
                }
                sendErrAndClose("not_joined");
                return;
            }

            // Joined peer: relay messages.
            //
            // Two delivery modes for encrypted frames:
            //   * "msg"      — chat content; cached for replay to new joiners.
            //   * "presence" — ephemeral metadata (nickname announce, etc.);
            //                  relayed but NEVER cached, so it cannot evict
            //                  real messages from the ring buffer.
            // The server cannot read either; it only branches on the
            // outer-envelope type.
            if (type == "msg" || type == "presence") {
                const auto payload = js_str(j, "payload");
                if (payload.empty()) {
                    sendErr("bad_payload");
                    return;
                }
                if (static_cast<int>(payload.size()) > cfg.wsMaxPayloadBytes) {
                    sendErr("payload_too_large");
                    return;
                }
                if (type == "msg") {
                    room.appendCachedMessage(peer->roomId, peer->peerId, payload, cfg.messageCacheSize);
                    db.touchRoom(peer->roomId);
                }

                crow::json::wvalue out;
                out["type"]    = type;
                out["peer_id"] = peer->peerId;
                out["payload"] = payload;
                out["ts"]      = util::nowSeconds();
                // Presence is broadcast to others only; msg goes to all
                // (sender included) so the sender's UI gets a server-stamped
                // timestamp and stable peer_id.
                room.broadcast(peer->roomId, out.dump(),
                               type == "presence" ? &conn : nullptr);
                return;
            }
            if (type == "ping") {
                conn.send_text(R"({"type":"pong"})");
                return;
            }

            sendErr("unknown_type");
        });
}
