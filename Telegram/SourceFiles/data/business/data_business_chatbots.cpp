/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/business/data_business_chatbots.h"

namespace Data {

Chatbots::Chatbots(not_null<Session*> session)
: _session(session) {
}

Chatbots::~Chatbots() = default;

const ChatbotsSettings &Chatbots::current() const {
	return _settings.current();
}

rpl::producer<ChatbotsSettings> Chatbots::changes() const {
	return _settings.changes();
}

rpl::producer<ChatbotsSettings> Chatbots::value() const {
	return _settings.value();
}

void Chatbots::save(ChatbotsSettings settings, Fn<void(QString)> fail) {
	_settings = settings;
}

} // namespace Data
