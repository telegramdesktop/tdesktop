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
	QSizeF stickerSize;
};

[[nodiscard]] crl::time CardLookFadeDuration();
[[nodiscard]] float64 CardLookFadeValue(float64 progress);
[[nodiscard]] float64 CardLookWaveStart(
	const CardLookChange &change,
	QSize card);
[[nodiscard]] float64 CardLookWaveRadius(
	float64 start,
	float64 reach,
	float64 progress);
[[nodiscard]] float64 CardLookRimInside(float64 width);
[[nodiscard]] float64 CardLookRimOutside(float64 width);
void PaintCardLookRim(
	QPainter &p,
	QPointF center,
	float64 radius,
	float64 width);
void DrawDevicePixels(QPainter &p, const QRect &pixels, const QImage &image);

class CardLookTransition final {
public:
	explicit CardLookTransition(Fn<void()> repaint);
	~CardLookTransition();

	void setLook(CardLook look, CardLookChange change = {});
	void paint(Painter &p, const QRect &card, float64 angle);
	void paintForeground(
		Painter &p,
		const QRect &card,
		const Fn<void(Painter&, QColor)> &foreground);
	[[nodiscard]] CardMarkTint markTint(
		const QRect &card,
		const QRect &mark) const;
	[[nodiscard]] rpl::producer<bool> runningValue() const;

private:
	struct Layer;
	struct Frame;

	[[nodiscard]] static float64 Progress(const Layer &layer, crl::time now);

	[[nodiscard]] Frame prepare(const QPainter &p, const QRect &card) const;
	[[nodiscard]] int settledIndex(crl::time now) const;
	[[nodiscard]] bool step(crl::time now);
	void prune(crl::time now);
	void settle();
	void refresh();
	void paintBackground(
		Painter &p,
		const Frame &frame,
		int index,
		float64 angle);
	void compose(const Frame &frame, const Fn<void(Painter&, int)> &layer);
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
