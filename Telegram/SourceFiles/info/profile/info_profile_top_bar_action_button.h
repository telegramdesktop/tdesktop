/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/widgets/buttons.h"

namespace Lottie {
class Icon;
} // namespace Lottie

namespace Info::Profile {

struct TopBarActionButtonStyle {
	QColor bgColor;
	std::optional<QColor> fgColor;
	std::optional<QColor> shadowColor;
};

class TopBarActionButton final : public Ui::RippleButton {
public:
	TopBarActionButton(
		not_null<QWidget*> parent,
		const QString &text,
		const QString &lottieName);
	TopBarActionButton(
		not_null<QWidget*> parent,
		const QString &text,
		const style::icon &icon);

	void convertToToggle(
		const style::icon &offIcon,
		const style::icon &onIcon,
		const QString &offLottie,
		const QString &onLottie);
	void setLottieColor(const style::color *color);
	void toggle(bool state);
	void finishAnimating();
	void setText(const QString &text);
	void setStyle(const TopBarActionButtonStyle &style);

	~TopBarActionButton();

protected:
	void paintEvent(QPaintEvent *e) override;

	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

private:
	void setupLottie(const QString &lottieName);

	QString _text;
	std::unique_ptr<Lottie::Icon> _lottie;
	const style::icon *_icon = nullptr;

	bool _isToggle = false;
	bool _toggleState = false;
	const style::icon *_offIcon = nullptr;
	const style::icon *_onIcon = nullptr;
	QString _offLottie;
	QString _onLottie;
	const style::color *_lottieColor = nullptr;

	QColor _bgColor;
	std::optional<QColor> _fgColor;
	std::optional<QColor> _shadowColor;

};

} // namespace Info::Profile
