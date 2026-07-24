/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animations.h"
#include "ui/widgets/buttons.h"

namespace HistoryView::Controls {

class ComposeAiButton final : public Ui::RippleButton {
public:
	ComposeAiButton(QWidget *parent, const style::IconButton &st);
	ComposeAiButton(
		QWidget *parent,
		const style::IconButton &st,
		const style::icon &letters,
		const style::icon &star1,
		const style::icon &star2,
		const style::color *overColor = nullptr);

	void setPremiumStar(QImage image, QPoint position, int outline);

protected:
	void paintEvent(QPaintEvent *e) override;
	void onStateChanged(State was, StateChangeSource source) override;

	[[nodiscard]] QImage prepareRippleMask() const override;
	[[nodiscard]] QPoint prepareRippleStartPosition() const override;

private:
	void paintIcons(
		QPainter &p,
		bool over,
		float64 star1Opacity,
		float64 star2Opacity);
	void validateFrame(bool over, float64 star1Opacity, float64 star2Opacity);

	const style::IconButton &_st;
	const style::icon &_letters;
	const style::icon &_star1;
	const style::icon &_star2;
	const style::color *_overColor = nullptr;
	Ui::Animations::Simple _animation;
	QImage _premiumStar;
	QPoint _premiumStarPosition;
	int _premiumStarOutline = 0;
	QImage _frame;
	float64 _frameStar1 = -1.;
	float64 _frameStar2 = -1.;
	bool _frameOver = false;

};

} // namespace HistoryView::Controls
