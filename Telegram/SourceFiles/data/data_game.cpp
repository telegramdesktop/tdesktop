/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#include "data/data_game.h"

GameData::GameData(not_null<Data::Session*> owner, const GameId &id)
: owner(owner)
, id(id) {
}
