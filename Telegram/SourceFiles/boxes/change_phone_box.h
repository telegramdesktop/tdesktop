/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"
#include "settings/settings_common.h"

namespace Lottie {
class Icon;
} // namespace Lottie

namespace Window {
class SessionController;
} // namespace Window

namespace Settings {

class ChangePhone : public Section<ChangePhone> {
public:
	ChangePhone(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] static rpl::producer<QString> Title();

private:
	void setupContent(not_null<Window::SessionController*> controller);

};

} // namespace Settings

class ChangePhoneBox : public Ui::BoxContent {
public:
	ChangePhoneBox(
		QWidget*,
		not_null<Window::SessionController*> controller);

	void showFinished() override;

protected:
	void prepare() override;

	void paintEvent(QPaintEvent *e) override;

private:
	void animateIcon();

	class EnterPhone;
	class EnterCode;

	const not_null<Window::SessionController*> _controller;
	const std::unique_ptr<Lottie::Icon> _icon;

};

