/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/media/history_view_file.h"

namespace Data {
class DocumentMedia;
} // namespace Data

namespace HistoryView {

class WidgetDocument final : public File {
public:
	WidgetDocument(
		not_null<Element*> parent,
		not_null<HistoryItem*> realParent,
		not_null<DocumentData*> document);
	~WidgetDocument();

	void draw(Painter &p, const PaintContext &context) const override;
	TextState textState(QPoint point, StateRequest request) const override;

	DocumentData *getDocument() const override {
		return _data;
	}

	bool hideMessageText() const override {
		return false;
	}
	bool needsBubble() const override {
		return true;
	}
	bool customInfoLayout() const override {
		return true;
	}
	QPoint resolveCustomInfoRightBottom() const override;

	bool hasHeavyPart() const override;
	void unloadHeavyPart() override;

protected:
	float64 dataProgress() const override;
	bool dataFinished() const override;
	bool dataLoaded() const override;

private:
	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;

	[[nodiscard]] QMargins cardMargins() const;
	[[nodiscard]] QRect cardRect() const;
	[[nodiscard]] int countHeight() const;
	[[nodiscard]] bool drawsInfo() const;
	void ensureDataMediaCreated() const;
	void requestFallback() const;

	const not_null<DocumentData*> _data;
	mutable std::shared_ptr<Data::DocumentMedia> _dataMedia;
	mutable bool _fallbackRequested = false;

};

} // namespace HistoryView
