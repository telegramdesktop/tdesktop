/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/layers/box_content.h"
#include "mtproto/sender.h"
#include "core/core_cloud_password.h"

namespace MTP {
class Instance;
} // namespace MTP

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

		struct Mtp {
			Core::CloudPasswordCheckRequest curRequest;
			Core::CloudPasswordAlgo newAlgo;
			Core::SecureSecretAlgo newSecureSecretAlgo;
		};
		Mtp mtp;
		bool hasPassword = false;
		bool hasRecovery = false;
		QString fromRecoveryCode;
		bool notEmptyPassport = false;
		QString hint;
		bool turningOff = false;
		TimeId pendingResetDate = 0;

		// Check cloud password for some action.
		using CustomCheck = Fn<void(
			const Core::CloudPasswordResult &,
			QPointer<PasscodeBox>)>;
		CustomCheck customCheckCallback;
		rpl::producer<QString> customTitle;
		std::optional<QString> customDescription;
		rpl::producer<QString> customSubmitButton;
	};
	PasscodeBox(
		QWidget*,
		not_null<MTP::Instance*> mtp,
		Main::Session *session,
		const CloudFields &fields);
	PasscodeBox(
		QWidget*,
		not_null<Main::Session*> session,
		const CloudFields &fields);

	rpl::producer<QByteArray> newPasswordSet() const;
	rpl::producer<> passwordReloadNeeded() const;
	rpl::producer<> clearUnconfirmedPassword() const;

	rpl::producer<MTPauth_Authorization> newAuthorization() const;

	bool handleCustomCheckError(const MTP::Error &error);
	bool handleCustomCheckError(const QString &type);

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
	void save(bool force = false);
	void badOldPasscode();
	void recoverByEmail();
	void recoverExpired();
	bool currentlyHave() const;
	bool onlyCheckCurrent() const;

	void setPasswordDone(const QByteArray &newPasswordBytes);
	void recoverPasswordDone(
		const QByteArray &newPasswordBytes,
		const MTPauth_Authorization &result);
	void setPasswordFail(const QString &type);
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

	Main::Session *_session = nullptr;
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
	bool _showRecoverLink = false;

	QString _oldError, _newError, _emailError;

	rpl::event_stream<QByteArray> _newPasswordSet;
	rpl::event_stream<MTPauth_Authorization> _newAuthorization;
	rpl::event_stream<> _passwordReloadNeeded;
	rpl::event_stream<> _clearUnconfirmedPassword;

};

class RecoverBox final : public Ui::BoxContent {
public:
	RecoverBox(
		QWidget*,
		not_null<MTP::Instance*> mtp,
		Main::Session *session,
		const QString &pattern,
		const PasscodeBox::CloudFields &fields,
		Fn<void()> closeParent = nullptr);

	[[nodiscard]] rpl::producer<QByteArray> newPasswordSet() const;
	[[nodiscard]] rpl::producer<> recoveryExpired() const;

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
	void proceedToClear();
	void proceedToChange(const QString &code);
	void checkSubmitFail(const MTP::Error &error);
	void setError(const QString &error);

	Main::Session *_session = nullptr;
	MTP::Sender _api;
	mtpRequestId _submitRequest = 0;

	QString _pattern;

	PasscodeBox::CloudFields _cloudFields;

	object_ptr<Ui::InputField> _recoverCode;
	object_ptr<Ui::LinkButton> _noEmailAccess;
	Fn<void()> _closeParent;

	QString _error;

	rpl::event_stream<QByteArray> _newPasswordSet;
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
	const QString &error,
	not_null<Main::Session*> session,
	TextWithEntities &&about);
