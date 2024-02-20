/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/business/data_business_common.h"

class UserData;

namespace Data {

class Session;

struct ChatbotsSettings {
	UserData *bot = nullptr;
	BusinessRecipients recipients;
	bool repliesAllowed = false;
};

class Chatbots final {
public:
	explicit Chatbots(not_null<Session*> session);
	~Chatbots();

	[[nodiscard]] const ChatbotsSettings &current() const;
	[[nodiscard]] rpl::producer<ChatbotsSettings> changes() const;
	[[nodiscard]] rpl::producer<ChatbotsSettings> value() const;

	void save(ChatbotsSettings settings);

private:
	const not_null<Session*> _session;

	rpl::variable<ChatbotsSettings> _settings;

};

} // namespace Data
