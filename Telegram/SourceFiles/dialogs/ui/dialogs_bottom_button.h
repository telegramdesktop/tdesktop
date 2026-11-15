/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/widgets/buttons.h"

namespace style {
struct FlatButton;
} // namespace style

namespace Ui {
class InfiniteRadialAnimation;
} // namespace Ui

namespace Dialogs {

class BottomButton final : public Ui::RippleButton {
public:
	BottomButton(
		QWidget *parent,
		const QString &text,
		const style::FlatButton &st,
		const style::icon &icon,
		const style::icon &iconOver,
		bool hasTextIcon);

	void setText(const QString &text);

protected:
	void paintEvent(QPaintEvent *e) override;

	void onStateChanged(State was, StateChangeSource source) override;

private:
	void radialAnimationCallback();

	QString _text;
	const style::FlatButton &_st;
	const style::icon &_icon;
	const style::icon &_iconOver;
	const bool _hasTextIcon;
	bool _quiet = false;
	std::unique_ptr<Ui::InfiniteRadialAnimation> _loading;

	QImage _textIcon;

};

} // namespace Dialogs
