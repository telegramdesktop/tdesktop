/*
This file is part of AhiGram,
a fork of Telegram Desktop with additional features.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

// Use of the code is permitted as long as links to the original source are maintained.
// Author: https://github.com/DyingLay

AhiGram: Initialization implementation
*/

#include "main_ahigram.h"
#include "main/main_session.h"

#include <stdexcept>

namespace AhiGram {
    void Initialize(not_null<Main::Session*> session){
        LOG(("AhiGram: Initializing for session %1...")
        .arg(QString::number(session->userId().bare)));
    }
}