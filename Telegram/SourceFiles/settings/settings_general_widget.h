/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "settings/settings_block_widget.h"
#include "ui/rp_widget.h"

namespace Settings {

#ifndef TDESKTOP_DISABLE_AUTOUPDATE
class UpdateStateRow : public Ui::RpWidget {
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

	int getUpdateTop() const;

protected:
	int resizeGetHeight(int newWidth) override;

private slots:
	void onChangeLanguage();

#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	void onUpdateAutomatically();
#endif // !TDESKTOP_DISABLE_AUTOUPDATE

	void onEnableTrayIcon();
	void onEnableTaskbarIcon();

#ifndef OS_WIN_STORE
	void onAutoStart();
	void onStartMinimized();
	void onAddInSendTo();
#endif // !OS_WIN_STORE

	void onRestart();

private:
	void refreshControls();
	void updateWorkmode();

	object_ptr<Ui::LinkButton> _changeLanguage;
#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	Ui::Checkbox *_updateAutomatically = nullptr;
	Ui::SlideWrap<UpdateStateRow> *_updateRow = nullptr;
#endif // !TDESKTOP_DISABLE_AUTOUPDATE
	Ui::Checkbox *_enableTrayIcon = nullptr;
	Ui::Checkbox *_enableTaskbarIcon = nullptr;
	Ui::Checkbox *_autoStart = nullptr;
	Ui::SlideWrap<Ui::Checkbox> *_startMinimized = nullptr;
	Ui::Checkbox *_addInSendTo = nullptr;

	int _languagesLoadedSubscription = 0;

};

} // namespace Settings
