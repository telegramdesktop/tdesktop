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

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class InputField;
class PasswordInput;
class LinkButton;
} // namespace Ui

namespace Core {
struct CloudPasswordState;
} // namespace Core

class PasscodeBox : public Ui::BoxContent {
public:
	PasscodeBox(QWidget*, not_null<Main::Session*> session, bool turningOff);

	struct CloudFields {
		static CloudFields From(const Core::CloudPasswordState &current);

		Core::CloudPasswordCheckRequest curRequest;
		Core::CloudPasswordAlgo newAlgo;
		bool hasRecovery = false;
		bool notEmptyPassport = false;
		QString hint;
		Core::SecureSecretAlgo newSecureSecretAlgo;
		bool turningOff = false;

		// Check cloud password for some action.
		Fn<void(const Core::CloudPasswordResult &)> customCheckCallback;
		rpl::producer<QString> customTitle;
		std::optional<QString> customDescription;
		rpl::producer<QString> customSubmitButton;
	};
	PasscodeBox(
		QWidget*,
		not_null<Main::Session*> session,
		const CloudFields &fields);

	rpl::producer<QByteArray> newPasswordSet() const;
	rpl::producer<> passwordReloadNeeded() const;
	rpl::producer<> clearUnconfirmedPassword() const;

	bool handleCustomCheckError(const MTP::Error &error);

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
	bool onlyCheckCurrent() const;

	void setPasswordDone(const QByteArray &newPasswordBytes);
	void setPasswordFail(const MTP::Error &error);
	void setPasswordFail(
		const QByteArray &newPasswordBytes,
		const QString &email,
		const MTP::Error &error);
	void validateEmail(
		const QString &email,
		int codeLength,
		const QByteArray &newPasswordBytes);

	void recoverStarted(const MTPauth_PasswordRecovery &result);
	void recoverStartFail(const MTP::Error &error);

	void recover();
	void submitOnlyCheckCloudPassword(const QString &oldPassword);
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

	void sendOnlyCheckCloudPassword(const QString &oldPassword);
	void sendClearCloudPassword(const Core::CloudPasswordResult &check);

	void handleSrpIdInvalid();
	void requestPasswordData();
	void passwordChecked();
	void serverError();

	const not_null<Main::Session*> _session;
	MTP::Sender _api;

	QString _pattern;

	QPointer<Ui::BoxContent> _replacedBy;
	bool _turningOff = false;
	bool _cloudPwd = false;
	CloudFields _cloudFields;
	mtpRequestId _setRequest = 0;

	crl::time _lastSrpIdInvalidTime = 0;
	bool _skipEmailWarning = false;
	CheckPasswordCallback _checkPasswordCallback;
	bytes::vector _checkPasswordHash;

	int _aboutHeight = 0;

	Ui::Text::String _about, _hintText;

	object_ptr<Ui::PasswordInput> _oldPasscode;
	object_ptr<Ui::PasswordInput> _newPasscode;
	object_ptr<Ui::PasswordInput> _reenterPasscode;
	object_ptr<Ui::InputField> _passwordHint;
	object_ptr<Ui::InputField> _recoverEmail;
	object_ptr<Ui::LinkButton> _recover;

	QString _oldError, _newError, _emailError;

	rpl::event_stream<QByteArray> _newPasswordSet;
	rpl::event_stream<> _passwordReloadNeeded;
	rpl::event_stream<> _clearUnconfirmedPassword;

};

class RecoverBox final : public Ui::BoxContent {
public:
	RecoverBox(
		QWidget*,
		not_null<Main::Session*> session,
		const QString &pattern,
		bool notEmptyPassport);

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
	void codeSubmitDone(const MTPauth_Authorization &result);
	void codeSubmitFail(const MTP::Error &error);

	MTP::Sender _api;
	mtpRequestId _submitRequest = 0;

	QString _pattern;
	bool _notEmptyPassport = false;

	object_ptr<Ui::InputField> _recoverCode;

	QString _error;

	rpl::event_stream<> _passwordCleared;
	rpl::event_stream<> _recoveryExpired;

};

struct RecoveryEmailValidation {
	object_ptr<Ui::BoxContent> box;
	rpl::producer<> reloadRequests;
	rpl::producer<> cancelRequests;
};
[[nodiscard]] RecoveryEmailValidation ConfirmRecoveryEmail(
	not_null<Main::Session*> session,
	const QString &pattern);

[[nodiscard]] object_ptr<Ui::GenericBox> PrePasswordErrorBox(
	const MTP::Error &error,
	not_null<Main::Session*> session,
	TextWithEntities &&about);
