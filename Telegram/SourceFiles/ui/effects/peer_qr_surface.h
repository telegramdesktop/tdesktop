/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animations.h"
#include "ui/effects/peer_qr_emoji_field.h"
#include "ui/rp_widget.h"

namespace Ui {

struct PeerQrAppearance {
	QColor edge;
	QColor center;
	std::vector<QString> emoji;
	QColor background;

	[[nodiscard]] bool empty() const {
		return !center.isValid() && emoji.empty();
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
	void setCardGeometry(QRect card);
	void setCardPlacement(QPointF topLeft, float64 scale);
	void setAppearance(PeerQrAppearance appearance, anim::type animated);
	void setEmojiSource(PeerQrEmojiSource source);
	void startAnimations();

	[[nodiscard]] const PeerQrAppearance &appearance() const;
	[[nodiscard]] const PeerQrColors &colors() const;
	[[nodiscard]] rpl::producer<PeerQrColors> colorsValue() const;
	[[nodiscard]] PeerQrImageArgs prepareImage() const;

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	struct Card;
	struct CardPlacement {
		QPointF topLeft;
		float64 scale = 1.;
	};
	struct Wave {
		QImage from;
		PeerQrColors colorsFrom;
		QColor tint;
		QPointF center;
		float64 farthest = 0.;
		crl::time startedAt = 0;
	};
	struct InkBlend {
		PeerQrColors from;
		crl::time startedAt = 0;
	};

	void composeNow();
	void refreshCard();
	void refreshQr();
	void recolorQr();
	void setShownColors(const PeerQrColors &colors);
	void ensureField();
	[[nodiscard]] PeerQrColors computeColors() const;
	void refreshColors(bool animated);
	void refreshShadow();
	void refreshFieldClock();
	[[nodiscard]] crl::time fieldElapsed(crl::time now) const;
	void paintField(QPainter &p, crl::time now);
	void updateWave(crl::time now);
	void paintWave(QPainter &p, crl::time now) const;
	[[nodiscard]] QImage currentPicture(crl::time now) const;
	void checkAnimation();
	[[nodiscard]] bool animationCallback(crl::time now);
	[[nodiscard]] QPointF gradientCenter() const;
	[[nodiscard]] bool backgroundStale() const;
	[[nodiscard]] Fn<QImage()> prepareBackground();
	void requestBackground();
	void applyBackground(QImage image);
	[[nodiscard]] QRectF cardRect() const;
	[[nodiscard]] float64 cardScale() const;
	[[nodiscard]] QRect logoRect() const;
	[[nodiscard]] QRect cardUpdateRect() const;

	PeerQrAppearance _appearance;
	PeerQrColors _colors;
	PeerQrColors _shownColors;
	rpl::event_stream<PeerQrColors> _colorsChanges;
	const std::unique_ptr<Card> _card;
	const std::array<QImage, 4> _corners;
	QImage _background;
	QPointF _backgroundCenter;
	QString _link;
	QString _username;
	QRect _cardGeometry;
	std::optional<CardPlacement> _cardPlacement;
	std::optional<Wave> _wave;
	PeerQrEmojiSource _emojiSource;
	std::unique_ptr<PeerQrEmojiField> _field;
	std::optional<InkBlend> _inkBlend;
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
