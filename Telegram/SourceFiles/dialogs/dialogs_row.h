/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/text/text.h"
#include "ui/effects/animations.h"
#include "ui/unread_badge.h"
#include "ui/userpic_view.h"
#include "dialogs/dialogs_key.h"
#include "dialogs/ui/dialogs_message_view.h"

class History;
class HistoryItem;

namespace style {
struct DialogRow;
} // namespace style

namespace Ui {
class RippleAnimation;
} // namespace Ui

namespace Dialogs::Ui {
using namespace ::Ui;
class RowPainter;
class VideoUserpic;
struct PaintContext;
struct TopicJumpCache;
} // namespace Dialogs::Ui

namespace Dialogs {

class Entry;
enum class SortMode;

[[nodiscard]] QRect CornerBadgeTTLRect(int photoSize);
[[nodiscard]] QImage BlurredDarkenedPart(QImage image, QRect part);

class BasicRow {
public:
	BasicRow();
	virtual ~BasicRow();

	virtual void paintUserpic(
		Painter &p,
		not_null<Entry*> entry,
		PeerData *peer,
		Ui::VideoUserpic *videoUserpic,
		const Ui::PaintContext &context) const;

	void addRipple(QPoint origin, QSize size, Fn<void()> updateCallback);
	virtual void stopLastRipple();
	virtual void clearRipple();
	void addRippleWithMask(
		QPoint origin,
		QImage mask,
		Fn<void()> updateCallback);

	void paintRipple(
		QPainter &p,
		int x,
		int y,
		int outerWidth,
		const QColor *colorOverride = nullptr) const;

	[[nodiscard]] Ui::PeerUserpicView &userpicView() const {
		return _userpic;
	}

private:
	mutable Ui::PeerUserpicView _userpic;
	mutable std::unique_ptr<Ui::RippleAnimation> _ripple;

};

class List;
class Row final : public BasicRow {
public:
	explicit Row(std::nullptr_t) {
	}
	Row(Key key, int index, int top);
	~Row();

	[[nodiscard]] int top() const {
		return _top;
	}
	[[nodiscard]] int height() const {
		Expects(_height != 0);

		return _height;
	}
	void recountHeight(float64 narrowRatio);

	void updateCornerBadgeShown(
		not_null<PeerData*> peer,
		Fn<void()> updateCallback = nullptr) const;
	void paintUserpic(
		Painter &p,
		not_null<Entry*> entry,
		PeerData *peer,
		Ui::VideoUserpic *videoUserpic,
		const Ui::PaintContext &context) const final override;

	[[nodiscard]] bool lookupIsInTopicJump(int x, int y) const;
	void stopLastRipple() override;
	void clearRipple() override;
	void addTopicJumpRipple(
		QPoint origin,
		not_null<Ui::TopicJumpCache*> topicJumpCache,
		Fn<void()> updateCallback);
	void clearTopicJumpRipple();
	[[nodiscard]] bool topicJumpRipple() const;

	[[nodiscard]] Key key() const {
		return _id;
	}
	[[nodiscard]] History *history() const {
		return _id.history();
	}
	[[nodiscard]] Data::Folder *folder() const {
		return _id.folder();
	}
	[[nodiscard]] Data::ForumTopic *topic() const {
		return _id.topic();
	}
	[[nodiscard]] Data::Thread *thread() const {
		return _id.thread();
	}
	[[nodiscard]] Data::SavedSublist *sublist() const {
		return _id.sublist();
	}
	[[nodiscard]] not_null<Entry*> entry() const {
		return _id.entry();
	}
	[[nodiscard]] int index() const {
		return _index;
	}
	[[nodiscard]] uint64 sortKey(FilterId filterId) const;

	// for any attached data, for example View in contacts list
	void *attached = nullptr;

private:
	friend class List;

	class CornerLayersManager {
	public:
		using Layer = int;
		CornerLayersManager();

		[[nodiscard]] bool isSameLayer(Layer layer) const;
		[[nodiscard]] bool isDisplayedNone() const;
		[[nodiscard]] float64 progressForLayer(Layer layer) const;
		[[nodiscard]] float64 progress() const;
		[[nodiscard]] bool isFinished() const;
		void setLayer(Layer layer, Fn<void()> updateCallback);
		void markFrameShown();

	private:
		bool _lastFrameShown = false;
		Layer _prevLayer = 0;
		Layer _nextLayer = 0;
		Ui::Animations::Simple _animation;

	};

	struct CornerBadgeUserpic {
		InMemoryKey key;
		CornerLayersManager layersManager;
		QImage frame;
		QImage cacheTTL;
		int frameIndex = -1;
		uint32 paletteVersion : 17 = 0;
		uint32 storiesCount : 7 = 0;
		uint32 storiesUnreadCount : 7 = 0;
		uint32 active : 1 = 0;
	};

	void setCornerBadgeShown(
		CornerLayersManager::Layer nextLayer,
		Fn<void()> updateCallback) const;
	void ensureCornerBadgeUserpic() const;
	static void PaintCornerBadgeFrame(
		not_null<CornerBadgeUserpic*> data,
		int framePadding,
		not_null<Entry*> entry,
		PeerData *peer,
		Ui::VideoUserpic *videoUserpic,
		Ui::PeerUserpicView &view,
		const Ui::PaintContext &context,
		bool subscribed);

	Key _id;
	mutable std::unique_ptr<CornerBadgeUserpic> _cornerBadgeUserpic;
	int _top = 0;
	int _height = 0;
	uint32 _index : 30 = 0;
	uint32 _cornerBadgeShown : 1 = 0;
	uint32 _topicJumpRipple : 1 = 0;

};

class FakeRow final : public BasicRow, public base::has_weak_ptr {
public:
	FakeRow(
		Key searchInChat,
		not_null<HistoryItem*> item,
		Fn<void()> repaint);

	[[nodiscard]] Key searchInChat() const {
		return _searchInChat;
	}
	[[nodiscard]] Data::ForumTopic *topic() const {
		return _topic;
	}
	[[nodiscard]] not_null<HistoryItem*> item() const {
		return _item;
	}
	[[nodiscard]] Ui::MessageView &itemView() const {
		return _itemView;
	}
	[[nodiscard]] Fn<void()> repaint() const {
		return _repaint;
	}
	[[nodiscard]] Ui::PeerBadge &badge() const {
		return _badge;
	}
	[[nodiscard]] const Ui::Text::String &name() const;

	void invalidateTopic();

private:
	friend class Ui::RowPainter;

	const Key _searchInChat;
	const not_null<HistoryItem*> _item;
	Data::ForumTopic *_topic = nullptr;
	const Fn<void()> _repaint;
	mutable Ui::MessageView _itemView;
	mutable Ui::PeerBadge _badge;
	mutable Ui::Text::String _name;

};

} // namespace Dialogs
