/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animations.h"
#include "wallet/wallet_card_space.h"

class Painter;

namespace Wallet {

struct CardForegroundArgs {
	Fn<void(Painter&, const CardFold&, QRect)> paint;
	Fn<void()> repaint;
	rpl::producer<bool> hidden;
	rpl::producer<> changes;
};

class CardForeground final {
public:
	explicit CardForeground(CardForegroundArgs &&args);

	void invalidate();
	void paint(QPainter &p, const CardFold &fold);

private:
	void setHidden(bool hidden);
	void validate(const CardFold &fold);
	void compose();

	const Fn<void(Painter&, const CardFold&, QRect)> _paint;
	const Fn<void()> _repaint;
	CardFold _fold;
	QImage _content;
	QImage _cache;
	QPoint _origin;
	int _ratio = 0;
	float64 _target = 1.;
	bool _valid = false;
	Ui::Animations::Simple _animation;
	rpl::lifetime _lifetime;

};

} // namespace Wallet
