/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "wallet/wallet_card_space.h"
#include "wallet/wallet_card_tint.h"

class Painter;

namespace Wallet {

struct CardForegroundHalo {
	Fn<QRect(const CardFold&)> area;
	Fn<void(Painter&, const CardFold&, QRect)> paint;
	Fn<CardMarkTint()> tint;
};

struct CardForegroundArgs {
	Fn<void(Painter&, const CardFold&, QRect)> paint;
	CardForegroundHalo halo;
	Fn<void()> repaint;
	rpl::producer<> changes;
};

class CardForeground final {
public:
	explicit CardForeground(CardForegroundArgs &&args);

	void invalidate();
	void paint(QPainter &p, const CardFold &fold);

private:
	void validate(const CardFold &fold);
	void compose();
	void composeSecondary(QRect bounds);

	const Fn<void(Painter&, const CardFold&, QRect)> _paint;
	const CardForegroundHalo _halo;
	const Fn<void()> _repaint;
	CardFold _fold;
	QImage _content;
	QImage _cache;
	QImage _secondary;
	QPoint _origin;
	QPoint _secondaryOrigin;
	int _ratio = 0;
	bool _valid = false;
	rpl::lifetime _lifetime;

};

} // namespace Wallet
