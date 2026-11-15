/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"
#include "ui/effects/animations.h"
#include "wallet/wallet_card_space.h"

class Painter;

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Wallet {

struct CardFloatArgs {
	not_null<Ui::RpWidget*> body;
	Fn<CardFold()> inPlace;
	rpl::producer<bool> arriving;
	Fn<void(Painter&, const CardFold&, QRect)> paint;
	Fn<void()> changed;
};

class CardFloat final {
public:
	explicit CardFloat(CardFloatArgs &&args);
	~CardFloat();

	void bring();
	void update();
	[[nodiscard]] bool shown() const;
	[[nodiscard]] const CardFold &fold() const;
	[[nodiscard]] not_null<Ui::RpWidget*> widget() const;

private:
	enum class Phase : uchar {
		Hidden,
		In,
		Hold,
		Linger,
		Back,
		Out,
	};

	[[nodiscard]] bool tick(crl::time now);
	void step(crl::time now, const CardFold &live, const CardFold &rest);
	void setArriving(bool arriving);
	void startSegment(Phase phase, crl::time now);
	void present(CardFold fold, float64 shadow);
	void finish(CardFold fold);
	void paint(QRect clip);
	void paintShadow(QPainter &p);
	void validateShadow();

	const Fn<CardFold()> _inPlace;
	const Fn<void(Painter&, const CardFold&, QRect)> _paint;
	const Fn<void()> _changed;
	base::unique_qptr<Ui::RpWidget> _layer;
	CardFold _fold;
	CardFold _from;
	QImage _shadowCache;
	crl::time _started = 0;
	float64 _shadow = 0.;
	float64 _fromShadow = 0.;
	Phase _phase = Phase::Hidden;
	bool _arriving = false;
	Ui::Animations::Basic _animation;
	rpl::lifetime _lifetime;

};

} // namespace Wallet
