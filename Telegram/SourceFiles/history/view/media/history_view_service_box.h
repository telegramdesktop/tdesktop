/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/media/history_view_media.h"

namespace Ui {
class RippleAnimation;
} // namespace Ui

namespace HistoryView {

class ServiceBoxContent {
public:
	virtual ~ServiceBoxContent() = default;

	[[nodiscard]] virtual int width();
	[[nodiscard]] virtual int top() = 0;
	[[nodiscard]] virtual QSize size() = 0;
	[[nodiscard]] virtual QString title() = 0;
	[[nodiscard]] virtual TextWithEntities subtitle() = 0;
	[[nodiscard]] virtual int buttonSkip() {
		return top();
	}
	[[nodiscard]] virtual rpl::producer<QString> button() = 0;
	[[nodiscard]] virtual QString cornerTagText() {
		return {};
	}
	virtual void draw(
		Painter &p,
		const PaintContext &context,
		const QRect &geometry) = 0;
	[[nodiscard]] virtual ClickHandlerPtr createViewLink() = 0;

	[[nodiscard]] virtual bool hideServiceText() = 0;

	virtual void stickerClearLoopPlayed() = 0;
	[[nodiscard]] virtual std::unique_ptr<StickerPlayer> stickerTakePlayer(
		not_null<DocumentData*> data,
		const Lottie::ColorReplacements *replacements) = 0;

	[[nodiscard]] virtual bool hasHeavyPart() = 0;
	virtual void unloadHeavyPart() = 0;
};

class ServiceBox final : public Media {
public:
	ServiceBox(
		not_null<Element*> parent,
		std::unique_ptr<ServiceBoxContent> content);
	~ServiceBox();

	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;

	void draw(Painter &p, const PaintContext &context) const override;
	TextState textState(QPoint point, StateRequest request) const override;

	[[nodiscard]] bool toggleSelectionByHandlerClick(
		const ClickHandlerPtr &p) const override;
	[[nodiscard]] bool dragItemByHandler(
		const ClickHandlerPtr &p) const override;

	void clickHandlerPressedChanged(
		const ClickHandlerPtr &handler,
		bool pressed) override;

	void stickerClearLoopPlayed() override;
	std::unique_ptr<StickerPlayer> stickerTakePlayer(
		not_null<DocumentData*> data,
		const Lottie::ColorReplacements *replacements) override;

	[[nodiscard]] bool needsBubble() const override;
	[[nodiscard]] bool customInfoLayout() const override;

	[[nodiscard]] bool hideServiceText() const override {
		return _content->hideServiceText();
	}
	void hideSpoilers() override;

	bool hasHeavyPart() const override;
	void unloadHeavyPart() override;

private:
	[[nodiscard]] QRect buttonRect() const;
	[[nodiscard]] QRect contentRect() const;

	const not_null<Element*> _parent;
	const std::unique_ptr<ServiceBoxContent> _content;
	mutable ClickHandlerPtr _contentLink;

	struct Button {
		void drawBg(QPainter &p) const;
		void toggleRipple(bool pressed);
		[[nodiscard]] bool empty() const;

		Fn<void()> repaint;

		Ui::Text::String text;
		QSize size;

		ClickHandlerPtr link;
		std::unique_ptr<Ui::RippleAnimation> ripple;

		mutable QPoint lastPoint;
	} _button;

	const int _maxWidth = 0;
	Ui::Text::String _title;
	Ui::Text::String _subtitle;
	const QSize _size;
	const QSize _innerSize;
	rpl::lifetime _lifetime;

};

} // namespace HistoryView
