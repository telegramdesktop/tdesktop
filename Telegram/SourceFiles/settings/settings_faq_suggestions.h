/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Main {
class Session;
} // namespace Main

namespace Settings {

struct FaqEntry {
	QString section;
	QString title;
	QString url;
};

class FaqSuggestions final {
public:
	explicit FaqSuggestions(not_null<Main::Session*> session);
	~FaqSuggestions();

	void request();
	[[nodiscard]] bool loaded() const;
	[[nodiscard]] rpl::producer<bool> loadedValue() const;
	[[nodiscard]] const std::vector<FaqEntry> &entries() const;

private:
	void parse(const MTPDwebPage &page);

	const not_null<Main::Session*> _session;

	std::vector<FaqEntry> _entries;
	rpl::variable<bool> _loaded = false;
	mtpRequestId _requestId = 0;

};

} // namespace Settings
