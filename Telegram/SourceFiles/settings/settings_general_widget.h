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
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "settings/settings_block_widget.h"
#include "ui/filedialog.h"

namespace Settings {

#ifndef TDESKTOP_DISABLE_AUTOUPDATE
class UpdateStateRow : public TWidget {
	Q_OBJECT

public:
	UpdateStateRow(QWidget *parent);

	bool isUpdateReady() const {
		return (_state == State::Ready);
	}

protected:
	int resizeGetHeight(int newWidth) override;

	void paintEvent(QPaintEvent *e) override;

signals:
	void restart();

private slots:
	void onCheck();

	void onChecking();
	void onLatest();
	void onDownloading(qint64 ready, qint64 total);
	void onReady();
	void onFailed();

private:
	enum class State {
		None,
		Check,
		Latest,
		Download,
		Fail,
		Ready
	};
	void setState(State state, bool force = false);
	void setDownloadProgress(qint64 ready, qint64 total);

	object_ptr<Ui::LinkButton> _check;
	object_ptr<Ui::LinkButton> _restart;

	State _state = State::None;
	QString _downloadText;
	QString _versionText;

};
#endif // !TDESKTOP_DISABLE_AUTOUPDATE

class GeneralWidget : public BlockWidget {
	Q_OBJECT

public:
	GeneralWidget(QWidget *parent, UserData *self);

protected:
	int resizeGetHeight(int newWidth) override;

private slots:
	void onChangeLanguage();

#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	void onUpdateAutomatically();
#endif // !TDESKTOP_DISABLE_AUTOUPDATE

	void onEnableTrayIcon();
	void onEnableTaskbarIcon();

	void onAutoStart();
	void onStartMinimized();
	void onAddInSendTo();

	void onRestart();

private:
	void refreshControls();
	void updateWorkmode();
	void chooseCustomLang();
	void notifyFileQueryUpdated(const FileDialog::QueryUpdate &update);

	object_ptr<Ui::LinkButton> _changeLanguage;
#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	object_ptr<Ui::Checkbox> _updateAutomatically = { nullptr };
	object_ptr<Ui::WidgetSlideWrap<UpdateStateRow>> _updateRow = { nullptr };
#endif // !TDESKTOP_DISABLE_AUTOUPDATE
	object_ptr<Ui::Checkbox> _enableTrayIcon = { nullptr };
	object_ptr<Ui::Checkbox> _enableTaskbarIcon = { nullptr };
	object_ptr<Ui::Checkbox> _autoStart = { nullptr };
	object_ptr<Ui::WidgetSlideWrap<Ui::Checkbox>> _startMinimized = { nullptr };
	object_ptr<Ui::Checkbox> _addInSendTo = { nullptr };

	FileDialog::QueryId _chooseLangFileQueryId = 0;
	QString _testLanguage;

};

} // namespace Settings
