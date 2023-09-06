#ifndef TELEGRAM_FAKEPASSCODE_BOX_H
#define TELEGRAM_FAKEPASSCODE_BOX_H

#include "boxes/abstract_box.h"
#include "mtproto/sender.h"
#include "core/core_cloud_password.h"
#include "window/window_session_controller.h"

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
    FakePasscodeBox(QWidget*, not_null<Window::SessionController*> controller, bool turningOff,
                    bool turningOn, size_t fakeIndex);


    rpl::producer<QByteArray> newPasswordSet() const;
    rpl::producer<> passwordReloadNeeded() const;
    rpl::producer<> clearUnconfirmedPassword() const;

protected:
    void prepare() override;
    void setInnerFocus() override;

    void paintEvent(QPaintEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;

private:
    void submit();
    void oldChanged();
    void newChanged();
    void save(bool force = false);
    void badOldPasscode();
    bool currentlyHave() const;
    bool onlyCheckCurrent() const;

    Main::Session *_session = nullptr;
    Window::SessionController* _controller = nullptr;

    QString _pattern;

    bool _turningOff = false;
    bool _turningOn = false;
    size_t _fakeIndex = 0;

    int _aboutHeight = 0;

    Ui::Text::String _about, _hintText;

    object_ptr<Ui::PasswordInput> _oldPasscode;
    object_ptr<Ui::PasswordInput> _newPasscode;
    object_ptr<Ui::PasswordInput> _reenterPasscode;
    object_ptr<Ui::InputField> _passwordName;
    object_ptr<Ui::InputField> _passwordHint;

    QString _oldError, _newError;

    rpl::event_stream<QByteArray> _newPasswordSet;
    rpl::event_stream<> _passwordReloadNeeded;
    rpl::event_stream<> _clearUnconfirmedPassword;

};

#endif //TELEGRAM_FAKEPASSCODE_BOX_H
