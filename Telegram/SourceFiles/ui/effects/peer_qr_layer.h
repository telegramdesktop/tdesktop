/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {

struct PeerQrLayerPlace {
	QPointF position;
	float64 scale = 1.;
};

struct PeerQrLayerFalloff {
	QRectF area;
	float64 band = 0.;

	[[nodiscard]] PeerQrLayerPlace place(QPointF flat) const;
	[[nodiscard]] QRectF reach(float64 margin) const;
};

class PeerQrLayerMotion final {
public:
	void press(QPointF position, crl::time now);
	void move(QPointF position, crl::time now);
	void release(crl::time now, bool glide);
	void advance(crl::time now, crl::time elapsed, float64 driftSpeed);

	[[nodiscard]] QPointF offset() const;
	[[nodiscard]] bool pressed() const;
	[[nodiscard]] bool animating() const;

private:
	struct Sample {
		crl::time time = 0;
		QPointF position;
	};
	struct Glide {
		QPointF from;
		QPointF direction;
		float64 speed = 0.;
		crl::time startedAt = 0;
		crl::time duration = 0;
	};

	[[nodiscard]] QPointF releaseVelocity(crl::time now) const;
	[[nodiscard]] float64 driftScale(crl::time now) const;
	void advanceGlide(crl::time now);

	QPointF _offset;
	QPointF _pressPosition;
	QPointF _pressOffset;
	std::vector<Sample> _samples;
	std::optional<Glide> _glide;
	crl::time _restSince = 0;
	crl::time _driftElapsed = 0;
	bool _pressed = false;
	bool _dragging = false;

};

} // namespace Ui
