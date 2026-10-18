/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtGui/QTransform>

namespace Wallet {

struct CardViewPose {
	QPointF center;
	float64 angle = 0.;
	float64 length = 1.;
	float64 height = 1.;
	float64 lean = 0.;
	float64 opacity = 1.;
	bool rest = false;
};

struct CardViewPlaces {
	QRect rest;
	std::optional<QRect> slot;
};

struct CardViewFrame {
	CardViewPose card;
	QRectF slot;
	float64 clip = 0.;
	float64 slide = 0.;
	float64 glide = 0.;
	float64 lift = 0.;
	float64 dim = 0.;
	float64 blur = 0.;
	float64 label = 0.;
	float64 button = 0.;
	float64 rebase = 1.;
	bool patch = false;
	bool finished = false;
};

[[nodiscard]] bool ValidCardViewSlot(const CardViewPlaces &places);
[[nodiscard]] CardViewFrame CardViewRestFrame(const CardViewPlaces &places);
[[nodiscard]] std::optional<QTransform> CardViewTransform(
	QSize card,
	const CardViewPose &pose);

class CardViewMotion final {
public:
	void open(const CardViewPlaces &places);
	void close(const CardViewFrame &from, const CardViewPlaces &places);
	void loseSlot(
		const CardViewFrame &painted,
		crl::time elapsed,
		const CardViewPlaces &places);
	[[nodiscard]] bool retarget(
		const CardViewFrame &painted,
		crl::time elapsed,
		const CardViewPlaces &places);

	[[nodiscard]] bool usesSlot() const;
	[[nodiscard]] CardViewFrame frame(
		crl::time elapsed,
		const CardViewPlaces &places) const;

private:
	struct Loss {
		CardViewPose card;
		QRectF slot;
		float64 clip = 0.;
		crl::time at = 0;
	};

	struct Rebase {
		CardViewFrame from;
		CardViewPlaces places;
		float64 at = 0.;
		float64 end = 0.;
	};

	[[nodiscard]] float64 cardEnd() const;
	[[nodiscard]] CardViewFrame opening(
		float64 time,
		const CardViewPlaces &places) const;
	[[nodiscard]] CardViewFrame closing(
		float64 time,
		const CardViewPlaces &places) const;

	CardViewFrame _from;
	std::optional<Loss> _loss;
	std::optional<Rebase> _rebase;
	float64 _insertDuration = 0.;
	bool _closing = false;
	bool _slot = false;

};

} // namespace Wallet
