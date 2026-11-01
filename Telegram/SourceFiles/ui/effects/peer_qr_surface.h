/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animations.h"
#include "ui/effects/peer_qr_emoji_field.h"
#include "ui/effects/peer_qr_gift_field.h"
#include "ui/rp_widget.h"

namespace Ui {

struct PeerQrAppearance {
	QColor edge;
	QColor center;
	std::vector<QString> emoji;
	PeerQrGiftLook gift;

	[[nodiscard]] bool empty() const {
		return !center.isValid() && emoji.empty() && !gift;
	}

	friend inline bool operator==(
		const PeerQrAppearance &,
		const PeerQrAppearance &) = default;
};

[[nodiscard]] PeerQrAppearance PeerQrDefaultAppearance();

struct PeerQrColors {
	QColor ink;
	QColor foreground;
	QColor control;
	QColor glass;

	friend inline bool operator==(
		const PeerQrColors &,
		const PeerQrColors &) = default;
};

[[nodiscard]] int PeerQrCardHeight(int cardWidth, bool withUsername);
[[nodiscard]] float64 PeerQrBounceScale(float64 progress);

struct PeerQrImageArgs {
	PeerQrAppearance appearance;
	PeerQrColors colors;
	QString link;
	QFont usernameFont;
	QStringList usernameLines;
	int cardWidth = 0;
	int margin = 0;
	int shadowRadius = 0;
	int shadowShift = 0;
	PeerQrEmojiRest emoji;
	PeerQrGiftRest gift;
	QPointF layerOffset;
	float64 layerBand = 0.;
};

[[nodiscard]] QImage RenderPeerQrImage(PeerQrImageArgs args);

class PeerQrSurface final : public RpWidget {
public:
	PeerQrSurface(QWidget *parent, PeerQrAppearance appearance);
	~PeerQrSurface();

	void setLink(
		const QString &link,
		anim::type animated = anim::type::instant);
	void setUsername(const QString &username);
	void setCountdown(const QString &countdown);
	void setCardGeometry(QRect card, QRectF centerReach);
	void setCardPlacement(QPointF topLeft, float64 scale);
	void setAppearance(PeerQrAppearance appearance, anim::type animated);
	void setEmojiSource(PeerQrEmojiSource source);
	void setGiftSource(PeerQrGiftSource source);
	void startAnimations();

	[[nodiscard]] const PeerQrAppearance &appearance() const;
	[[nodiscard]] const PeerQrColors &colors() const;
	[[nodiscard]] rpl::producer<PeerQrColors> colorsValue() const;
	[[nodiscard]] rpl::producer<not_null<QTouchEvent*>> touchPresses() const;
	[[nodiscard]] PeerQrImageArgs prepareImage() const;
	// the child's control tint lands on the clean backdrop inside its shape
	void addControl(
		not_null<QWidget*> control,
		Fn<QPainterPath()> shape,
		Fn<float64()> opacity = nullptr);
	void setCovered(Fn<QRegion()> covered);

protected:
	bool eventHook(QEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

private:
	struct Card;
	struct Countdown;
	struct Control;
	struct ControlShapes;
	struct CardPlacement {
		QPointF topLeft;
		float64 scale = 1.;
	};
	struct Background {
		QImage image;
		QPointF center;
	};
	struct Wave {
		QImage from;
		QImage clean;
		PeerQrColors colorsFrom;
		QColor tint;
		QPointF center;
		float64 farthest = 0.;
		crl::time startedAt = 0;
	};

	void composeNow();
	void refreshCard();
	void refreshUsername(anim::type animated = anim::type::instant);
	void refreshQr();
	void recolorQr();
	void setShownColors(const PeerQrColors &colors);
	void ensureField();
	void refreshFieldClock();
	[[nodiscard]] crl::time fieldElapsed(crl::time now) const;
	[[nodiscard]] PeerQrLayerFalloff layerFalloff() const;
	[[nodiscard]] bool layerShown() const;
	[[nodiscard]] QPainterPath fieldClip(crl::time now, bool leaving) const;
	[[nodiscard]] PeerQrEmojiPaint fieldArgs(crl::time now) const;
	void clipField(
		QPainter &p,
		crl::time now,
		bool leaving,
		const QPainterPath &shown) const;
	void paintField(QPainter &p, crl::time now, const QPainterPath &shown);
	void paintGift(
		QPainter &p,
		not_null<PeerQrGiftField*> field,
		crl::time now,
		bool leaving,
		const QPainterPath &shown);
	[[nodiscard]] ControlShapes controlShapes(QRect clip) const;
	void paintLayers(
		QPainter &p,
		crl::time now,
		const QPainterPath &shown);
	void paintFadingUnderlays(
		QPainter &p,
		crl::time now,
		const ControlShapes &controls) const;
	void startFieldEntry(crl::time now);
	void updateWave(crl::time now);
	void paintWave(
		QPainter &p,
		crl::time now,
		const QPainterPath &clean) const;
	void paintClean(
		QPainter &p,
		crl::time now,
		QRect area,
		const std::vector<QRect> &parts) const;
	void paintCountdown(QPainter &p);
	[[nodiscard]] QImage currentPicture(crl::time now, bool clean) const;
	[[nodiscard]] bool handleTouch(not_null<QTouchEvent*> e);
	void stopTouchSwipe();
	void checkAnimation();
	[[nodiscard]] bool animationCallback(crl::time now);
	[[nodiscard]] QPointF gradientCenter() const;
	[[nodiscard]] QRectF gradientReach(QPointF center) const;
	[[nodiscard]] QRect backgroundSource(QPointF center) const;
	[[nodiscard]] bool backgroundStale() const;
	[[nodiscard]] Fn<Background()> prepareBackground() const;
	void requestBackground();
	void applyBackground(Background background);
	void paintBackground(QPainter &p, QRect area) const;
	[[nodiscard]] QRegion covered() const;
	void updateField();
	[[nodiscard]] QRectF cardRect() const;
	[[nodiscard]] float64 cardScale() const;
	[[nodiscard]] QRect logoRect() const;
	[[nodiscard]] QRect cardUpdateRect() const;
	[[nodiscard]] QRect labelUpdateRect() const;

	PeerQrAppearance _appearance;
	PeerQrColors _colors;
	PeerQrColors _shownColors;
	rpl::event_stream<PeerQrColors> _colorsChanges;
	rpl::event_stream<not_null<QTouchEvent*>> _touchPresses;
	const std::unique_ptr<Card> _card;
	std::unique_ptr<Countdown> _countdown;
	Background _background;
	QString _link;
	QString _username;
	QRect _cardGeometry;
	QRectF _cardReach;
	std::optional<CardPlacement> _cardPlacement;
	std::optional<Wave> _wave;
	PeerQrEmojiSource _emojiSource;
	std::unique_ptr<PeerQrEmojiField> _field;
	PeerQrGiftSource _giftSource;
	std::unique_ptr<PeerQrGiftField> _giftField;
	std::unique_ptr<PeerQrGiftField> _giftLeaving;
	PeerQrLayerMotion _layer;
	std::optional<int> _touchId;
	std::vector<Control> _controls;
	Fn<QRegion()> _covered;
	crl::time _startedAt = 0;
	crl::time _bounceStartedAt = 0;
	crl::time _dipStartedAt = 0;
	crl::time _fieldFrozenAt = 0;
	crl::time _fieldShift = 0;
	uint32 _backgroundGeneration = 0;
	bool _backgroundComposing = false;
	bool _blooming = false;
	bool _fieldLeaving = false;
	bool _entryPending = false;
	Animations::Basic _animation;

};

} // namespace Ui
