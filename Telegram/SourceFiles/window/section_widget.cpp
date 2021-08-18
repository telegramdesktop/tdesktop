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

AbstractSectionWidget::AbstractSectionWidget(
	QWidget *parent,
	not_null<SessionController*> controller,
	PaintedBackground paintedBackground)
: RpWidget(parent)
, _controller(controller) {
	if (paintedBackground == PaintedBackground::Section) {
		controller->repaintBackgroundRequests(
		) | rpl::start_with_next([=] {
			update();
		}, lifetime());
	}
}

Main::Session &AbstractSectionWidget::session() const {
	return _controller->session();
}

SectionWidget::SectionWidget(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	PaintedBackground paintedBackground)
: AbstractSectionWidget(parent, controller, paintedBackground) {
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
	const auto fullHeight = controller->content()->height();
	if (const auto color = background->colorForFill()) {
		p.fillRect(clip, *color);
		return;
	}
	const auto gradient = background->gradientForFill();
	const auto fill = QSize(widget->width(), fullHeight);
	auto fromy = controller->content()->backgroundFromY();
	auto state = controller->backgroundState(fill);
	const auto paintCache = [&](const CachedBackground &cache) {
		const auto to = QRect(
			QPoint(cache.x, fromy + cache.y),
			cache.pixmap.size() / cIntRetinaFactor());
		if (cache.area == fill) {
			p.drawPixmap(to, cache.pixmap);
		} else {
			const auto sx = fill.width() / float64(cache.area.width());
			const auto sy = fill.height() / float64(cache.area.height());
			const auto round = [](float64 value) -> int {
				return (value >= 0.)
					? int(std::ceil(value))
					: int(std::floor(value));
			};
			const auto sto = QPoint(round(to.x() * sx), round(to.y() * sy));
			p.drawPixmap(
				sto.x(),
				sto.y(),
				round((to.x() + to.width()) * sx) - sto.x(),
				round((to.y() + to.height()) * sy) - sto.y(),
				cache.pixmap);
		}
	};
	const auto hasNow = !state.now.pixmap.isNull();
	const auto goodNow = hasNow && (state.now.area == fill);
	const auto useCache = goodNow || !gradient.isNull();
	if (useCache) {
		if (state.shown < 1. && !gradient.isNull()) {
			paintCache(state.was);
			p.setOpacity(state.shown);
		}
		paintCache(state.now);
		return;
	}
	const auto &prepared = background->prepared();
	if (!prepared.isNull() && !background->tile()) {
		const auto hq = PainterHighQualityEnabler(p);
		const auto rects = Window::Theme::ComputeBackgroundRects(
			fill,
			prepared.size());
		auto to = rects.to;
		to.moveTop(to.top() + fromy);
		p.drawImage(to, prepared, rects.from);
		return;
	}
	if (!prepared.isNull()) {
		const auto &tiled = background->preparedForTiled();
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
				p.drawImage(QPointF(i * w, fromy + j * h), tiled);
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
