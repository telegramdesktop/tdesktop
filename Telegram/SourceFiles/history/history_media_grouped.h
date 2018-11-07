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

namespace Data {
class Media;
} // namespace Data

class HistoryGroupedMedia : public HistoryMedia {
public:
	static constexpr auto kMaxSize = 10;

	HistoryGroupedMedia(
		not_null<Element*> parent,
		const std::vector<std::unique_ptr<Data::Media>> &medias);
	HistoryGroupedMedia(
		not_null<Element*> parent,
		const std::vector<not_null<HistoryItem*>> &items);

	HistoryMediaType type() const override {
		return MediaTypeGrouped;
	}

	void refreshParentId(not_null<HistoryItem*> realParent) override;

	void draw(
		Painter &p,
		const QRect &clip,
		TextSelection selection,
		TimeMs ms) const override;
	PointState pointState(QPoint point) const override;
	TextState textState(
		QPoint point,
		StateRequest request) const override;

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

	TextWithEntities selectedText(TextSelection selection) const override;

	void clickHandlerActiveChanged(
		const ClickHandlerPtr &p,
		bool active) override;
	void clickHandlerPressedChanged(
		const ClickHandlerPtr &p,
		bool pressed) override;

	TextWithEntities getCaption() const override;
	Storage::SharedMediaTypesMask sharedMediaTypes() const override;

	bool overrideEditedDate() const override {
		return true;
	}
	HistoryMessageEdited *displayedEditBadge() const override;

	bool skipBubbleTail() const override {
		return isBubbleBottom() && _caption.isEmpty();
	}
	void updateNeedBubbleState() override;
	bool needsBubble() const override;
	bool customInfoLayout() const override {
		return _caption.isEmpty();
	}
	bool allowsFastShare() const override {
		return true;
	}

	void parentTextUpdated() override;

private:
	struct Part {
		Part(
			not_null<HistoryView::Element*> parent,
			not_null<Data::Media*> media);

		not_null<HistoryItem*> item;
		std::unique_ptr<HistoryMedia> content;

		RectParts sides = RectPart::None;
		QRect initialGeometry;
		QRect geometry;
		mutable uint64 cacheKey = 0;
		mutable QPixmap cache;

	};

	template <typename DataMediaRange>
	bool applyGroup(const DataMediaRange &medias);

	template <typename DataMediaRange>
	bool validateGroupParts(const DataMediaRange &medias) const;

	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;

	bool needInfoDisplay() const;
	bool computeNeedBubble() const;
	not_null<HistoryMedia*> main() const;
	TextState getPartState(
		QPoint point,
		StateRequest request) const;

	Text _caption;
	std::vector<Part> _parts;
	bool _needBubble = false;

};
