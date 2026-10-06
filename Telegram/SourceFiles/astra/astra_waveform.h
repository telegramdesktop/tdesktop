/*
Astra UI — waveform scrubber widget ("Sound Glass" player).
Part of the Astra UI redesign for this Telegram Desktop fork.

Renders per-track audio peaks as rounded bars; the played portion is
accent-colored, the rest muted. Click/drag emits a seek fraction.
*/
#pragma once

#include "ui/rp_widget.h"

#include <vector>
#include <functional>

namespace Astra {

class WaveformWidget final : public Ui::RpWidget {
public:
	explicit WaveformWidget(QWidget *parent);

	// peaks are normalized 0..1 (1 = full bar height)
	void setPeaks(std::vector<float> peaks);
	void setPlayedFraction(float64 fraction); // 0..1
	void setLive(bool live);                  // "playing" shimmer state
	void setBarColors(QColor played, QColor unplayed, QColor tick);

	// called on click / drag with normalized position 0..1
	std::function<void(float64)> seekRequested;

protected:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;

private:
	[[nodiscard]] float64 fractionAt(QPointF position) const;
	void requestSeek(QPointF position);

	std::vector<float> _peaks;
	float64 _played = 0.;
	bool _live = false;
	bool _dragging = false;
	QColor _playedColor;
	QColor _unplayedColor;
	QColor _tickColor;
};

} // namespace Astra
