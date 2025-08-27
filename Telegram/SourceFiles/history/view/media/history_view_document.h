/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/media/history_view_file.h"
#include "base/runtime_composer.h"

struct HistoryDocumentNamed;
struct HistoryDocumentThumbed;

namespace Data {
class DocumentMedia;
} // namespace Data

namespace Ui::Text {
class String;
} // namespace Ui::Text

namespace HistoryView {

using TtlPaintCallback = Fn<void(QPainter&, QRect, QColor)>;

class Document final
	: public File
	, public RuntimeComposer<Document> {
public:
	Document(
		not_null<Element*> parent,
		not_null<HistoryItem*> realParent,
		not_null<DocumentData*> document);
	~Document();

	bool hideMessageText() const override {
		return false;
	}

	void draw(Painter &p, const PaintContext &context) const override;
	TextState textState(QPoint point, StateRequest request) const override;
	void updatePressed(QPoint point) override;

	[[nodiscard]] TextSelection adjustSelection(
		TextSelection selection,
		TextSelectType type) const override;
	uint16 fullSelectionLength() const override;
	bool hasTextForCopy() const override;

	TextForMimeData selectedText(TextSelection selection) const override;
	SelectedQuote selectedQuote(TextSelection selection) const override;
	TextSelection selectionFromQuote(
		const SelectedQuote &quote) const override;

	bool uploading() const override;

	DocumentData *getDocument() const override {
		return _data;
	}

	void hideSpoilers() override;
	bool needsBubble() const override {
		return true;
	}
	bool customInfoLayout() const override {
		return false;
	}
	QMargins bubbleMargins() const override;

	QSize sizeForGroupingOptimal(int maxWidth, bool last) const override;
	QSize sizeForGrouping(int width) const override;
	void drawGrouped(
		Painter &p,
		const PaintContext &context,
		const QRect &geometry,
		RectParts sides,
		Ui::BubbleRounding rounding,
		float64 highlightOpacity,
		not_null<uint64*> cacheKey,
		not_null<QPixmap*> cache) const override;
	TextState getStateGrouped(
		const QRect &geometry,
		RectParts sides,
		QPoint point,
		StateRequest request) const override;

	bool voiceProgressAnimationCallback(crl::time now);

	void clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) override;

	void refreshParentId(not_null<HistoryItem*> realParent) override;
	void parentTextUpdated() override;

	bool hasHeavyPart() const override;
	void unloadHeavyPart() override;

protected:
	float64 dataProgress() const override;
	bool dataFinished() const override;
	bool dataLoaded() const override;

private:
	enum class LayoutMode {
		Full,
		Grouped,
	};

	void draw(
		Painter &p,
		const PaintContext &context,
		int width,
		LayoutMode mode,
		Ui::BubbleRounding outsideRounding) const;
	[[nodiscard]] TextState textState(
		QPoint point,
		QSize layout,
		StateRequest request,
		LayoutMode mode) const;
	void ensureDataMediaCreated() const;

	[[nodiscard]] Ui::Text::String createCaption() const;

	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;

	void refreshCaption(bool last);
	void createComponents();
	void fillNamedFromData(not_null<HistoryDocumentNamed*> named);

	[[nodiscard]] Ui::BubbleRounding thumbRounding(
		LayoutMode mode,
		Ui::BubbleRounding outsideRounding) const;
	void validateThumbnail(
		not_null<const HistoryDocumentThumbed*> thumbed,
		int size,
		Ui::BubbleRounding rounding) const;

	void setStatusSize(int64 newSize, TimeId realDuration = 0) const;
	bool updateStatusText() const; // returns showPause
	[[nodiscard]] int thumbedLinkMaxWidth() const;

	[[nodiscard]] bool downloadInCorner() const;
	void drawCornerDownload(
		Painter &p,
		const PaintContext &context,
		LayoutMode mode) const;
	[[nodiscard]] TextState cornerDownloadTextState(
		QPoint point,
		StateRequest request,
		LayoutMode mode) const;

	not_null<DocumentData*> _data;
	mutable std::shared_ptr<Data::DocumentMedia> _dataMedia;
	mutable QImage _iconCache;
	mutable QImage _cornerDownloadCache;

	class TooltipFilename {
	public:
		void setElided(bool value);
		void setMoused(bool value);
		void setTooltipText(QString text);
		void updateTooltipForLink(ClickHandler *link);
		void updateTooltipForState(TextState &state) const;
	private:
		ClickHandler *_lastLink = nullptr;
		bool _elided = false;
		bool _moused = false;
		bool _stale = false;
		QString _tooltip;
	};

	mutable TooltipFilename _tooltipFilename;

	TtlPaintCallback _drawTtl;

	bool _transcribedRound = false;

};

bool DrawThumbnailAsSongCover(
	Painter &p,
	const style::color &colored,
	const std::shared_ptr<Data::DocumentMedia> &dataMedia,
	const QRect &rect,
	bool selected = false);

rpl::producer<> TTLVoiceStops(FullMsgId fullId);

} // namespace HistoryView
