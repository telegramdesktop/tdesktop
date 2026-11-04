/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/editor_round_record_button.h"

#include "ui/paint/blobs.h"
#include "ui/painter.h"
#include "styles/style_editor.h"

#include <QtGui/QMouseEvent>

namespace Editor {
namespace {

constexpr auto kMaxLevel = 1.;
constexpr auto kLevelDuration = 100. + 500. * 0.33;
constexpr auto kBlobMaxSpeed = 5.0;
constexpr auto kMinorBlobAlpha = 0.3;
constexpr auto kMajorBlobAlpha = 0.15;

[[nodiscard]] float BlobMinScale(int minRadius) {
	return float(st::storyRecordButtonRadius) / float(minRadius);
}

[[nodiscard]] std::vector<Ui::Paint::Blobs::BlobData> Blobs() {
	return {
		{
			.segmentsCount = 9,
			.minScale = BlobMinScale(st::storyRecordMinorBlobMinRadius),
			.minRadius = float(st::storyRecordMinorBlobMinRadius),
			.maxRadius = float(st::storyRecordMinorBlobMaxRadius),
			.speedScale = 1.,
			.alpha = kMinorBlobAlpha,
			.maxSpeed = kBlobMaxSpeed,
		},
		{
			.segmentsCount = 12,
			.minScale = BlobMinScale(st::storyRecordMajorBlobMinRadius),
			.minRadius = float(st::storyRecordMajorBlobMinRadius),
			.maxRadius = float(st::storyRecordMajorBlobMaxRadius),
			.speedScale = 1.,
			.alpha = kMajorBlobAlpha,
			.maxSpeed = kBlobMaxSpeed,
		},
	};
}

} // namespace

RoundRecordButton::RoundRecordButton(not_null<QWidget*> parent)
: AbstractButton(parent)
, _blobs(std::make_unique<Ui::Paint::Blobs>(
	Blobs(),
	kLevelDuration,
	kMaxLevel))
, _center(int(std::ceil(_blobs->maxRadius()))) {
	resize(_center * 2, _center * 2);
	_levelAnimation.init([=](crl::time now) {
		_blobs->updateLevel(now - _lastUpdate);
		_lastUpdate = now;
		update();
		return true;
	});
	hide();
}

RoundRecordButton::~RoundRecordButton() = default;

QPoint RoundRecordButton::center() const {
	return QPoint(_center, _center);
}

void RoundRecordButton::setAmplitude(float64 amplitude) {
	_blobs->setLevel(std::clamp(amplitude, 0., 1.));
}

void RoundRecordButton::showAnimated() {
	_shown = true;
	_blobs->resetLevel();
	_lastUpdate = crl::now();
	show();
	raise();
	if (!anim::Disabled()) {
		_levelAnimation.start();
	}
	_shownAnimation.start([=] {
		update();
	}, 0., 1., st::universalDuration);
}

void RoundRecordButton::hideAnimated() {
	if (!_shown) {
		return;
	}
	_shown = false;
	_levelAnimation.stop();
	_shownAnimation.start([=] {
		update();
		if (!_shownAnimation.animating()) {
			hide();
		}
	}, 1., 0., st::universalDuration);
}

bool RoundRecordButton::inCircle(QPoint point) const {
	const auto radius = st::storyRecordButtonMaxRadius;
	const auto delta = point - center();
	return QPoint::dotProduct(delta, delta) <= radius * radius;
}

void RoundRecordButton::mousePressEvent(QMouseEvent *e) {
	if (inCircle(e->pos())) {
		AbstractButton::mousePressEvent(e);
	} else {
		e->ignore();
	}
}

void RoundRecordButton::mouseMoveEvent(QMouseEvent *e) {
	setPointerCursor(inCircle(e->pos()));
	AbstractButton::mouseMoveEvent(e);
}

void RoundRecordButton::paintEvent(QPaintEvent *e) {
	const auto shown = _shownAnimation.value(_shown ? 1. : 0.);
	if (shown <= 0.) {
		return;
	}
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	p.translate(_center, _center);
	const auto brush = QBrush(st::storyRecordButtonBg->c);
	_blobs->paint(p, brush, shown);

	const auto level = _blobs->currentLevel();
	const auto grown = st::storyRecordButtonRadius
		+ st::storyRecordButtonLevelRadius * level;
	const auto radius = std::min(
		grown,
		float64(st::storyRecordButtonMaxRadius)) * shown;
	p.setPen(Qt::NoPen);
	p.setBrush(brush);
	p.drawEllipse(QPointF(), radius, radius);

	const auto half = st::storyRecordStopSize * shown / 2.;
	const auto rounding = st::storyRecordStopRadius * shown;
	p.setBrush(st::storyRecordButtonFg);
	p.drawRoundedRect(
		QRectF(-half, -half, half * 2., half * 2.),
		rounding,
		rounding);
}

} // namespace Editor
