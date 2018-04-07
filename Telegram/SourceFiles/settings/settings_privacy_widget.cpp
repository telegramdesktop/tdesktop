/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_privacy_widget.h"

#include "ui/wrap/slide_wrap.h"
#include "ui/widgets/buttons.h"
#include "styles/style_settings.h"
#include "lang/lang_keys.h"
#include "application.h"
#include "platform/platform_specific.h"
#include "boxes/sessions_box.h"
#include "boxes/passcode_box.h"
#include "boxes/autolock_box.h"
#include "boxes/peer_list_box.h"
#include "boxes/edit_privacy_box.h"
#include "boxes/self_destruction_box.h"
#include "settings/settings_privacy_controllers.h"

namespace Settings {

LocalPasscodeState::LocalPasscodeState(QWidget *parent) : RpWidget(parent)
, _edit(this, GetEditPasscodeText(), st::boxLinkButton)
, _turnOff(this, lang(lng_passcode_turn_off), st::boxLinkButton) {
	updateControls();
	connect(_edit, SIGNAL(clicked()), this, SLOT(onEdit()));
	connect(_turnOff, SIGNAL(clicked()), this, SLOT(onTurnOff()));
	subscribe(Global::RefLocalPasscodeChanged(), [this]() { updateControls(); });
}

int LocalPasscodeState::resizeGetHeight(int newWidth) {
	_edit->moveToLeft(0, 0, newWidth);
	_turnOff->moveToRight(0, 0, newWidth);
	return _edit->height();
}

void LocalPasscodeState::onEdit() {
	Ui::show(Box<PasscodeBox>(false));
}

void LocalPasscodeState::onTurnOff() {
	Ui::show(Box<PasscodeBox>(true));
}

void LocalPasscodeState::updateControls() {
	_edit->setText(GetEditPasscodeText());
	_edit->moveToLeft(0, 0);
	_turnOff->setVisible(Global::LocalPasscode());
}

QString LocalPasscodeState::GetEditPasscodeText() {
	return lang(Global::LocalPasscode() ? lng_passcode_change : lng_passcode_turn_on);
}

CloudPasswordState::CloudPasswordState(QWidget *parent) : RpWidget(parent)
, _edit(this, lang(lng_cloud_password_set), st::boxLinkButton)
, _turnOff(this, lang(lng_passcode_turn_off), st::boxLinkButton) {
	_turnOff->hide();
	connect(_edit, SIGNAL(clicked()), this, SLOT(onEdit()));
	connect(_turnOff, SIGNAL(clicked()), this, SLOT(onTurnOff()));
	Sandbox::connect(SIGNAL(applicationStateChanged(Qt::ApplicationState)), this, SLOT(onReloadPassword(Qt::ApplicationState)));
	onReloadPassword();
}

int CloudPasswordState::resizeGetHeight(int newWidth) {
	_edit->moveToLeft(0, 0, newWidth);
	_turnOff->moveToRight(0, 0, newWidth);
	return _edit->height();
}

void CloudPasswordState::onEdit() {
	auto box = Ui::show(Box<PasscodeBox>(_newPasswordSalt, _curPasswordSalt, _hasPasswordRecovery, _curPasswordHint));
	connect(box, SIGNAL(reloadPassword()), this, SLOT(onReloadPassword()));
}

void CloudPasswordState::onTurnOff() {
	if (_curPasswordSalt.isEmpty()) {
		_turnOff->hide();

		auto flags = MTPDaccount_passwordInputSettings::Flag::f_email;
		MTPaccount_PasswordInputSettings settings(MTP_account_passwordInputSettings(MTP_flags(flags), MTP_bytes(QByteArray()), MTP_bytes(QByteArray()), MTP_string(QString()), MTP_string(QString())));
		MTP::send(MTPaccount_UpdatePasswordSettings(MTP_bytes(QByteArray()), settings), rpcDone(&CloudPasswordState::offPasswordDone), rpcFail(&CloudPasswordState::offPasswordFail));
	} else {
		auto box = Ui::show(Box<PasscodeBox>(_newPasswordSalt, _curPasswordSalt, _hasPasswordRecovery, _curPasswordHint, true));
		connect(box, SIGNAL(reloadPassword()), this, SLOT(onReloadPassword()));
	}
}

void CloudPasswordState::onReloadPassword(Qt::ApplicationState state) {
	if (state == Qt::ApplicationActive) {
		MTP::send(MTPaccount_GetPassword(), rpcDone(&CloudPasswordState::getPasswordDone));
	}
}

void CloudPasswordState::getPasswordDone(const MTPaccount_Password &result) {
	_waitingConfirm = QString();

	switch (result.type()) {
	case mtpc_account_noPassword: {
		auto &d = result.c_account_noPassword();
		_curPasswordSalt = QByteArray();
		_hasPasswordRecovery = false;
		_curPasswordHint = QString();
		_newPasswordSalt = qba(d.vnew_salt);
		auto pattern = qs(d.vemail_unconfirmed_pattern);
		if (!pattern.isEmpty()) {
			_waitingConfirm = lng_cloud_password_waiting(lt_email, pattern);
		}
	} break;

	case mtpc_account_password: {
		auto &d = result.c_account_password();
		_curPasswordSalt = qba(d.vcurrent_salt);
		_hasPasswordRecovery = mtpIsTrue(d.vhas_recovery);
		_curPasswordHint = qs(d.vhint);
		_newPasswordSalt = qba(d.vnew_salt);
		auto pattern = qs(d.vemail_unconfirmed_pattern);
		if (!pattern.isEmpty()) {
			_waitingConfirm = lng_cloud_password_waiting(lt_email, pattern);
		}
	} break;
	}
	_edit->setText(lang(_curPasswordSalt.isEmpty() ? lng_cloud_password_set : lng_cloud_password_edit));
	_edit->setVisible(_waitingConfirm.isEmpty());
	_turnOff->setVisible(!_waitingConfirm.isEmpty() || !_curPasswordSalt.isEmpty());
	update();

	_newPasswordSalt.resize(_newPasswordSalt.size() + 8);
	memset_rand(_newPasswordSalt.data() + _newPasswordSalt.size() - 8, 8);
}

void CloudPasswordState::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto text = st::boxTextFont->elided(_waitingConfirm, width() - _turnOff->width() - st::boxTextFont->spacew);
	if (!text.isEmpty()) {
		p.setPen(st::windowFg);
		p.setFont(st::boxTextFont);
		p.drawTextLeft(0, 0, width(), text);
	}
}

