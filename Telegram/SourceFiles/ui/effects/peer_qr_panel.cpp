/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/peer_qr_panel.h"

#include "ui/effects/premium_3d_support.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"

#include "styles/style_boxes.h"

namespace Ui {
namespace {

constexpr auto kOpenDuration = crl::time(420);
constexpr auto kCloseDuration = crl::time(360);
constexpr auto kCurveX1 = 0.16;
constexpr auto kCurveY1 = 1.;
constexpr auto kCurveX2 = 0.3;
constexpr auto kCurveY2 = 1.;
constexpr auto kContentInDelay = crl::time(60);
constexpr auto kContentInDuration = crl::time(220);
constexpr auto kContentOutDuration = crl::time(220);
constexpr auto kGlassAlpha = 0.25;

[[nodiscard]] QColor DefaultOpenColor() {
	auto result = QColor(255, 255, 255);
	result.setAlphaF(kGlassAlpha);
	return result;
}

[[nodiscard]] float64 ContentIn(crl::time elapsed) {
	const auto progress = (elapsed - kContentInDelay)
		/ float64(kContentInDuration);
	return anim::easeOutCubic(1., std::clamp(progress, 0., 1.));
}

} // namespace

PeerQrPanel::PeerQrPanel(not_null<QWidget*> parent)
: RpWidget(parent)
, _openColor(DefaultOpenColor())
, _animation([=](crl::time now) { return animationCallback(now); }) {
	hide();
}

PeerQrPanel::~PeerQrPanel() = default;

void PeerQrPanel::setIslandRect(QRect island) {
	if (_island == island) {
		return;
	}
	_island = island;
	if (_progress.current() < 1.) {
		updateAll();
	}
}

void PeerQrPanel::setColors(QColor island, QColor open) {
	if (_islandColor == island && _openColor == open) {
		return;
	}
	_islandColor = island;
	_openColor = open;
	update();
}

void PeerQrPanel::setContent(
		not_null<RpWidget*> content,
		PeerQrPanelContent kind) {
	Expects(content->parentWidget() == this);

	_content = content;
	_kind = kind;
	_contentCache = QImage();
	content->setGeometry(contentGeometry());
	if (kind == PeerQrPanelContent::Opaque) {
		content->setVisible(_shown && !_animation.animating());
	}
	updateAll();
}

void PeerQrPanel::toggle(bool shown, anim::type animated) {
	if (_shown == shown) {
		return;
	}
	const auto now = crl::now();
	_openedAtClose = (!shown && _animation.animating())
		? std::make_optional(now - _toggledAt)
		: std::nullopt;
	_shown = shown;
	_toggledAt = now;
	_progressFrom = _progress.current();
	if (shown) {
		show();
	}
	if (animated == anim::type::normal && !anim::Disabled()) {
		if (_content && _kind == PeerQrPanelContent::Opaque) {
			_contentCache = GrabWidgetToImage(_content);
			_content->hide();
		}
		_animation.start();
		updateAll();
	} else {
		_animation.stop();
		finishToggle();
	}
}

bool PeerQrPanel::toggled() const {
	return _shown;
}

PeerQrPanelPhase PeerQrPanel::phase() const {
	return {
		.openedAtClose = _openedAtClose,
		.elapsed = crl::now() - _toggledAt,
		.shown = _shown,
		.animating = _animation.animating(),
	};
}

QPainterPath PeerQrPanel::glassPath() const {
	const auto glass = glassRect();
	const auto radius = glassRadius(glass);
	auto result = QPainterPath();
	result.addRoundedRect(glass, radius, radius);
	return result;
}

bool PeerQrPanel::insideGlass(QPoint point) const {
	return glassPath().contains(QPointF(point));
}

void PeerQrPanel::pressOutside() {
	_outsidePresses.fire({});
}

rpl::producer<float64> PeerQrPanel::progressValue() const {
	return _progress.value();
}

rpl::producer<> PeerQrPanel::outsidePresses() const {
	return _outsidePresses.events();
}

void PeerQrPanel::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto path = glassPath();
	p.fillPath(path, glassColor());
	if (_kind != PeerQrPanelContent::Opaque) {
		return;
	}
	if (!_contentCache.isNull()) {
		p.save();
		p.setClipPath(path);
		p.setOpacity(contentOpacity(crl::now()));
		p.drawImage(contentGeometry().topLeft(), _contentCache);
		p.restore();
	}
	const auto glass = glassRect();
	const auto half = st::lineWidth / 2.;
	const auto radius = std::max(glassRadius(glass) - half, 0.);
	p.setPen(QPen(st::shadowFg->c, st::lineWidth));
	p.setBrush(Qt::NoBrush);
	p.drawRoundedRect(
		glass.marginsRemoved({ half, half, half, half }),
		radius,
		radius);
}

