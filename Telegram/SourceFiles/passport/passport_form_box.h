/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"

namespace Passport {

class ViewSeparate;

class FormBox : public BoxContent {
public:
	FormBox(QWidget*, not_null<ViewSeparate*> controller);

protected:
	void prepare() override;
	void setInnerFocus() override;

private:
	class CheckWidget;
	class Inner;

	void submitPassword();
	void showForm();
	void submitForm();

	not_null<ViewSeparate*> _controller;
	object_ptr<Inner> _innerCached = { nullptr };
	QPointer<CheckWidget> _passwordCheck;
	QPointer<Inner> _inner;

};

} // namespace Passport
