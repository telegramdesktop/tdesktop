/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"

class UserData;

namespace Window {
class Controller;
} // namespace Window

namespace Ui {
class VerticalLayout;
} // namespace Ui

class AddToContactsBox : public BoxContent {
public:
	AddToContactsBox(
		QWidget*,
		not_null<Window::Controller*> window,
		not_null<UserData*> user);

protected:
	void prepare() override;

	void setInnerFocus() override;

private:
	void setupContent();
	void setupCover(not_null<Ui::VerticalLayout*> container);
	void setupNameFields(not_null<Ui::VerticalLayout*> container);
	void setupWarning(not_null<Ui::VerticalLayout*> container);
	void initNameFields(
		not_null<Ui::InputField*> first,
		not_null<Ui::InputField*> last,
		bool inverted);

	not_null<Window::Controller*> _window;
	not_null<UserData*> _user;
	QString _phone;
	Fn<void()> _focus;
	Fn<void()> _save;

};
