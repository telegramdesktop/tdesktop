/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class Radiobutton;
} // namespace Ui

class AutoLockBox : public BoxContent {
public:
	AutoLockBox(QWidget*, not_null<Main::Session*> session);

protected:
	void prepare() override;

private:
	void durationChanged(int seconds);

	const not_null<Main::Session*> _session;

	std::vector<object_ptr<Ui::Radiobutton>> _options;

};
