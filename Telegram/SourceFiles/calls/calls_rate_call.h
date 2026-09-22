/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animations.h"
#include "ui/rp_widget.h"
#include "ui/text/text.h"
#include "ui/widgets/buttons.h"

namespace Lottie {
class Icon;
} // namespace Lottie

namespace Ui {
class RippleAnimation;
} // namespace Ui

namespace Calls {

class RateCall final : public Ui::RpWidget {
public:
	explicit RateCall(QWidget *parent);
	~RateCall();

	void showAnimated();
	void setTextColorOverride(std::optional<QColor> color);
	void setCardOverlay(QColor color);

	[[nodiscard]] int rating() const;
	[[nodiscard]] rpl::producer<int> ratingValue() const;

	[[nodiscard]] static int Height();

private:
	static constexpr auto kStarsCount = 5;

	struct Burst {
		std::unique_ptr<Lottie::Icon> icon;
		QPoint center;
		crl::time started = 0;
	};

	struct Star {
		std::unique_ptr<Ui::RippleAnimation> ripple;
		Ui::Animations::Simple filled;
		Ui::Animations::Simple pressed;
		float64 filledTo = 0.;
		float64 pressedTo = 1.;
	};

	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;

	void paintCard(QPainter &p, float64 shown);
	void paintStar(QPainter &p, int index, float64 shown);
	void paintBurst(QPainter &p);

	void setOver(int index);
	void applyPreview(int preview);
	void commit(int index);
	void startBurst(int index);

	[[nodiscard]] QRect cardRect() const;
	[[nodiscard]] QRect starRect(int index) const;
	[[nodiscard]] int starByPosition(QPoint position) const;
	[[nodiscard]] QColor textColor() const;
	[[nodiscard]] float64 cardShown() const;
	[[nodiscard]] float64 starShown(int index) const;
	[[nodiscard]] const QImage &starImage(bool filled) const;

	Ui::Text::String _title;
	Ui::Text::String _description;
	std::array<Star, kStarsCount> _stars;
	std::vector<Burst> _bursts;
	Ui::Animations::Simple _showAnimation;
	std::optional<QColor> _textColorOverride;
	QColor _cardOverlay;
	mutable QImage _starOutline;
	mutable QImage _starFilled;
	mutable QColor _starColor;
	rpl::variable<int> _rating = 0;
	int _preview = 0;
	int _over = -1;
	int _pressed = -1;

};

class EndCloseButton final : public Ui::RippleButton {
public:
	explicit EndCloseButton(QWidget *parent);

	void switchToClose(QRect hangupCircle, Fn<void()> close);

private:
	void paintEvent(QPaintEvent *e) override;

	QImage prepareRippleMask() const override;

	void prepareFrame();

	const QString _text;
	QRect _from;
	Ui::Animations::Simple _animation;
	QImage _frame;

};

} // namespace Calls
