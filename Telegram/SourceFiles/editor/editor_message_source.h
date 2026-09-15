/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class HistoryItem;
struct WebPageData;

namespace Main {
class Session;
} // namespace Main

namespace Editor {

struct LinkPreview {
	QString url;
	QString name;
	bool captionAbove = true;
	bool largePhoto = false;
	bool dark = false;

	friend inline bool operator==(
		const LinkPreview &,
		const LinkPreview &) = default;
};

[[nodiscard]] bool CanRenderMessage(not_null<HistoryItem*> item);
[[nodiscard]] not_null<HistoryItem*> MessageToRender(
	not_null<HistoryItem*> item);

class MessageSource final {
public:
	explicit MessageSource(not_null<HistoryItem*> item);
	MessageSource(
		not_null<Main::Session*> session,
		LinkPreview link,
		WebPageData *webpage);
	~MessageSource();

	[[nodiscard]] Main::Session &session() const;
	[[nodiscard]] HistoryItem *item() const;
	[[nodiscard]] const std::optional<LinkPreview> &link() const;
	[[nodiscard]] WebPageData *webpage() const;
	[[nodiscard]] rpl::producer<> removed() const;

private:
	void watchRemoval();

	const not_null<Main::Session*> _session;
	const std::optional<LinkPreview> _link;
	WebPageData *_webpage = nullptr;
	HistoryItem *_item = nullptr;
	bool _owned = false;
	rpl::event_stream<> _removed;
	rpl::lifetime _lifetime;

};

} // namespace Editor
