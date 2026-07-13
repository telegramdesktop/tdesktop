/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/media/info_media_grid_zoom.h"

#include "info/media/info_media_list_widget.h"
#include "info/media/info_media_list_section.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "ui/ui_utility.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_overview.h"

namespace Info::Media {
namespace {

constexpr auto kStepMin = -2;
constexpr auto kStepMax = 3;
constexpr auto kPinchStep = 0.5;
constexpr auto kFadeDuration = crl::time(200);

} // namespace

ListZoom::ListZoom(not_null<ListWidget*> list)
: _list(list)
, _step(std::clamp(
	Core::App().settings().mediaGridZoomStep(),
	kStepMin,
	kStepMax)) {
}

bool ListZoom::isZoomable() const {
	switch (_list->_provider->type()) {
	case Type::Photo:
	case Type::Video:
	case Type::PhotoVideo:
	case Type::RoundFile:
		return true;
	default:
		return false;
	}
}

int ListZoom::minVideoGridSize() const {
	return 2 * st::overviewVideoStatusMargin
		+ 3 * st::overviewVideoStatusPadding.x()
		+ st::overviewVideoPlay.width()
		+ st::normalFont->width(u"00:00"_q);
}

int ListZoom::minGridSizeForStep(int step) const {
	const auto base = st::infoMediaMinGridSize
		+ step * st::infoMediaGridSizeStep;
	switch (_list->_provider->type()) {
	case Type::Video:
	case Type::PhotoVideo:
	case Type::RoundFile:
		return std::max(base, minVideoGridSize());
	default:
		return base;
	}
}

int ListZoom::minGridSize() const {
	return minGridSizeForStep(_step);
}

int ListZoom::columnsForGridSize(int gridSize) const {
	const auto outer = _list->width();
	if (outer <= 0) {
		return 1;
	}
	const auto skip = st::infoMediaSkip;
	const auto left = st::infoMediaLeft;
	return std::max(1, (outer - left * 2 + skip) / (gridSize + skip));
}

std::optional<int> ListZoom::nextZoomStep(int direction) const {
	if (_list->_sections.empty() || !isZoomable()) {
		return std::nullopt;
	}
	const auto currentColumns = columnsForGridSize(minGridSize());
	for (auto step = _step + direction
		; step >= kStepMin && step <= kStepMax
		; step += direction) {
		const auto columns = columnsForGridSize(minGridSizeForStep(step));
		if ((direction > 0)
			? (columns < currentColumns)
			: (columns > currentColumns)) {
			return step;
		}
	}
	return std::nullopt;
}

QPoint ListZoom::defaultAnchor() const {
	return QPoint(
		_list->width() / 2,
		(_list->_visibleTop + _list->_visibleBottom) / 2);
}

void ListZoom::zoomBy(int direction, QPoint anchor) {
	if (const auto step = nextZoomStep(direction)) {
		apply(*step, anchor);
	}
}

void ListZoom::zoomIn() {
	zoomBy(1, defaultAnchor());
}

void ListZoom::zoomOut() {
	zoomBy(-1, defaultAnchor());
}

bool ListZoom::canZoomIn() const {
	return nextZoomStep(1).has_value();
}

bool ListZoom::canZoomOut() const {
	return nextZoomStep(-1).has_value();
}

QPixmap ListZoom::grabViewport(int height) {
	const auto width = _list->width();
	if (width <= 0 || height <= 0) {
		return QPixmap();
	}
	return Ui::GrabWidget(
		_list.get(),
		QRect(0, _list->_visibleTop, width, height),
		st::boxBg->c);
}

void ListZoom::apply(int step, QPoint anchor) {
	step = std::clamp(step, kStepMin, kStepMax);
	if (step == _step || _list->_sections.empty()) {
		return;
	}
	const auto oldColumns = columnsForGridSize(minGridSize());
	const auto newColumns = columnsForGridSize(minGridSizeForStep(step));
	_step = step;
	Core::App().settings().setMediaGridZoomStep(step);
	Core::App().saveSettingsDelayed();
	if (newColumns == oldColumns) {
		return;
	}
	const auto fadeHeight = _list->_visibleBottom - _list->_visibleTop;
	const auto anchorScreen = QPoint(
		anchor.x(),
		anchor.y() - _list->_visibleTop);
	_fade.stop();
	auto oldPix = grabViewport(fadeHeight);

	_list->_scrollTopState = _list->countScrollState(anchor);
	_list->resizeToWidth(_list->width());
	_list->restoreScrollState();

	auto newPix = grabViewport(fadeHeight);
	if (!oldPix.isNull() && !newPix.isNull() && fadeHeight > 0) {
		_oldPix = std::move(oldPix);
		_newPix = std::move(newPix);
		_anchorScreen = anchorScreen;
		_fadeHeight = fadeHeight;
		_scaleRatio = float64(oldColumns) / newColumns;
		_fade.start([=] {
			_list->update(
				QRect(0, _list->_visibleTop, _list->width(), _fadeHeight));
		}, 0., 1., kFadeDuration, anim::easeOutCirc);
	}
	_list->update();
}

bool ListZoom::handleNativeGesture(not_null<QNativeGestureEvent*> e) {
	if (e->gestureType() == Qt::BeginNativeGesture) {
		_pinchAccumulated = 0.;
		return false;
	} else if (e->gestureType() != Qt::ZoomNativeGesture) {
		return false;
	} else if (!isZoomable()) {
		return false;
	}
	const auto anchor = e->pos();
	_pinchAccumulated += e->value();
	while (_pinchAccumulated >= kPinchStep) {
		_pinchAccumulated -= kPinchStep;
		zoomBy(1, anchor);
	}
	while (_pinchAccumulated <= -kPinchStep) {
		_pinchAccumulated += kPinchStep;
		zoomBy(-1, anchor);
	}
	return true;
}

bool ListZoom::processWheel(not_null<QWheelEvent*> e) {
	if (!isZoomable()) {
		return false;
	}
	const auto delta = e->angleDelta().y();
	if (!delta) {
		return false;
	}
	const auto position = e->position();
	const auto anchor = QPoint(
		qRound(position.x()),
		_list->_visibleTop + qRound(position.y()));
	zoomBy((delta > 0) ? 1 : -1, anchor);
	return true;
}

bool ListZoom::paint(QPainter &p) {
	if (_fade.animating() && !_oldPix.isNull() && !_newPix.isNull()) {
		const auto progress = _fade.value(1.);
		const auto top = _list->_visibleTop;
		const auto width = _list->width();
		const auto target = QRect(0, top, width, _fadeHeight);
		const auto pivot = QPointF(
			_anchorScreen.x(),
			top + _anchorScreen.y());
		p.setRenderHint(QPainter::SmoothPixmapTransform);
		const auto layer = [&](
				const QPixmap &pix,
				float64 scale,
				float64 opacity) {
			if (opacity <= 0.) {
				return;
			}
			p.save();
			p.setOpacity(opacity);
			p.translate(pivot);
			p.scale(scale, scale);
			p.translate(-pivot);
			p.drawPixmap(target, pix);
			p.restore();
		};
		const auto inv = 1. / _scaleRatio;
		layer(_newPix, inv + (1. - inv) * progress, progress);
		layer(_oldPix, 1. + (_scaleRatio - 1.) * progress, 1. - progress);
		return true;
	} else if (!_oldPix.isNull() || !_newPix.isNull()) {
		_oldPix = QPixmap();
		_newPix = QPixmap();
	}
	return false;
}

} // namespace Info::Media
