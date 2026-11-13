/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_bot_info_localization.h"

class UserData;

namespace Api {

[[nodiscard]] Data::BotInfoTransport MakeBotInfoTransport(
	not_null<UserData*> bot);

class BotInfoLocalizations final {
public:
	[[nodiscard]] not_null<Data::BotInfoLocalization*> get(
		not_null<UserData*> bot);

private:
	base::flat_map<
		not_null<UserData*>,
		std::unique_ptr<Data::BotInfoLocalization>> _localizations;

};

} // namespace Api
