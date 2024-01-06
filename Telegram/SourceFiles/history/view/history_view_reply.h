/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/history_view_element.h"

namespace Data {
class Session;
} // namespace Data

namespace Ui {
class SpoilerAnimation;
struct BackgroundEmojiData;
struct BackgroundEmojiCache;
} // namespace Ui

namespace Ui::Text {
class CustomEmoji;
struct QuotePaintCache;
} // namespace Ui::Text

namespace HistoryView {

void ValidateBackgroundEmoji(
	DocumentId backgroundEmojiId,
	not_null<Ui::BackgroundEmojiData*> data,
	not_null<Ui::BackgroundEmojiCache*> cache,
	not_null<Ui::Text::QuotePaintCache*> quote,
	not_null<const Element*> view);

// For this one data->firstFrameMask or data->emoji must be already set.
void ValidateBackgroundEmoji(
	DocumentId backgroundEmojiId,
	not_null<Ui::BackgroundEmojiData*> data,
	not_null<Ui::BackgroundEmojiCache*> cache,
	not_null<Ui::Text::QuotePaintCache*> quote);
[[nodiscard]] auto CreateBackgroundEmojiInstance(
	not_null<Data::Session*> owner,
	DocumentId backgroundEmojiId,
	Fn<void()> repaint)
-> std::unique_ptr<Ui::Text::CustomEmoji>;

void FillBackgroundEmoji(
	QPainter &p,
	const QRect &rect,
	bool quote,
	const Ui::BackgroundEmojiCache &cache);

class Reply final : public RuntimeComponent<Reply, Element> {
public:
	Reply();
	Reply(const Reply &other) = delete;
	Reply(Reply &&other) = delete;
	Reply &operator=(const Reply &other) = delete;
	Reply &operator=(Reply &&other);
	~Reply();

	void update(
		not_null<Element*> view,
		not_null<HistoryMessageReply*> data);

	[[nodiscard]] bool isNameUpdated(
		not_null<const Element*> view,
		not_null<HistoryMessageReply*> data) const;
	void updateName(
		not_null<const Element*> view,
		not_null<HistoryMessageReply*> data,
		std::optional<PeerData*> resolvedSender = std::nullopt) const;
	[[nodiscard]] int resizeToWidth(int width) const;
	[[nodiscard]] int height() const;
	[[nodiscard]] QMargins margins() const;

	bool expand();

	void paint(
		Painter &p,
		not_null<const Element*> view,
		const Ui::ChatPaintContext &context,
		int x,
		int y,
		int w,
		bool inBubble) const;
	void unloadPersistentAnimation();

	void createRippleAnimation(not_null<const Element*> view, QSize size);
	void saveRipplePoint(QPoint point) const;
	void addRipple();
	void stopLastRipple();

	[[nodiscard]] int maxWidth() const {
		return _maxWidth;
	}
	[[nodiscard]] ClickHandlerPtr link() const {
		return _link;
	}

	[[nodiscard]] static TextWithEntities PeerEmoji(
		not_null<History*> history,
		PeerData *peer);
	[[nodiscard]] static TextWithEntities PeerEmoji(
		not_null<Data::Session*> owner,
		PeerData *peer);
	[[nodiscard]] static TextWithEntities ForwardEmoji(
		not_null<Data::Session*> owner);
	[[nodiscard]] static TextWithEntities ComposePreviewName(
		not_null<History*> history,
		not_null<HistoryItem*> to,
		bool quote);

private:
	[[nodiscard]] Ui::Text::GeometryDescriptor textGeometry(
		int available,
		int firstLineSkip,
		bool *outElided = nullptr) const;
	[[nodiscard]] QSize countMultilineOptimalSize(
		int firstLineSkip) const;
	void setLinkFrom(
		not_null<Element*> view,
		not_null<HistoryMessageReply*> data);

	[[nodiscard]] PeerData *sender(
		not_null<const Element*> view,
		not_null<HistoryMessageReply*> data) const;
	[[nodiscard]] QString senderName(
		not_null<const Element*> view,
		not_null<HistoryMessageReply*> data,
		bool shorten) const;
	[[nodiscard]] QString senderName(
		not_null<PeerData*> peer,
		bool shorten) const;

	ClickHandlerPtr _link;
	std::unique_ptr<Ui::SpoilerAnimation> _spoiler;
	mutable PeerData *_externalSender = nullptr;
	mutable PeerData *_colorPeer = nullptr;
	mutable struct {
		mutable std::unique_ptr<Ui::RippleAnimation> animation;
		QPoint lastPoint;
	} _ripple;
	mutable Ui::Text::String _name;
	mutable Ui::Text::String _text;
	mutable QString _stateText;
	mutable int _maxWidth = 0;
	mutable int _minHeight = 0;
	mutable int _height = 0;
	mutable int _nameVersion = 0;
	uint8 _hiddenSenderColorIndexPlusOne : 7 = 0;
	uint8 _hasQuoteIcon : 1 = 0;
	uint8 _replyToStory : 1 = 0;
	uint8 _expanded : 1 = 0;
	mutable uint8 _expandable : 1 = 0;
	mutable uint8 _minHeightExpandable : 1 = 0;
	mutable uint8 _nameTwoLines : 1 = 0;
	mutable uint8 _hasPreview : 1 = 0;
	mutable uint8 _displaying : 1 = 0;
	mutable uint8 _multiline : 1 = 0;

};

} // namespace HistoryView
