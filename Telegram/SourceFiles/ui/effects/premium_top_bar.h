/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"
#include "ui/rp_widget.h"
#include "ui/effects/premium_stars_colored.h"

namespace style {
struct PremiumCover;
} // namespace style

namespace st {
extern const style::PremiumCover &defaultPremiumCover;
} // namespace st

namespace Ui {
class FlatLabel;
} // namespace Ui

namespace Lottie {
class Icon;
} // namespace Lottie

namespace Ui::Premium {

class Star;
class Diamond;
class Coin;
class StarParticles;

class TopBarAbstract : public RpWidget {
public:
	TopBarAbstract(
		QWidget *parent = nullptr,
		const style::PremiumCover &st = st::defaultPremiumCover);

	void setRoundEdges(bool value);

	virtual void setPaused(bool paused) = 0;
	virtual void setTextPosition(int x, int y) = 0;

	[[nodiscard]] virtual rpl::producer<int> additionalHeight() const = 0;

	[[nodiscard]] const style::PremiumCover &st() const {
		return _st;
	}

protected:
	void paintEdges(QPainter &p, const QBrush &brush) const;
	void paintEdges(QPainter &p) const;

	[[nodiscard]] QRectF starRect(
		float64 topProgress,
		float64 sizeProgress) const;

	[[nodiscard]] bool isDark() const;
	void computeIsDark();

private:
	const style::PremiumCover &_st;
	bool _roundEdges = true;
	bool _isDark = false;

};

struct TopBarDescriptor {
	Fn<QVariant()> clickContextOther;
	QString logo;
	rpl::producer<QString> title;
	rpl::producer<TextWithEntities> about;
	bool light = false;
	bool optimizeMinistars = true;
	bool use3dStar = false;
	bool star3dGolden = false;
	bool use3dDiamond = false;
	bool use3dCoin = false;
	std::optional<QGradientStops> gradientStops;
	rpl::producer<> showFinished;
};

class TopBar final : public TopBarAbstract {
public:
	TopBar(
		not_null<QWidget*> parent,
		const style::PremiumCover &st,
		TopBarDescriptor &&descriptor);
	~TopBar();

	void setPaused(bool paused) override;
	void setTextPosition(int x, int y) override;

	rpl::producer<int> additionalHeight() const override;

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	const bool _light = false;
	const QString _logo;
	const style::font &_titleFont;
	const style::margins &_titlePadding;
	const int _aboutMaxWidth = 0;
	object_ptr<FlatLabel> _about;
	ColoredMiniStars _ministars;
	QSvgRenderer _star;
	QImage _dollar;
	std::unique_ptr<Lottie::Icon> _lottie;
	Star *_star3d = nullptr;
	bool _star3dGolden = false;
	Diamond *_diamond3d = nullptr;
	Coin *_coin3d = nullptr;
	std::unique_ptr<StarParticles> _particles3d;

	struct {
		float64 top = 0.;
		float64 body = 0.;
		float64 title = 0.;
		float64 scaleTitle = 0.;
	} _progress;

	QRectF _starRect;

	QPoint _titlePosition;
	QPainterPath _titlePath;

};

} // namespace Ui::Premium
