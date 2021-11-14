#ifndef TELEGRAM_FAKEPASSCODE_BOX_H
#define TELEGRAM_FAKEPASSCODE_BOX_H

#include "boxes/abstract_box.h"
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

class FakePasscodeBox : public Ui::BoxContent {
public:
    FakePasscodeBox(QWidget*, not_null<Main::Session*> session, bool turningOff,
                    bool turningOn, size_t fakeIndex);

    struct CloudFields {
        static CloudFields From(const Core::CloudPasswordState &current);

        Core::CloudPasswordCheckRequest curRequest;
        Core::CloudPasswordAlgo newAlgo;
        bool hasRecovery = false;
        QString fromRecoveryCode;
        bool notEmptyPassport = false;
        QString hint;
        Core::SecureSecretAlgo newSecureSecretAlgo;
        bool turningOff = false;
        TimeId pendingResetDate = 0;

        // Check cloud password for some action.
        Fn<void(const Core::CloudPasswordResult &)> customCheckCallback;
        rpl::producer<QString> customTitle;
        std::optional<QString> customDescription;
        rpl::producer<QString> customSubmitButton;
    };
    FakePasscodeBox(
            QWidget*,
            not_null<MTP::Instance*> mtp,
            Main::Session *session,
            const CloudFields &fields);
    FakePasscodeBox(
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
    void emailChanged();
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
    void setPasswordFail(const MTP::Error &error);
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
    bool _turningOn = false;
    bool _cloudPwd = false;
    size_t _fakeIndex = 0;
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
    object_ptr<Ui::InputField> _passwordName;
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

#endif //TELEGRAM_FAKEPASSCODE_BOX_H
