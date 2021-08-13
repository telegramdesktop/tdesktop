/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/section_widget.h"

#include "mainwidget.h"
#include "ui/ui_utility.h"
#include "window/section_memento.h"
#include "window/window_slide_animation.h"
#include "window/themes/window_theme.h"
#include "window/window_session_controller.h"

#include <rpl/range.h>

namespace Window {

Main::Session &AbstractSectionWidget::session() const {
	return _controller->session();
}

SectionWidget::SectionWidget(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: AbstractSectionWidget(parent, controller) {
}

void SectionWidget::setGeometryWithTopMoved(
		const QRect &newGeometry,
		int topDelta) {
	_topDelta = topDelta;
	bool willBeResized = (size() != newGeometry.size());
	if (geometry() != newGeometry) {
		auto weak = Ui::MakeWeak(this);
		setGeometry(newGeometry);
		if (!weak) {
			return;
		}
	}
	if (!willBeResized) {
		resizeEvent(nullptr);
	}
	_topDelta = 0;
}

void SectionWidget::showAnimated(
		SlideDirection direction,
		const SectionSlideParams &params) {
	if (_showAnimation) return;

	showChildren();
	auto myContentCache = grabForShowAnimation(params);
	hideChildren();
	showAnimatedHook(params);

	_showAnimation = std::make_unique<SlideAnimation>();
	_showAnimation->setDirection(direction);
	_showAnimation->setRepaintCallback([this] { update(); });
	_showAnimation->setFinishedCallback([this] { showFinished(); });
	_showAnimation->setPixmaps(
		params.oldContentCache,
		myContentCache);
	_showAnimation->setTopBarShadow(params.withTopBarShadow);
	_showAnimation->setWithFade(params.withFade);
	_showAnimation->start();

	show();
}

std::shared_ptr<SectionMemento> SectionWidget::createMemento() {
	return nullptr;
}

void SectionWidget::showFast() {
	show();
	showFinished();
}

QPixmap SectionWidget::grabForShowAnimation(
		const SectionSlideParams &params) {
	return Ui::GrabWidget(this);
}

void SectionWidget::PaintBackground(
		not_null<Window::SessionController*> controller,
		not_null<QWidget*> widget,
		QRect clip) {
	Painter p(widget);

	const auto background = Window::Theme::Background();
	auto fill = QRect(0, 0, widget->width(), controller->content()->height());
	if (const auto color = background->colorForFill()) {
		p.fillRect(fill, *color);
		return;
	}
	auto fromy = controller->content()->backgroundFromY();
	auto cached = controller->cachedBackground(fill);
	if (!cached.pixmap.isNull()) {
		p.drawPixmap(cached.x, fromy + cached.y, cached.pixmap);
		return;
	}
	const auto gradient = background->gradientForFill();
	const auto patternOpacity = background->paper().patternOpacity();
	const auto &bg = background->pixmap();
	if (!bg.isNull() && !background->tile()) {
		auto hq = PainterHighQualityEnabler(p);
		QRect to, from;
		Window::Theme::ComputeBackgroundRects(fill, bg.size(), to, from);
		if (!gradient.isNull()) {
			p.drawImage(to, gradient);
			p.setCompositionMode(QPainter::CompositionMode_SoftLight);
			p.setOpacity(patternOpacity);
		}
		to.moveTop(to.top() + fromy);
		p.drawPixmap(to, bg, from);
		return;
	}
	if (!gradient.isNull()) {
		auto hq = PainterHighQualityEnabler(p);
		p.drawImage(fill, gradient);
		p.setCompositionMode(QPainter::CompositionMode_SoftLight);
		p.setOpacity(patternOpacity);
	}
	if (!bg.isNull()) {
		auto &tiled = background->pixmapForTiled();
		auto left = clip.left();
		auto top = clip.top();
		auto right = clip.left() + clip.width();
		auto bottom = clip.top() + clip.height();
		auto w = tiled.width() / cRetinaFactor();
		auto h = tiled.height() / cRetinaFactor();
		auto sx = qFloor(left / w);
		auto sy = qFloor((top - fromy) / h);
		auto cx = qCeil(right / w);
		auto cy = qCeil((bottom - fromy) / h);
		for (auto i = sx; i < cx; ++i) {
			for (auto j = sy; j < cy; ++j) {
				p.drawPixmap(QPointF(i * w, fromy + j * h), tiled);
			}
		}
	}
}

void SectionWidget::paintEvent(QPaintEvent *e) {
	if (_showAnimation) {
		Painter p(this);
		_showAnimation->paintContents(p, e->rect());
	}
}

void SectionWidget::showFinished() {
	_showAnimation.reset();
	if (isHidden()) return;

	showChildren();
	showFinishedHook();

	setInnerFocus();
}

rpl::producer<int> SectionWidget::desiredHeight() const {
	return rpl::single(height());
}

SectionWidget::~SectionWidget() = default;

} // namespace Window
