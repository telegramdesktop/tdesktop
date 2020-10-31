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

namespace Data {
class DocumentMedia;
} // namespace Data

namespace Ui {
namespace Text {
class String;
} // namespace Text
} // namespace Ui

namespace HistoryView {

class Document final
	: public File
	, public RuntimeComposer<Document> {
public:
	Document(
		not_null<Element*> parent,
		not_null<HistoryItem*> realParent,
		not_null<DocumentData*> document);
	~Document();

	void draw(Painter &p, const QRect &r, TextSelection selection, crl::time ms) const override;
	TextState textState(QPoint point, StateRequest request) const override;
	void updatePressed(QPoint point) override;

	[[nodiscard]] TextSelection adjustSelection(
		TextSelection selection,
		TextSelectType type) const override;
	uint16 fullSelectionLength() const override;
	bool hasTextForCopy() const override;

	TextForMimeData selectedText(TextSelection selection) const override;

	bool uploading() const override;

	DocumentData *getDocument() const override {
		return _data;
	}

	TextWithEntities getCaption() const override;
	bool needsBubble() const override {
		return true;
	}
	bool customInfoLayout() const override {
		return false;
	}
	QMargins bubbleMargins() const override;
	bool hideForwardedFrom() const override;

	QSize sizeForGroupingOptimal(int maxWidth) const override;
	QSize sizeForGrouping(int width) const override;
	void drawGrouped(
		Painter &p,
		const QRect &clip,
		TextSelection selection,
		crl::time ms,
		const QRect &geometry,
		RectParts sides,
		RectParts corners,
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
	struct StateFromPlayback {
		int statusSize = 0;
		bool showPause = false;
		int realDuration = 0;
	};
	enum class LayoutMode {
		Full,
		Grouped,
	};

	void draw(
		Painter &p,
		int width,
		TextSelection selection,
		crl::time ms,
		LayoutMode mode) const;
	[[nodiscard]] TextState textState(
		QPoint point,
		QSize layout,
		StateRequest request,
		LayoutMode mode) const;
	void ensureDataMediaCreated() const;

	[[nodiscard]] Ui::Text::String createCaption();

	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;

	void createComponents(bool caption);
	void fillNamedFromData(HistoryDocumentNamed *named);

	void setStatusSize(int newSize, qint64 realDuration = 0) const;
	bool updateStatusText() const; // returns showPause

	[[nodiscard]] bool downloadInCorner() const;
	void drawCornerDownload(Painter &p, bool selected, LayoutMode mode) const;
	[[nodiscard]] TextState cornerDownloadTextState(
		QPoint point,
		StateRequest request,
		LayoutMode mode) const;

	not_null<DocumentData*> _data;
	mutable std::shared_ptr<Data::DocumentMedia> _dataMedia;

};

} // namespace HistoryView
