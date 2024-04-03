/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class ApiWrap;

namespace Api {

struct ChatLink {
	QString link;
	QString title;
	TextWithEntities message;
	int clicks = 0;
};

struct ChatLinkUpdate {
	QString was;
	std::optional<ChatLink> now;
};

class ChatLinks final {
public:
	explicit ChatLinks(not_null<ApiWrap*> api);

	using Link = ChatLink;
	using Update = ChatLinkUpdate;

	void create(
		const QString &title,
		const TextWithEntities &message,
		Fn<void(Link)> done = nullptr);
	void edit(
		const QString &link,
		const QString &title,
		const TextWithEntities &message,
		Fn<void(Link)> done = nullptr);
	void destroy(
		const QString &link,
		Fn<void()> done = nullptr);

	void preload();
	[[nodiscard]] const std::vector<ChatLink> &list() const;
	[[nodiscard]] bool loaded() const;
	[[nodiscard]] rpl::producer<> loadedUpdates() const;
	[[nodiscard]] rpl::producer<Update> updates() const;

private:
	const not_null<ApiWrap*> _api;

	std::vector<Link> _list;
	rpl::event_stream<> _loadedUpdates;
	mtpRequestId _requestId = 0;
	bool _loaded = false;

	rpl::event_stream<Update> _updates;

};

} // namespace Api