void PeerQrPanel::resizeEvent(QResizeEvent *e) {
	if (_content) {
		_content->setGeometry(contentGeometry());
	}
}

void PeerQrPanel::mousePressEvent(QMouseEvent *e) {
	if (!insideGlass(e->pos())) {
		pressOutside();
	}
}

bool PeerQrPanel::animationCallback(crl::time now) {
	const auto duration = _shown ? kOpenDuration : kCloseDuration;
	const auto progress = (now - _toggledAt) / float64(duration);
	if (progress >= 1.) {
		finishToggle();
		return false;
	}
	const auto eased = Premium::CubicBezier(
		kCurveX1,
		kCurveY1,
		kCurveX2,
		kCurveY2,
		std::max(progress, 0.));
	const auto target = _shown ? 1. : 0.;
	_progress = _progressFrom + (target - _progressFrom) * eased;
	updateAll();
	return true;
}

void PeerQrPanel::finishToggle() {
	_openedAtClose = std::nullopt;
	if (_content && _kind == PeerQrPanelContent::Opaque) {
		_contentCache = QImage();
		_content->setVisible(_shown);
	}
	_progress = _shown ? 1. : 0.;
	if (!_shown) {
		hide();
	}
	updateAll();
}

void PeerQrPanel::updateAll() {
	update();
	if (_content && _kind == PeerQrPanelContent::Animated) {
		_content->update();
	}
}

QRect PeerQrPanel::contentGeometry() const {
	const auto line = st::lineWidth;
	return (_kind == PeerQrPanelContent::Opaque)
		? rect().marginsRemoved({ line, line, line, line })
		: rect();
}

QRectF PeerQrPanel::glassRect() const {
	const auto progress = _progress.current();
	const auto from = QRectF(_island.translated(-pos()));
	const auto till = QRectF(rect());
	const auto lerp = [&](float64 a, float64 b) {
		return a + (b - a) * progress;
	};
	return QRectF(
		lerp(from.x(), till.x()),
		lerp(from.y(), till.y()),
		lerp(from.width(), till.width()),
		lerp(from.height(), till.height()));
}

float64 PeerQrPanel::glassRadius(QRectF glass) const {
	return std::min(
		_island.height() / 2.
			+ (st::peerQrPanelRadius - _island.height() / 2.)
				* _progress.current(),
		std::min(glass.width(), glass.height()) / 2.);
}

QColor PeerQrPanel::glassColor() const {
	return _islandColor.isValid()
		? anim::color(_islandColor, _openColor, _progress.current())
		: _openColor;
}

float64 PeerQrPanel::contentOpacity(crl::time now) const {
	const auto elapsed = now - _toggledAt;
	if (_shown) {
		return ContentIn(elapsed);
	}
	const auto out = 1. - std::clamp(
		elapsed / float64(kContentOutDuration),
		0.,
		1.);
	return _openedAtClose ? (out * ContentIn(*_openedAtClose)) : out;
}

} // namespace Ui
