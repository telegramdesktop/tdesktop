/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animations.h"
#include "wallet/wallet_card_look.h"

class Painter;

namespace Wallet {

class CardStickers;

enum class CardLookReveal : uchar {
	Instant,
	Wave,
	Fade,
};

struct CardLookChange {
	CardLookReveal reveal = CardLookReveal::Instant;
	QPointF origin;
};

class CardLookTransition final {
public:
	explicit CardLookTransition(Fn<void()> repaint);
	~CardLookTransition();

	void setLook(CardLook look, CardLookChange change = {});
	void paint(
		Painter &p,
		const QRect &card,
		float64 angle,
		const Fn<void(Painter&, QColor)> &foreground);
	[[nodiscard]] rpl::producer<bool> runningValue() const;

private:
	struct Layer;
	struct Frame;

	[[nodiscard]] static float64 Progress(const Layer &layer, crl::time now);

	[[nodiscard]] int settledIndex(crl::time now) const;
	[[nodiscard]] bool step(crl::time now);
	void prune(crl::time now);
	void settle();
	void refresh();
	void compose(
		const Frame &frame,
		const Fn<void(Painter&, QColor)> &foreground);
	void reveal(const Layer &layer, const Frame &frame);

	const Fn<void()> _repaint;
	std::vector<Layer> _layers;
	QImage _composed;
	QImage _scratch;
	rpl::variable<bool> _running;
	Ui::Animations::Basic _animation;
	rpl::lifetime _lifetime;

};

struct CardLookFollowerArgs {
	not_null<Main::Session*> session;
	not_null<CardStickers*> stickers;
	Fn<void(CardLook, CardLookChange)> apply;
};

class CardLookFollower final {
public:
	explicit CardLookFollower(CardLookFollowerArgs &&args);
	~CardLookFollower();

private:
	void changed(CardLook look);
	void released(const QString &address);
	void wave(CardLook look);
	void present(CardLook look, CardLookChange change);
	void clear();

	const not_null<Main::Session*> _session;
	const not_null<CardStickers*> _stickers;
	const Fn<void(CardLook, CardLookChange)> _apply;
	CardLook _target;
	std::optional<CardLook> _waiting;
	bool _initialized = false;
	rpl::lifetime _lifetime;

};

} // namespace Wallet
