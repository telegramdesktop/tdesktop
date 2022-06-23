/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "settings/settings_common.h"

namespace Window {
class Controller;
} // namespace Window

namespace Settings {

class Blocked : public Section<Blocked> {
public:
	Blocked(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	void showFinished() override;

	[[nodiscard]] rpl::producer<QString> title() override;

	[[nodiscard]] QPointer<Ui::RpWidget> createPinnedToTop(
		not_null<QWidget*> parent) override;

private:
	void setupContent();
	void checkTotal(int total);

	const not_null<Window::SessionController*> _controller;

	base::unique_qptr<Ui::RpWidget> _loading;

	rpl::variable<int> _countBlocked;

	rpl::event_stream<> _showFinished;
	rpl::event_stream<bool> _emptinessChanges;

};

} // namespace Settings
