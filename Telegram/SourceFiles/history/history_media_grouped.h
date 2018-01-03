/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/history_media.h"
#include "data/data_document.h"
#include "data/data_photo.h"

class HistoryGroupedMedia : public HistoryMedia {
public:
	HistoryGroupedMedia(
		not_null<HistoryItem*> parent,
		const std::vector<not_null<HistoryItem*>> &others);

	HistoryMediaType type() const override {
		return MediaTypeGrouped;
	}
	std::unique_ptr<HistoryMedia> clone(
		not_null<HistoryItem*> newParent,
		not_null<HistoryItem*> realParent) const override;

	void initDimensions() override;
	int resizeGetHeight(int width) override;
	void refreshParentId(not_null<HistoryItem*> realParent) override;
	void updateSentMedia(const MTPMessageMedia &media) override;
	bool needReSetInlineResultMedia(const MTPMessageMedia &media) override;

	void draw(
		Painter &p,
		const QRect &clip,
		TextSelection selection,
		TimeMs ms) const override;
	HistoryTextState getState(
		QPoint point,
		HistoryStateRequest request) const override;

	bool toggleSelectionByHandlerClick(
		const ClickHandlerPtr &p) const override;
	bool dragItemByHandler(const ClickHandlerPtr &p) const override;

	[[nodiscard]] TextSelection adjustSelection(
		TextSelection selection,
		TextSelectType type) const override;
	uint16 fullSelectionLength() const override {
		return _caption.length();
	}
	bool hasTextForCopy() const override {
		return !_caption.isEmpty();
	}

	PhotoData *getPhoto() const override;
	DocumentData *getDocument() const override;

	QString notificationText() const override;
	QString inDialogsText() const override;
	TextWithEntities selectedText(TextSelection selection) const override;

	void clickHandlerActiveChanged(
		const ClickHandlerPtr &p,
		bool active) override;
	void clickHandlerPressedChanged(
		const ClickHandlerPtr &p,
		bool pressed) override;

	void attachToParent() override;
	void detachFromParent() override;
	std::unique_ptr<HistoryMedia> takeLastFromGroup() override;
	bool applyGroup(
		const std::vector<not_null<HistoryItem*>> &others) override;

	bool hasReplyPreview() const override;
	ImagePtr replyPreview() override;
	TextWithEntities getCaption() const override;
	Storage::SharedMediaTypesMask sharedMediaTypes() const override;

	bool overrideEditedDate() const override {
		return true;
	}
	HistoryMessageEdited *displayedEditBadge() const override;

	bool canBeGrouped() const override {
		return true;
	}

	bool skipBubbleTail() const override {
		return isBubbleBottom() && _caption.isEmpty();
	}
	void updateNeedBubbleState() override;
	bool needsBubble() const override;
	bool customInfoLayout() const override {
		return _caption.isEmpty();
	}
	bool canEditCaption() const override;
	bool allowsFastShare() const override {
		return true;
	}

private:
	struct Element {
		Element(not_null<HistoryItem*> item);

		not_null<HistoryItem*> item;
		std::unique_ptr<HistoryMedia> content;

		RectParts sides = RectPart::None;
		QRect initialGeometry;
		QRect geometry;
		mutable uint64 cacheKey = 0;
		mutable QPixmap cache;

	};

	bool computeNeedBubble() const;
	not_null<HistoryMedia*> main() const;
	bool validateGroupElements(
		const std::vector<not_null<HistoryItem*>> &others) const;
	HistoryTextState getElementState(
		QPoint point,
		HistoryStateRequest request) const;

	Text _caption;
	std::vector<Element> _elements;
	bool _needBubble = false;

};
