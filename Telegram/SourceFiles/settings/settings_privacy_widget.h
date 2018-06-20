/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "settings/settings_block_widget.h"
#include "settings/settings_chat_settings_widget.h"
#include "ui/rp_widget.h"

namespace Settings {

class LocalPasscodeState : public Ui::RpWidget, private base::Subscriber {
	Q_OBJECT

public:
	LocalPasscodeState(QWidget *parent);

protected:
	int resizeGetHeight(int newWidth) override;

private slots:
	void onEdit();
	void onTurnOff();

private:
	static QString GetEditPasscodeText();

	void updateControls();

	object_ptr<Ui::LinkButton> _edit;
	object_ptr<Ui::LinkButton> _turnOff;

};

class CloudPasswordState : public Ui::RpWidget, private base::Subscriber, public RPCSender {
	Q_OBJECT

public:
	CloudPasswordState(QWidget *parent);

protected:
	int resizeGetHeight(int newWidth) override;

	void paintEvent(QPaintEvent *e) override;

private slots:
	void onEdit();
	void onTurnOff();
	void onReloadPassword();
	void onReloadPassword(Qt::ApplicationState state);

private:
	void getPasswordDone(const MTPaccount_Password &result);
	bool getPasswordFail(const RPCError &error);
	void offPasswordDone(const MTPBool &result);
	bool offPasswordFail(const RPCError &error);

	object_ptr<Ui::LinkButton> _edit;
	object_ptr<Ui::LinkButton> _turnOff;

	QString _waitingConfirm;
	QByteArray _curPasswordSalt;
	bool _hasPasswordRecovery = false;
	bool _notEmptyPassport = false;
	QString _curPasswordHint;
	QByteArray _newPasswordSalt;
	QByteArray _newSecureSecretSalt;
	mtpRequestId _reloadRequestId = 0;

};

class PrivacyWidget : public BlockWidget {
	Q_OBJECT

public:
	PrivacyWidget(QWidget *parent, UserData *self);

private slots:
	void onBlockedUsers();
	void onLastSeenPrivacy();
	void onCallsPrivacy();
	void onGroupsInvitePrivacy();
	void onAutoLock();
	void onShowSessions();
	void onSelfDestruction();
	void onExportData();

private:
	static QString GetAutoLockText();

	void createControls();
	void autoLockUpdated();

	Ui::LinkButton *_blockedUsers = nullptr;
	Ui::LinkButton *_lastSeenPrivacy = nullptr;
	Ui::LinkButton *_callsPrivacy = nullptr;
	Ui::LinkButton *_groupsInvitePrivacy = nullptr;
	LocalPasscodeState *_localPasscodeState = nullptr;
	Ui::SlideWrap<LabeledLink> *_autoLock = nullptr;
	CloudPasswordState *_cloudPasswordState = nullptr;
	Ui::LinkButton *_showAllSessions = nullptr;
	Ui::LinkButton *_selfDestruction = nullptr;
	Ui::LinkButton *_exportData = nullptr;

};

} // namespace Settings
