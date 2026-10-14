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

namespace Tgw {
class Runtime;
} // namespace Tgw

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
	bool needsBubble() const override;
	bool unwrapped() const override;
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

	[[nodiscard]] bool bareKeepsBubble() const;
	[[nodiscard]] QMargins cardMargins() const;
	[[nodiscard]] QRect cardRect() const;
	[[nodiscard]] int countHeight() const;
	[[nodiscard]] int bareInfoHeight() const;
	[[nodiscard]] bool drawsInfo() const;
	[[nodiscard]] std::optional<QRect> rightActionRect() const;
	void drawFilename(
		Painter &p,
		QRect card,
		const style::color &fg) const;
	void drawStopped(
		Painter &p,
		const PaintContext &context,
		QRect card) const;
	void ensureDataMediaCreated() const;
	void requestFallback() const;
	void runtimeHeavyChanged();

	const not_null<DocumentData*> _data;
	mutable std::shared_ptr<Data::DocumentMedia> _dataMedia;
	std::unique_ptr<Tgw::Runtime> _runtime;
	const bool _bare = false;
	const bool _hideReply = false;
	mutable bool _fallbackRequested = false;

};

} // namespace HistoryView
