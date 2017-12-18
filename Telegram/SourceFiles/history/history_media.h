/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

namespace base {
template <typename Enum>
class enum_mask;
} // namespace base

namespace Storage {
enum class SharedMediaType : char;
using SharedMediaTypesMask = base::enum_mask<SharedMediaType>;
} // namespace Storage

enum class MediaInBubbleState {
	None,
	Top,
	Middle,
	Bottom,
};

class HistoryMedia : public HistoryElement {
public:
	HistoryMedia(not_null<HistoryItem*> parent) : _parent(parent) {
	}

	virtual HistoryMediaType type() const = 0;

	virtual QString notificationText() const {
		return QString();
	}

	// Returns text with link-start and link-end commands for service-color highlighting.
	// Example: "[link1-start]You:[link1-end] [link1-start]Photo,[link1-end] caption text"
	virtual QString inDialogsText() const {
		auto result = notificationText();
		return result.isEmpty() ? QString() : textcmdLink(1, TextUtilities::Clean(result));
	}
	virtual TextWithEntities selectedText(TextSelection selection) const = 0;

	bool hasPoint(QPoint point) const {
		return QRect(0, 0, _width, _height).contains(point);
	}

	virtual bool isDisplayed() const {
		return !_parent->isHiddenByGroup();
	}
	virtual void updateNeedBubbleState() {
	}
	virtual bool isAboveMessage() const {
		return false;
	}
	virtual bool hasTextForCopy() const {
		return false;
	}
	virtual bool allowsFastShare() const {
		return false;
	}
	virtual void initDimensions() = 0;
	virtual void refreshParentId(not_null<HistoryItem*> realParent) {
	}
	virtual int resizeGetHeight(int width) {
		_width = qMin(width, _maxw);
		return _height;
	}
	virtual void draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const = 0;
	virtual HistoryTextState getState(QPoint point, HistoryStateRequest request) const = 0;
	virtual void updatePressed(QPoint point) {
	}

	virtual Storage::SharedMediaTypesMask sharedMediaTypes() const;

	// if we are in selecting items mode perhaps we want to
	// toggle selection instead of activating the pressed link
	virtual bool toggleSelectionByHandlerClick(
		const ClickHandlerPtr &p) const = 0;

	// if we press and drag on this media should we drag the item
	[[nodiscard]] virtual bool dragItem() const {
		return false;
	}

	[[nodiscard]] virtual TextSelection adjustSelection(
			TextSelection selection,
			TextSelectType type) const {
		return selection;
	}
	[[nodiscard]] virtual bool consumeMessageText(
			const TextWithEntities &textWithEntities) {
		return false;
	}
	[[nodiscard]] virtual uint16 fullSelectionLength() const {
		return 0;
	}
	[[nodiscard]] TextSelection skipSelection(
			TextSelection selection) const {
		return internal::unshiftSelection(
			selection,
			fullSelectionLength());
	}
	[[nodiscard]] TextSelection unskipSelection(
			TextSelection selection) const {
		return internal::shiftSelection(
			selection,
			fullSelectionLength());
	}

	// if we press and drag this link should we drag the item
	virtual bool dragItemByHandler(const ClickHandlerPtr &p) const = 0;

	virtual void clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) {
	}
	virtual void clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) {
	}

	virtual bool uploading() const {
		return false;
	}
	virtual std::unique_ptr<HistoryMedia> clone(
		not_null<HistoryItem*> newParent,
		not_null<HistoryItem*> realParent) const = 0;

	virtual PhotoData *getPhoto() const {
		return nullptr;
	}
	virtual DocumentData *getDocument() const {
		return nullptr;
	}
	virtual Media::Clip::Reader *getClipReader() {
		return nullptr;
	}

	bool playInline(/*bool autoplay = false*/) {
		return playInline(false);
	}
	virtual bool playInline(bool autoplay) {
		return false;
	}
	virtual void stopInline() {
	}
	virtual bool isRoundVideoPlaying() const {
		return false;
	}

	virtual void attachToParent() {
	}
	virtual void detachFromParent() {
	}

	virtual bool canBeGrouped() const {
		return false;
	}
	virtual QSize sizeForGrouping() const {
		Unexpected("Grouping method call.");
	}
	virtual void drawGrouped(
			Painter &p,
			const QRect &clip,
			TextSelection selection,
			TimeMs ms,
			const QRect &geometry,
			RectParts corners,
			not_null<uint64*> cacheKey,
			not_null<QPixmap*> cache) const {
		Unexpected("Grouping method call.");
	}
	virtual HistoryTextState getStateGrouped(
			const QRect &geometry,
			QPoint point,
			HistoryStateRequest request) const {
		Unexpected("Grouping method call.");
	}
	virtual std::unique_ptr<HistoryMedia> takeLastFromGroup() {
		return nullptr;
	}
	virtual bool applyGroup(
			const std::vector<not_null<HistoryItem*>> &others) {
		return others.empty();
	}

	virtual void updateSentMedia(const MTPMessageMedia &media) {
	}

	// After sending an inline result we may want to completely recreate
	// the media (all media that was generated on client side, for example)
	virtual bool needReSetInlineResultMedia(const MTPMessageMedia &media) {
		return true;
	}

	virtual bool animating() const {
		return false;
	}

	virtual bool hasReplyPreview() const {
		return false;
	}
	virtual ImagePtr replyPreview() {
		return ImagePtr();
	}
	virtual TextWithEntities getCaption() const {
		return TextWithEntities();
	}
	virtual bool needsBubble() const = 0;
	virtual bool customInfoLayout() const = 0;
	virtual QMargins bubbleMargins() const {
		return QMargins();
	}
	virtual bool hideForwardedFrom() const {
		return false;
	}

	virtual bool overrideEditedDate() const {
		return false;
	}
	virtual HistoryMessageEdited *displayedEditBadge() const {
		Unexpected("displayedEditBadge() on non-grouped media.");
	}

	// An attach media in a web page can provide an
	// additional text to be displayed below the attach.
	// For example duration / progress for video messages.
	virtual QString additionalInfoString() const {
		return QString();
	}

	int currentWidth() const {
		return _width;
	}

	void setInBubbleState(MediaInBubbleState state) {
		_inBubbleState = state;
	}
	MediaInBubbleState inBubbleState() const {
		return _inBubbleState;
	}
	bool isBubbleTop() const {
		return (_inBubbleState == MediaInBubbleState::Top) || (_inBubbleState == MediaInBubbleState::None);
	}
	bool isBubbleBottom() const {
		return (_inBubbleState == MediaInBubbleState::Bottom) || (_inBubbleState == MediaInBubbleState::None);
	}
	virtual bool skipBubbleTail() const {
		return false;
	}

	virtual bool canEditCaption() const {
		return false;
	}

	// Sometimes click on media in message is overloaded by the messsage:
	// (for example it can open a link or a game instead of opening media)
	// But the overloading click handler should be used only when media
	// is already loaded (not a photo or gif waiting for load with auto
	// load being disabled - in such case media should handle the click).
	virtual bool isReadyForOpen() const {
		return true;
	}

protected:
	not_null<HistoryItem*> _parent;
	int _width = 0;
	MediaInBubbleState _inBubbleState = MediaInBubbleState::None;


};
