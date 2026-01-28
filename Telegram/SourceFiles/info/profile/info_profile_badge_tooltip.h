/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "ui/effects/animations.h"
#include "base/timer.h"

namespace style {
struct ImportantTooltip;
} // namespace style

namespace Data {
struct EmojiStatusCollectible;
} // namespace Data

namespace Info::Profile {

class BadgeTooltip final : public Ui::RpWidget {
public:
	BadgeTooltip(
		not_null<QWidget*> parent,
		std::shared_ptr<Data::EmojiStatusCollectible> collectible,
		not_null<QWidget*> pointTo);

	void fade(bool shown);
	void finishAnimating();
	void setOpacity(float64 opacity);

	[[nodiscard]] crl::time glarePeriod() const;

private:
	void paintEvent(QPaintEvent *e) override;
	void setupGeometry(not_null<QWidget*> pointTo);
	void prepareImage();
	void showGlare();

	const style::ImportantTooltip &_st;
	std::shared_ptr<Data::EmojiStatusCollectible> _collectible;
	QString _text;
	const style::font &_font;
	QSize _inner;
	QSize _outer;
	int _stroke = 0;
	int _skip = 0;
	QSize _full;
	int _glareSize = 0;
	int _glareRange = 0;
	crl::time _glareDuration = 0;
	base::Timer _glareTimer;

	Ui::Animations::Simple _showAnimation;
	Ui::Animations::Simple _glareAnimation;

	QImage _image;
	int _glareRight = 0;
	int _imageGlareRight = 0;
	int _arrowMiddle = 0;
	int _imageArrowMiddle = 0;

	bool _shown = false;
	float64 _opacity = 1.;
};

} // namespace Info::Profile
