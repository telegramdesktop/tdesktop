/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"
#include "mtproto/sender.h"
#include "core/core_cloud_password.h"

namespace Ui {
class InputField;
class PasswordInput;
class LinkButton;
} // namespace Ui

class PasscodeBox : public BoxContent, private MTP::Sender {
public:
	PasscodeBox(QWidget*, bool turningOff);
	PasscodeBox(
		QWidget*,
		const Core::CloudPasswordCheckRequest &curRequest,
		const Core::CloudPasswordAlgo &newAlgo,
		bool hasRecovery,
		bool notEmptyPassport,
		const QString &hint,
		const Core::SecureSecretAlgo &newSecureSecretAlgo,
		bool turningOff = false);

	rpl::producer<QByteArray> newPasswordSet() const;
	rpl::producer<> passwordReloadNeeded() const;

protected:
	void prepare() override;
	void setInnerFocus() override;

	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	using CheckPasswordCallback = Fn<void(
		const Core::CloudPasswordResult &check)>;

	void submit();
	void closeReplacedBy();
	void oldChanged();
	void newChanged();
	void emailChanged();
	void save(bool force = false);
	void badOldPasscode();
	void recoverByEmail();
	void recoverExpired();
	bool currentlyHave() const;

	void setPasswordDone(const QByteArray &newPasswordBytes);
	void setPasswordFail(const RPCError &error);
	void setPasswordFail(
		const QByteArray &newPasswordBytes,
		const RPCError &error);

	void recoverStarted(const MTPauth_PasswordRecovery &result);
	void recoverStartFail(const RPCError &error);

	void recover();
	void clearCloudPassword(const QString &oldPassword);
	void setNewCloudPassword(const QString &newPassword);

	void checkPassword(
		const QString &oldPassword,
		CheckPasswordCallback callback);
	void checkPasswordHash(CheckPasswordCallback callback);

	void changeCloudPassword(
		const QString &oldPassword,
		const QString &newPassword);
	void changeCloudPassword(
		const QString &oldPassword,
		const Core::CloudPasswordResult &check,
		const QString &newPassword);

	void sendChangeCloudPassword(
		const Core::CloudPasswordResult &check,
		const QString &newPassword,
		const QByteArray &secureSecret);
	void suggestSecretReset(const QString &newPassword);
	void resetSecret(
		const Core::CloudPasswordResult &check,
		const QString &newPassword,
		Fn<void()> callback);
	void resetSecretAndChangePassword(
		const bytes::vector &oldPasswordHash,
		const QString &newPassword);

	void sendClearCloudPassword(const QString &oldPassword);
	void sendClearCloudPassword(const Core::CloudPasswordResult &check);

	void handleSrpIdInvalid();
	void requestPasswordData();
	void passwordChecked();
	void serverError();

	QString _pattern;

	QPointer<BoxContent> _replacedBy;
	bool _turningOff = false;
	bool _cloudPwd = false;
	mtpRequestId _setRequest = 0;

	Core::CloudPasswordCheckRequest _curRequest;
	TimeMs _lastSrpIdInvalidTime = 0;
	Core::CloudPasswordAlgo _newAlgo;
	Core::SecureSecretAlgo _newSecureSecretAlgo;
	bool _hasRecovery = false;
	bool _notEmptyPassport = false;
	bool _skipEmailWarning = false;
	CheckPasswordCallback _checkPasswordCallback;
	bytes::vector _checkPasswordHash;

	int _aboutHeight = 0;

	Text _about, _hintText;

	object_ptr<Ui::PasswordInput> _oldPasscode;
	object_ptr<Ui::PasswordInput> _newPasscode;
	object_ptr<Ui::PasswordInput> _reenterPasscode;
	object_ptr<Ui::InputField> _passwordHint;
	object_ptr<Ui::InputField> _recoverEmail;
	object_ptr<Ui::LinkButton> _recover;

	QString _oldError, _newError, _emailError;

	rpl::event_stream<QByteArray> _newPasswordSet;
	rpl::event_stream<> _passwordReloadNeeded;

};

class RecoverBox : public BoxContent, public RPCSender {
public:
	RecoverBox(QWidget*, const QString &pattern, bool notEmptyPassport);

	rpl::producer<> passwordCleared() const;
	rpl::producer<> recoveryExpired() const;

	//void reloadPassword();
	//void recoveryExpired();

protected:
	void prepare() override;
	void setInnerFocus() override;

	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	void submit();
	void codeChanged();
	void codeSubmitDone(bool recover, const MTPauth_Authorization &result);
	bool codeSubmitFail(const RPCError &error);

	mtpRequestId _submitRequest = 0;

	QString _pattern;
	bool _notEmptyPassport = false;

	object_ptr<Ui::InputField> _recoverCode;

	QString _error;

	rpl::event_stream<> _passwordCleared;
	rpl::event_stream<> _recoveryExpired;

};
