/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "boxes/abstractbox.h"

namespace Ui {
class InputField;
class PortInput;
class PasswordInput;
class Checkbox;
class Radiobutton;
class RoundButton;
} // namespace Ui

class ConnectionBox : public AbstractBox {
	Q_OBJECT

public:
	ConnectionBox();

public slots:
	void onChange();
	void onSubmit();
	void onSave();

protected:
	void resizeEvent(QResizeEvent *e) override;

	void doSetInnerFocus() override;

private:
	void updateControlsVisibility();

	ChildWidget<Ui::InputField> _hostInput;
	ChildWidget<Ui::PortInput> _portInput;
	ChildWidget<Ui::InputField> _userInput;
	ChildWidget<Ui::PasswordInput> _passwordInput;
	ChildWidget<Ui::Radiobutton> _autoRadio;
	ChildWidget<Ui::Radiobutton> _httpProxyRadio;
	ChildWidget<Ui::Radiobutton> _tcpProxyRadio;
	ChildWidget<Ui::Checkbox> _tryIPv6;

	ChildWidget<Ui::RoundButton> _save;
	ChildWidget<Ui::RoundButton> _cancel;

};

class AutoDownloadBox : public AbstractBox {
	Q_OBJECT

public:
	AutoDownloadBox();

public slots:
	void onSave();

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	ChildWidget<Ui::Checkbox> _photoPrivate;
	ChildWidget<Ui::Checkbox> _photoGroups;
	ChildWidget<Ui::Checkbox> _audioPrivate;
	ChildWidget<Ui::Checkbox> _audioGroups;
	ChildWidget<Ui::Checkbox> _gifPrivate;
	ChildWidget<Ui::Checkbox> _gifGroups;
	ChildWidget<Ui::Checkbox> _gifPlay;

	int _sectionHeight;

	ChildWidget<Ui::RoundButton> _save;
	ChildWidget<Ui::RoundButton> _cancel;

};
