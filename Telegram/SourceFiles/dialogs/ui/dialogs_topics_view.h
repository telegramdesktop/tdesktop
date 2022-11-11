/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class Painter;

namespace style {
struct DialogRow;
} // namespace style

namespace Data {
class Forum;
class ForumTopic;
} // namespace Data

namespace Ui {
} // namespace Ui

namespace Dialogs::Ui {

using namespace ::Ui;

struct PaintContext;

class TopicsView final {
public:
	explicit TopicsView(not_null<Data::Forum*> forum);
	~TopicsView();

	[[nodiscard]] not_null<Data::Forum*> forum() const {
		return _forum;
	}

	void prepare(
		const QRect &geometry,
		not_null<const style::DialogRow*> st,
		Fn<void()> customEmojiRepaint);

	void paint(
		Painter &p,
		const QRect &geometry,
		const PaintContext &context) const;

	[[nodiscard]] rpl::lifetime &lifetime() {
		return _lifetime;
	}

private:
	struct Title {
		Text::String title;
		MsgId topicRootId = 0;
		bool unread = false;
	};
	const not_null<Data::Forum*> _forum;

	mutable std::vector<Title> _titles;

	rpl::lifetime _lifetime;

};

} // namespace Dialogs::Ui
