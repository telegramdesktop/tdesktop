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
class RippleAnimation;
} // namespace Ui

namespace Dialogs::Ui {

using namespace ::Ui;

struct PaintContext;
struct TopicJumpCache;
struct TopicJumpCorners;

struct JumpToLastBg {
	not_null<const style::DialogRow*> st;
	not_null<TopicJumpCorners*> corners;
	QRect geometry;
	const style::color &bg;
	int width1 = 0;
	int width2 = 0;
};
struct JumpToLastGeometry {
	int rightCut = 0;
	QRect area1;
	QRect area2;

	friend inline bool operator==(
		const JumpToLastGeometry&,
		const JumpToLastGeometry&) = default;
};
JumpToLastGeometry FillJumpToLastBg(QPainter &p, JumpToLastBg context);

struct JumpToLastPrepared {
	not_null<const style::DialogRow*> st;
	not_null<TopicJumpCorners*> corners;
	const style::color &bg;
	const JumpToLastGeometry &prepared;
};
void FillJumpToLastPrepared(QPainter &p, JumpToLastPrepared context);

class TopicsView final {
public:
	explicit TopicsView(not_null<Data::Forum*> forum);
	~TopicsView();

	[[nodiscard]] not_null<Data::Forum*> forum() const {
		return _forum;
	}

	[[nodiscard]] bool prepared() const;
	void prepare(MsgId frontRootId, Fn<void()> customEmojiRepaint);

	[[nodiscard]] int jumpToTopicWidth() const;

	void paint(
		Painter &p,
		const QRect &geometry,
		const PaintContext &context) const;

	bool changeTopicJumpGeometry(JumpToLastGeometry geometry);
	void clearTopicJumpGeometry();
	[[nodiscard]] bool isInTopicJumpArea(int x, int y) const;
	void addTopicJumpRipple(
		QPoint origin,
		not_null<TopicJumpCache*> topicJumpCache,
		Fn<void()> updateCallback);
	void paintRipple(
		QPainter &p,
		int x,
		int y,
		int outerWidth,
		const QColor *colorOverride) const;
	void stopLastRipple();
	void clearRipple();

	[[nodiscard]] rpl::lifetime &lifetime() {
		return _lifetime;
	}

private:
	struct Title {
		Text::String title;
		MsgId topicRootId = 0;
		int version = -1;
		bool unread = false;
	};

	[[nodiscard]] QImage topicJumpRippleMask(
		not_null<TopicJumpCache*> topicJumpCache) const;

	const not_null<Data::Forum*> _forum;

	mutable std::vector<Title> _titles;
	mutable std::unique_ptr<RippleAnimation> _ripple;
	JumpToLastGeometry _lastTopicJumpGeometry;
	int _version = -1;
	bool _jumpToTopic = false;

	rpl::lifetime _lifetime;

};

} // namespace Dialogs::Ui
