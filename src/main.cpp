// Copyright (C) 2026 Roman Lyubimov
// SPDX-License-Identifier: GPL-3.0-or-later

#include "config.h"
#include "crowlib/crow/json.h"
#include "db.h"
#include "log.h"
#include "room.h"
#include "util.h"
#include "version.h"
#include "webapi.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

namespace {

constexpr const char* kDefaultConfigPath = "/etc/hashhush/config.ini";

constexpr const char* kDefaultConfigContent = R"INI([server]
log_level = info
bind_address = 0.0.0.0
bind_port = 8080
app_name = HashHush

[storage]
db_path = /var/lib/hashhush/hashhush.sqlite

[room]
default_max_participants = 10
max_participants_cap = 50
idle_ttl_seconds = 86400
total_rooms_cap = 0
message_cache_size = 5
ws_max_payload_bytes = 8192
challenge_max_blob_bytes = 256
)INI";

void printHelp(const char* prog) {
    std::cout
        << "hashhush " << HASHHUSH_VERSION << " (" << HASHHUSH_GIT_SHORT << ")\n"
        << "Ephemeral end-to-end encrypted group chat server.\n\n"
        << "Usage:\n"
        << "  " << prog << " [options]\n\n"
        << "Options:\n"
        << "  -c, --config <path>            Path to config file (default: " << kDefaultConfigPath << ")\n"
        << "  -g, --generate-config <path>   Write a default config to <path> and exit\n"
        << "  -V, --version                  Print version and exit\n"
        << "  -h, --help                     Show this message\n";
}

plog::Severity parseLogLevel(const std::string& s) {
    std::string l = s;
    for (auto& c : l) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (l == "verbose") {
        return plog::verbose;
    }
    if (l == "debug") {
        return plog::debug;
    }
    if (l == "info") {
        return plog::info;
    }
    if (l == "warning") {
        return plog::warning;
    }
    if (l == "error") {
        return plog::error;
    }
    if (l == "fatal") {
        return plog::fatal;
    }
    if (l == "none") {
        return plog::none;
    }
    std::cerr << "Unknown log level '" << s << "', falling back to 'info'\n";
    return plog::info;
}

bool generateConfig(const std::string& path) {
    std::ofstream f(path);
    if (!f) {
        std::cerr << "Cannot write " << path << "\n";
        return false;
    }
    f << kDefaultConfigContent;
    return static_cast<bool>(f);
}

std::atomic<WebAPI*> g_api{nullptr};

extern "C" void onSignal(int) {
    if (auto* api = g_api.load()) {
        api->stop();
    }
}

void installSignalHandlers() {
    std::signal(SIGINT,  onSignal);
    std::signal(SIGTERM, onSignal);
}

}  // namespace

int main(int argc, char** argv) {
    std::string configPath = kDefaultConfigPath;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-h" || a == "--help") {
            printHelp(argv[0]);
            return 0;
        }
        if (a == "-V" || a == "--version") {
            std::cout << "hashhush " << HASHHUSH_VERSION << " (" << HASHHUSH_GIT_SHORT << ")\n";
            return 0;
        }
        if (a == "-g" || a == "--generate-config") {
            if (i + 1 >= argc) {
                std::cerr << "Missing path\n";
                return 2;
            }
            return generateConfig(argv[++i]) ? 0 : 1;
        }
        if (a == "-c" || a == "--config") {
            if (i + 1 >= argc) {
                std::cerr << "Missing path\n";
                return 2;
            }
            configPath = argv[++i];
            continue;
        }
        std::cerr << "Unknown argument: " << a << "\n";
        printHelp(argv[0]);
        return 2;
    }

    auto& cfg = Config::instance();
    if (!cfg.loadFromFile(configPath)) {
        std::cerr << "Run `" << argv[0] << " --generate-config <path>` to create a default config.\n";
        return 1;
    }

    static plog::ColorConsoleAppender<plog::TxtFormatter> consoleAppender;
    plog::init(parseLogLevel(cfg.logLevel), &consoleAppender);

    PLOGI << "hashhush " << HASHHUSH_VERSION << " (" << HASHHUSH_GIT_SHORT << ")";
    PLOGI << "binding " << cfg.bindAddress << ":" << cfg.bindPort
          << ", db=" << cfg.dbPath;

    std::unique_ptr<Database> db;
    try {
        db = Database::open(cfg.dbPath);
    } catch (const std::exception& e) {
        std::cerr << "Database error: " << e.what() << "\n";
        return 1;
    }

    RoomManager rooms(*db);
    WebAPI api(*db, rooms);

    g_api.store(&api);
    installSignalHandlers();

    // Background purge thread: enforces idle TTL on rooms.
    std::atomic<bool> stopPurger{false};
    std::thread purger([&] {
        while (!stopPurger.load()) {
            for (int i = 0; i < 60 && !stopPurger.load(); ++i) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            if (stopPurger.load()) {
                break;
            }
            const auto cutoff = util::nowSeconds() - cfg.idleTtlSeconds;
            const auto stale = db->roomsIdleSince(cutoff);
            for (const auto& id : stale) {
                crow::json::wvalue f;
                f["type"] = "deleted";
                rooms.closeRoom(id, f.dump());
                db->deleteRoom(id);
                PLOGI << "purged idle room " << id;
            }
        }
    });

    api.run();   // blocks until stop()

    stopPurger.store(true);
    if (purger.joinable()) {
        purger.join();
    }

    g_api.store(nullptr);
    PLOGI << "shutdown complete";
    return 0;
}
