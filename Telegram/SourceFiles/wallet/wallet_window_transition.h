/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"
#include "base/weak_ptr.h"
#include "ui/effects/animations.h"
#include "wallet/wallet_card_space.h"
#include "wallet/wallet_card_transition.h"

namespace Ui {
class RpWidget;
class SeparatePanel;
} // namespace Ui

namespace Wallet {

class CardStickers;
class WindowPalette;

struct WindowLookRevealArgs {
	not_null<Ui::SeparatePanel*> panel;
	not_null<Ui::RpWidget*> body;
	std::shared_ptr<WindowPalette> palette;
	Fn<CardFold()> fold;
	Fn<void(CardLook, CardLookChange)> apply;
};

class WindowLookReveal final : public base::has_weak_ptr {
public:
	explicit WindowLookReveal(WindowLookRevealArgs &&args);
	~WindowLookReveal();

	void apply(CardLook look, CardLookChange change);
	[[nodiscard]] rpl::producer<bool> runningValue() const;

private:
	struct Pending {
		CardLook look;
		CardLookChange change;
	};
	struct Source {
		QPointF origin;
		float64 width = 0.;
		float64 start = 0.;
	};

	[[nodiscard]] QRect area() const;
	[[nodiscard]] bool hidden() const;
	[[nodiscard]] bool live() const;
	[[nodiscard]] std::optional<Source> source(
		const CardLookChange &change) const;
	[[nodiscard]] QTransform reprojection() const;
	[[nodiscard]] QTransform imageMap(QPointF shift) const;
	[[nodiscard]] float64 progress() const;
	[[nodiscard]] std::optional<QRect> hole() const;

	void instant(CardLook look);
	void animate(CardLook look, CardLookChange change);
	void paintFirstFrame();
	void install(
		QImage image,
		QRect pixels,
		QRect area,
		std::optional<Source> source,
		crl::time started);
	void settle();
	void flush();
	void finish();
	void dropOverlay();
	void updateGeometry();
	void refresh();
	void restack();
	bool watch(not_null<QObject*> child);
	[[nodiscard]] bool tick(crl::time now);
	void paint(QPainter &p) const;
	void compose(QPainter &p, not_null<QWidget*> widget) const;

	const not_null<Ui::SeparatePanel*> _panel;
	const not_null<Ui::RpWidget*> _body;
	const std::shared_ptr<WindowPalette> _palette;
	const Fn<CardFold()> _fold;
	const Fn<void(CardLook, CardLookChange)> _apply;
	std::optional<Pending> _pending;
	base::unique_qptr<Ui::RpWidget> _overlay;
	base::unique_qptr<Ui::RpWidget> _band;
	QImage _image;
	QRectF _imageRect;
	QRect _area;
	Source _source;
	CardLookReveal _reveal = CardLookReveal::Instant;
	crl::time _started = 0;
	crl::time _now = 0;
	bool _restacking = false;
	bool _firstFramePending = false;
	bool _firstFrameQueued = false;
	rpl::variable<bool> _running = false;
	Ui::Animations::Basic _animation;
	rpl::lifetime _overlayLifetime;
	rpl::lifetime _lifetime;

};

struct WindowLookTransitionArgs {
	not_null<Main::Session*> session;
	not_null<CardStickers*> stickers;
	not_null<Ui::SeparatePanel*> panel;
	not_null<Ui::RpWidget*> body;
	std::shared_ptr<WindowPalette> palette;
	Fn<CardFold()> fold;
	Fn<void(CardLook, CardLookChange)> apply;
};

class WindowLookTransition final {
public:
	explicit WindowLookTransition(WindowLookTransitionArgs &&args);
	~WindowLookTransition();

	[[nodiscard]] rpl::producer<bool> runningValue() const;

private:
	const Fn<void(CardLook, CardLookChange)> _card;
	std::unique_ptr<WindowLookReveal> _reveal;
	std::unique_ptr<CardLookFollower> _follower;
	rpl::lifetime _lifetime;

};

} // namespace Wallet
