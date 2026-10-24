/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/media/history_view_media.h"
#include "tgw/tgw_media.h"
#include "tgw/tgw_theme.h"

namespace Tgw {
class RendererCore;
class Runtime;
} // namespace Tgw

namespace HistoryView {

class HtmlWidget final : public Media {
public:
	HtmlWidget(
		not_null<Element*> parent,
		not_null<HistoryItem*> realParent,
		Tgw::Anchors anchors,
		QByteArray html,
		Element *replacing);
	~HtmlWidget();

	void draw(Painter &p, const PaintContext &context) const override;
	TextState textState(QPoint point, StateRequest request) const override;
	void updatePressed(QPoint point) override;

	[[nodiscard]] bool toggleSelectionByHandlerClick(
		const ClickHandlerPtr &p) const override;
	[[nodiscard]] bool dragItemByHandler(
		const ClickHandlerPtr &p) const override;
	[[nodiscard]] bool keepFocusByHandler(
		const ClickHandlerPtr &p) const override;
	[[nodiscard]] Tgw::RendererCore *touchCore(QPoint point) const;
	[[nodiscard]] Tgw::RendererCore *menuPress(const ClickHandlerPtr &link);
	[[nodiscard]] QPointF cssPoint(QPoint point) const;
	[[nodiscard]] QPointF gesturePoint(QPoint point) const;

	void clickHandlerActiveChanged(
		const ClickHandlerPtr &p,
		bool active) override;
	void clickHandlerPressedChanged(
		const ClickHandlerPtr &p,
		bool pressed) override;

	bool hideMessageText() const override {
		return false;
	}
	bool allowsFastShare() const override {
		return true;
	}
	bool needsBubble() const override;
	bool customInfoLayout() const override;
	bool enforceBubbleWidth() const override;

	bool hasHeavyPart() const override;
	void unloadHeavyPart() override;

private:
	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;

	[[nodiscard]] std::unique_ptr<Tgw::Runtime> takeRuntime(
		const QByteArray &html);
	[[nodiscard]] QRect cardRect() const;
	[[nodiscard]] int cardHeight() const;
	[[nodiscard]] double maxCssHeight() const;
	[[nodiscard]] int infoHeight() const;
	[[nodiscard]] Tgw::RendererCore *interactiveCore() const;
	[[nodiscard]] Tgw::RendererCore *pointerCore() const;
	void pressCard();
	void releaseCard();
	void drawStopped(
		Painter &p,
		const PaintContext &context,
		QRect card) const;
	void runtimeHeightChanged();
	void runtimeHeavyChanged();

	const Tgw::Anchors _anchors;
	std::unique_ptr<Tgw::Runtime> _runtime;
	const ClickHandlerPtr _cardLink;
	mutable Tgw::ThemeKey _themeKey;
	QPoint _pointerPoint;
	mutable bool _heavy = false;
	bool _cardHovered = false;

};

} // namespace HistoryView
