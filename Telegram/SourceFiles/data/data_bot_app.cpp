/*
This file is part of exteraGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/xmdnx/exteraGramDesktop/blob/dev/LEGAL
*/
#include "data/data_bot_app.h"

BotAppData::BotAppData(not_null<Data::Session*> owner, const BotAppId &id)
: owner(owner)
, id(id) {
}
