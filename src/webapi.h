// Copyright (C) 2026 Roman Lyubimov
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "crowlib/crow/app.h"

class Database;
class RoomManager;

class WebAPI {
public:
    WebAPI(Database& db, RoomManager& rooms);

    void run();
    void stop();

private:
    void initRoutes();

    Database&    db_;
    RoomManager& rooms_;
    crow::SimpleApp app_;
    std::string  bundleEtag_;     // sha256-prefix of embedded HTML
    std::string  bundleHtml_;
    std::string  infoBody_;       // pre-rendered /api/info JSON
    std::string  infoEtag_;
};