void CloudPasswordState::offPasswordDone(const MTPBool &result) {
	onReloadPassword();
}

bool CloudPasswordState::offPasswordFail(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	onReloadPassword();
	return true;
}

PrivacyWidget::PrivacyWidget(QWidget *parent, UserData *self) : BlockWidget(parent, self, lang(lng_settings_section_privacy)) {
	createControls();
	subscribe(Global::RefLocalPasscodeChanged(), [this]() { autoLockUpdated(); });
}

QString PrivacyWidget::GetAutoLockText() {
	return (Global::AutoLock() % 3600) ? lng_passcode_autolock_minutes(lt_count, Global::AutoLock() / 60) : lng_passcode_autolock_hours(lt_count, Global::AutoLock() / 3600);
}

void PrivacyWidget::createControls() {
	style::margins marginSmall(0, 0, 0, st::settingsSmallSkip);
	style::margins marginSkip(0, 0, 0, st::settingsSkip);
	style::margins slidedPadding(0, marginSmall.bottom() / 2, 0, marginSmall.bottom() - (marginSmall.bottom() / 2));

	createChildRow(_blockedUsers, marginSmall, lang(lng_settings_blocked_users), SLOT(onBlockedUsers()));
	createChildRow(_lastSeenPrivacy, marginSmall, lang(lng_settings_last_seen_privacy), SLOT(onLastSeenPrivacy()));
	createChildRow(_callsPrivacy, marginSmall, lang(lng_settings_calls_privacy), SLOT(onCallsPrivacy()));
	createChildRow(_groupsInvitePrivacy, marginSmall, lang(lng_settings_groups_invite_privacy), SLOT(onGroupsInvitePrivacy()));
	createChildRow(_localPasscodeState, marginSmall);
	auto label = lang(psIdleSupported() ? lng_passcode_autolock_away : lng_passcode_autolock_inactive);
	auto value = GetAutoLockText();
	createChildRow(_autoLock, marginSmall, slidedPadding, label, value, LabeledLink::Type::Primary, SLOT(onAutoLock()));
	if (!Global::LocalPasscode()) {
		_autoLock->hide(anim::type::instant);
	}
	createChildRow(_cloudPasswordState, marginSmall);
	createChildRow(_showAllSessions, marginSmall, lang(lng_settings_show_sessions), SLOT(onShowSessions()));
	createChildRow(_selfDestruction, marginSmall, lang(lng_settings_self_destruct), SLOT(onSelfDestruction()));
}

void PrivacyWidget::autoLockUpdated() {
	if (Global::LocalPasscode()) {
		auto value = GetAutoLockText();
		_autoLock->entity()->link()->setText(value);
		resizeToWidth(width());
	}
	_autoLock->toggle(
		Global::LocalPasscode(),
		anim::type::normal);
}

void PrivacyWidget::onBlockedUsers() {
	Ui::show(Box<PeerListBox>(std::make_unique<BlockedBoxController>(), [](not_null<PeerListBox*> box) {
		box->addButton(langFactory(lng_close), [box] { box->closeBox(); });
		box->addLeftButton(langFactory(lng_blocked_list_add), [=] { BlockedBoxController::BlockNewUser(); });
	}));
}

void PrivacyWidget::onLastSeenPrivacy() {
	Ui::show(Box<EditPrivacyBox>(std::make_unique<LastSeenPrivacyController>()));
}

void PrivacyWidget::onCallsPrivacy() {
	Ui::show(Box<EditPrivacyBox>(std::make_unique<CallsPrivacyController>()));
}

void PrivacyWidget::onGroupsInvitePrivacy() {
	Ui::show(Box<EditPrivacyBox>(std::make_unique<GroupsInvitePrivacyController>()));
}

void PrivacyWidget::onAutoLock() {
	Ui::show(Box<AutoLockBox>());
}

void PrivacyWidget::onShowSessions() {
	Ui::show(Box<SessionsBox>());
}

void PrivacyWidget::onSelfDestruction() {
	Ui::show(Box<SelfDestructionBox>());
}

} // namespace Settings
