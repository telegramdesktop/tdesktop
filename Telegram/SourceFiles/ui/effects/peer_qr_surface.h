/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animations.h"
#include "ui/rp_widget.h"

namespace Ui {

struct PeerQrAppearance {
	std::vector<QColor> colors;
	QString patternPath;
	float64 patternOpacity = 0.;
	QColor edge;
};

struct PeerQrColors {
	QColor ink;
	QColor foreground;
	QColor control;
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
	int patternHeight = 0;
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
	void startAnimations();

	[[nodiscard]] const PeerQrColors &colors() const;
	[[nodiscard]] PeerQrImageArgs prepareImage() const;

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	struct Card;

	void loadPattern();
	void composeNow();
	void refreshCard();
	void refreshQr();
	void checkAnimation();
	[[nodiscard]] bool animationCallback(crl::time now);
	[[nodiscard]] bool backgroundDue(crl::time now) const;
	[[nodiscard]] Fn<QImage()> prepareBackground(crl::time now);
	void requestBackground(crl::time now);
	void applyBackground(QImage image);
	[[nodiscard]] QRect logoRect() const;
	[[nodiscard]] QRect cardUpdateRect() const;

	const PeerQrAppearance _appearance;
	const PeerQrColors _colors;
	const std::unique_ptr<Card> _card;
	const std::array<QImage, 4> _corners;
	QImage _pattern;
	QImage _background;
	QString _link;
	QString _username;
	QRect _cardGeometry;
	QPointF _driftOffset;
	crl::time _startedAt = 0;
	crl::time _driftTime = 0;
	crl::time _backgroundRequestedAt = 0;
	crl::time _patternShownAt = 0;
	crl::time _bounceStartedAt = 0;
	bool _backgroundComposing = false;
	bool _blooming = false;
	Animations::Basic _animation;

};

} // namespace Ui
