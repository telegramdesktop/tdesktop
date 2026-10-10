/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "ui/effects/animations.h"
#include "ui/rp_widget.h"

namespace style {
struct ImportantTooltip;
} // namespace style

namespace Ui {

struct GlareTooltipColors {
	QColor edge;
	QColor center;
	QColor rim;
	QColor text;
};

class GlareTooltip final : public RpWidget {
public:
	GlareTooltip(
		not_null<QWidget*> parent,
		const style::ImportantTooltip &st,
		const style::font &font,
		const QString &text,
		GlareTooltipColors colors);

	void trackWidget(not_null<QWidget*> pointTo);
	void pointAt(QRect area, QRect within);

	void fade(bool shown);
	void finishAnimating();
	void setOpacity(float64 opacity);
	void setColors(GlareTooltipColors colors);
	void stopGlare();

	[[nodiscard]] crl::time glarePeriod() const;
	[[nodiscard]] crl::time glaresDuration(int glares) const;

private:
	void paintEvent(QPaintEvent *e) override;
	void prepareImage();
	void showGlare();

	const style::ImportantTooltip &_st;
	QString _text;
	const style::font &_font;
	GlareTooltipColors _colors;
	QSize _inner;
	QSize _outer;
	int _stroke = 0;
	int _skip = 0;
	QSize _full;
	int _glareSize = 0;
	int _glareRange = 0;
	crl::time _glareDuration = 0;
	base::Timer _glareTimer;

	Animations::Simple _showAnimation;
	Animations::Simple _glareAnimation;

	QImage _image;
	int _glareRight = 0;
	int _imageGlareRight = 0;
	int _arrowMiddle = 0;
	int _imageArrowMiddle = 0;

	bool _shown = false;
	float64 _opacity = 1.;
	rpl::lifetime _trackLifetime;

};

} // namespace Ui
