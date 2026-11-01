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
	Fn<QImage()> gifts;
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
	void compose(const QImage &gifts);
	void composeSecondary(QRect bounds, const QImage &gifts);

	const Fn<void(Painter&, const CardFold&, QRect)> _paint;
	const CardForegroundHalo _halo;
	const Fn<void()> _repaint;
	CardFold _fold;
	QImage _content;
	QImage _cache;
	QImage _secondary;
	QImage _secondaryGifts;
	QPoint _origin;
	QPoint _secondaryOrigin;
	qint64 _giftsKey = 0;
	int _ratio = 0;
	bool _valid = false;
	rpl::lifetime _lifetime;

};

} // namespace Wallet
