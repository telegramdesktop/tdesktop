/*
Astra UI — waveform scrubber widget.
Part of the Astra UI redesign for this Telegram Desktop fork.
*/
#include "astra/astra_waveform.h"

#include "astra/astra_design.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>

namespace Astra {
namespace {

constexpr auto kBarRadius = 0.6;
constexpr auto kBarWidthFraction = 1.1; // % of widget width per bar

} // namespace

WaveformWidget::WaveformWidget(QWidget *parent)
: Ui::RpWidget(parent)
, _playedColor(Design::Active::WavePlayed())
, _unplayedColor(Design::Active::WaveUnplayed())
, _tickColor(Design::Active::WaveTick()) {
	setCursor(Qt::PointingHandCursor);
	setAttribute(Qt::WA_OpaquePaintEvent, false);
}

void WaveformWidget::setPeaks(std::vector<float> peaks) {
	_peaks = std::move(peaks);
	update();
}

void WaveformWidget::setPlayedFraction(float64 fraction) {
	_played = std::clamp(fraction, 0., 1.);
	update();
}

void WaveformWidget::setLive(bool live) {
	if (_live != live) {
		_live = live;
		update();
	}
}

void WaveformWidget::setBarColors(
		QColor played,
		QColor unplayed,
		QColor tick) {
	_playedColor = played;
	_unplayedColor = unplayed;
	_tickColor = tick;
	update();
}

float64 WaveformWidget::fractionAt(QPointF position) const {
	return std::clamp(position.x() / std::max(width(), 1), 0., 1.);
}

void WaveformWidget::requestSeek(QPointF position) {
	if (seekRequested) {
		seekRequested(fractionAt(position));
	}
}

void WaveformWidget::mousePressEvent(QMouseEvent *e) {
	_dragging = true;
	requestSeek(e->position());
}

void WaveformWidget::mouseMoveEvent(QMouseEvent *e) {
	if (_dragging) {
		requestSeek(e->position());
	}
}

void WaveformWidget::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	p.setRenderHint(QPainter::Antialiasing, true);

	const auto w = width();
	const auto h = height();
	if (w <= 0 || h <= 0 || _peaks.empty()) {
		return;
	}

	const auto bars = int(_peaks.size());
	const auto step = w / float64(bars);
	const auto barWidth = std::max<qreal>(
		1.2,
		step * (kBarWidthFraction / 2.) * 2.);
	const auto radius = std::min(kBarRadius, barWidth / 2.);

	const auto playedX = _played * w;
	const auto hoverTickX = _dragging ? playedX : -1.;

	for (auto i = 0; i != bars; ++i) {
		const auto peak = std::clamp(_peaks[i], 0.f, 1.f);
		const auto barHeight = std::max<qreal>(
			3.,
			(0.12 + 0.88 * peak) * h);
		const auto x = i * step;
		const auto y = (h - barHeight) / 2.;

		const auto playedBar = (x + barWidth / 2.) <= playedX;
		auto color = playedBar ? _playedColor : _unplayedColor;
		if (_live && playedBar && (i % 7) == 3) {
			color = _tickColor;
		}
		p.setPen(Qt::NoPen);
		p.setBrush(color);
		p.drawRoundedRect(
			QRectF(x, y, barWidth, barHeight),
			radius,
			radius);
	}

	// scrub position tick
	if (hoverTickX >= 0.) {
		p.setBrush(_tickColor);
		p.drawRect(QRectF(playedX - 0.5, 0., 1.5, h));
	}
}

} // namespace Astra
