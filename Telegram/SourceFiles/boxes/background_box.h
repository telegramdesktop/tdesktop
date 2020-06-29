/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"

namespace Window {
class SessionController;
} // namespace Window

namespace Data {
class WallPaper;
} // namespace Data

class BackgroundBox : public Ui::BoxContent {
public:
	BackgroundBox(QWidget*, not_null<Window::SessionController*> controller);

protected:
	void prepare() override;

private:
	class Inner;

	void removePaper(const Data::WallPaper &paper);

	const not_null<Window::SessionController*> _controller;

	QPointer<Inner> _inner;

};
