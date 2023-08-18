/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_bot_app.h"

BotAppData::BotAppData(not_null<Data::Session*> owner, const BotAppId &id)
: owner(owner)
, id(id) {
}
