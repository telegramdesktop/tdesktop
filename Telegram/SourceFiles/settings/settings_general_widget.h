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

#include "settings/settings_block_widget.h"
#include "ui/filedialog.h"

class Checkbox;
class LinkButton;

namespace Ui {
template <typename Widget>
class WidgetSlideWrap;
} // namespace Ui

namespace Settings {

class GeneralWidget : public BlockWidget {
	Q_OBJECT

public:
	GeneralWidget(QWidget *parent, UserData *self);

protected:
	int resizeGetHeight(int newWidth) override;

private slots:
	void onChangeLanguage();
	void onSaveTestLanguage();

#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	void onUpdateAutomatically();
	void onCheckForUpdates();
#endif // TDESKTOP_DISABLE_AUTOUPDATE

	void onEnableTrayIcon();
	void onEnableTaskbarIcon();

	void onAutoStart();
	void onStartMinimized();
	void onAddInSendTo();

private:
	void refreshControls();
	void updateWorkmode();
	void chooseCustomLang();
	void notifyFileQueryUpdated(const FileDialog::QueryUpdate &update);

	ChildWidget<LinkButton> _changeLanguage;
#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	ChildWidget<Checkbox> _updateAutomatically = { nullptr };
	ChildWidget<LinkButton> _checkForUpdates = { nullptr };
#endif // TDESKTOP_DISABLE_AUTOUPDATE
	ChildWidget<Checkbox> _enableTrayIcon = { nullptr };
	ChildWidget<Checkbox> _enableTaskbarIcon = { nullptr };
	ChildWidget<Checkbox> _autoStart = { nullptr };
	ChildWidget<Ui::WidgetSlideWrap<Checkbox>> _startMinimized = { nullptr };
	ChildWidget<Checkbox> _addInSendTo = { nullptr };

	FileDialog::QueryId _chooseLangFileQueryId = 0;
	QString _testLanguage;

};

} // namespace Settings
