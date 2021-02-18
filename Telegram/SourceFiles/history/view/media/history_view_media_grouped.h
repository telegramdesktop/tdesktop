/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/media/history_view_media.h"
#include "data/data_document.h"
#include "data/data_photo.h"

namespace Data {
class Media;
} // namespace Data

namespace HistoryView {

class GroupedMedia : public Media {
public:
	static constexpr auto kMaxSize = 10;

	GroupedMedia(
		not_null<Element*> parent,
		const std::vector<std::unique_ptr<Data::Media>> &medias);
	GroupedMedia(
		not_null<Element*> parent,
		const std::vector<not_null<HistoryItem*>> &items);
	~GroupedMedia();

	void refreshParentId(not_null<HistoryItem*> realParent) override;

	void drawHighlight(Painter &p, int top) const override;
	void draw(
		Painter &p,
		const QRect &clip,
		TextSelection selection,
		crl::time ms) const override;
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
	uint16 fullSelectionLength() const override;
	bool hasTextForCopy() const override;

	PhotoData *getPhoto() const override;
	DocumentData *getDocument() const override;

	TextForMimeData selectedText(TextSelection selection) const override;

	std::vector<BubbleSelectionInterval> getBubbleSelectionIntervals(
		TextSelection selection) const override;

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
		return (_mode == Mode::Grid)
			&& isRoundedInBubbleBottom()
			&& _caption.isEmpty();
	}
	void updateNeedBubbleState() override;
	bool needsBubble() const override;
	bool customInfoLayout() const override {
		return _caption.isEmpty() && (_mode != Mode::Column);
	}
	bool allowsFastShare() const override {
		return true;
	}
	bool customHighlight() const override {
		return true;
	}
	bool hideForwardedFrom() const override;

	void stopAnimation() override;
	void checkAnimation() override;
	bool hasHeavyPart() const override;
	void unloadHeavyPart() override;

	void parentTextUpdated() override;

private:
	enum class Mode : char {
		Grid,
		Column,
	};
	struct Part {
		Part(
			not_null<Element*> parent,
			not_null<Data::Media*> media);

		not_null<HistoryItem*> item;
		std::unique_ptr<Media> content;

		RectParts sides = RectPart::None;
		QRect initialGeometry;
		QRect geometry;
		mutable uint64 cacheKey = 0;
		mutable QPixmap cache;

	};

	[[nodiscard]] static Mode DetectMode(not_null<Data::Media*> media);

	template <typename DataMediaRange>
	bool applyGroup(const DataMediaRange &medias);

	template <typename DataMediaRange>
	bool validateGroupParts(const DataMediaRange &medias) const;

	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;

	bool needInfoDisplay() const;
	bool computeNeedBubble() const;
	not_null<Media*> main() const;
	TextState getPartState(
		QPoint point,
		StateRequest request) const;

	[[nodiscard]] RectParts cornersFromSides(RectParts sides) const;
	[[nodiscard]] QMargins groupedPadding() const;

	Ui::Text::String _caption;
	std::vector<Part> _parts;
	Mode _mode = Mode::Grid;
	bool _needBubble = false;

};

} // namespace HistoryView
