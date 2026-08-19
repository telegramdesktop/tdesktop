/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/history_view_element.h"
#include "ui/effects/ministar_particles.h"
#include "lottie/lottie_icon.h"

namespace HistoryView {

class SummaryHeader final : public RuntimeComponent<SummaryHeader, Element> {
public:
	SummaryHeader();
	SummaryHeader(const SummaryHeader &other) = delete;
	SummaryHeader(SummaryHeader &&other) = delete;
	SummaryHeader &operator=(const SummaryHeader &other) = delete;
	SummaryHeader &operator=(SummaryHeader &&other);
	~SummaryHeader();

	void update(not_null<Element*> view);

	[[nodiscard]] bool isNameUpdated(not_null<const Element*> view) const;
	[[nodiscard]] int resizeToWidth(int width) const;
	[[nodiscard]] int height() const;
	[[nodiscard]] QMargins margins() const;

	void paint(
		Painter &p,
		not_null<const Element*> view,
		const Ui::ChatPaintContext &context,
		int x,
		int y,
		int w,
		bool inBubble) const;

	void createRippleAnimation(not_null<const Element*> view, QSize size);
	void saveRipplePoint(QPoint point) const;
	void addRipple();
	void stopLastRipple();

	[[nodiscard]] int maxWidth() const {
		return _maxWidth;
	}
	[[nodiscard]] ClickHandlerPtr link() const {
		return _link;
	}

	void unloadHeavyPart();

private:
	void ensureAnimation() const;
	void ensureLottie() const;

	[[nodiscard]] QRect iconRect() const;
	struct Animation {
		Ui::StarParticles particles;
		QPainterPath path;
		QSize cachedSize;
	};

	ClickHandlerPtr _link;
	mutable std::unique_ptr<Animation> _animation;
	mutable struct {
		mutable std::unique_ptr<Ui::RippleAnimation> animation;
		QPoint lastPoint;
	} _ripple;
	mutable struct {
		mutable std::unique_ptr<Ui::RippleAnimation> animation;
		QPoint lastPoint;
	} _iconRipple;
	mutable Ui::Text::String _name;
	mutable Ui::Text::String _text;
	mutable int _maxWidth = 0;
	mutable int _height = 0;
	mutable int _width = 0;
	mutable std::unique_ptr<Lottie::Icon> _lottie;
	mutable crl::time _unloadTime = 0;

};

} // namespace HistoryView
