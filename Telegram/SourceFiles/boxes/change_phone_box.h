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

	void showFinished() override;
	[[nodiscard]] rpl::producer<QString> title() override;

private:
	class EnterPhone;
	class EnterCode;

	void setupContent();

	const not_null<Window::SessionController*> _controller;
	Fn<void(anim::repeat)> _animate;

};

} // namespace Settings
