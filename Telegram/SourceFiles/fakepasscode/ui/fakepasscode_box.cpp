#include "fakepasscode_box.h"
#include "base/bytes.h"
#include "lang/lang_keys.h"
#include "ui/boxes/confirm_box.h"
#include "base/unixtime.h"
#include "mainwindow.h"
#include "apiwrap.h"
#include "api/api_cloud_password.h"
#include "main/main_session.h"
#include "main/main_domain.h"
#include "core/application.h"
#include "storage/storage_domain.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/painter.h"
#include "passport/passport_encryption.h"
#include "passport/passport_panel_edit_contact.h"
#include "settings/settings_privacy_security.h"
#include "styles/style_layers.h"
#include "styles/style_passport.h"
#include "styles/style_boxes.h"
#include "fakepasscode/log/fake_log.h"
#include "fakepasscodes_list.h"

FakePasscodeBox::FakePasscodeBox(
        QWidget*,
        not_null<Window::SessionController*> controller,
        bool turningOff,
        bool turningOn,
        size_t fakeIndex)
        : _session(&controller->session())
        , _controller(controller)
        , _turningOff(turningOff)
        , _turningOn(turningOn)
        , _fakeIndex(fakeIndex)
        , _about(st::boxWidth - st::boxPadding.left() * 1.5)
        , _oldPasscode(this, st::defaultInputField, tr::lng_passcode_enter_old())
        , _newPasscode(
                this,
                st::defaultInputField,
                _session->domain().local().hasLocalPasscode()
                ? tr::lng_passcode_enter_new()
                : tr::lng_passcode_enter_first())
        , _reenterPasscode(this, st::defaultInputField, tr::lng_passcode_confirm_new())
        , _passwordName(this, st::defaultInputField, tr::lng_fakepasscode_name())
        , _passwordHint(this, st::defaultInputField, tr::lng_cloud_password_hint()) {
}

rpl::producer<QByteArray> FakePasscodeBox::newPasswordSet() const {
    return _newPasswordSet.events();
}

rpl::producer<> FakePasscodeBox::passwordReloadNeeded() const {
    return _passwordReloadNeeded.events();
}

rpl::producer<> FakePasscodeBox::clearUnconfirmedPassword() const {
    return _clearUnconfirmedPassword.events();
}

bool FakePasscodeBox::currentlyHave() const {
    return !_turningOn;
}

bool FakePasscodeBox::onlyCheckCurrent() const {
    return _turningOff;
}

void FakePasscodeBox::prepare() {
    addButton(_turningOff
               ? tr::lng_passcode_remove_button()
               : tr::lng_settings_save(),
            [=] { save(); });
    addButton(tr::lng_cancel(), [=] { closeBox(); });

    const auto onlyCheck = onlyCheckCurrent();
    if (onlyCheck) {
        _oldPasscode->show();
        setTitle(tr::lng_remove_fakepasscode());
        setDimensions(st::boxWidth, st::passcodePadding.top() + _oldPasscode->height() + st::passcodeTextLine + st::passcodeAboutSkip + _aboutHeight + st::passcodePadding.bottom());
    } else {
        if (currentlyHave()) {
            _oldPasscode->show();
            setTitle(tr::lng_fakepasscode_change());
            setDimensions(st::boxWidth, st::passcodePadding.top() + _oldPasscode->height() + st::passcodeTextLine + _newPasscode->height() + st::passcodeLittleSkip + _reenterPasscode->height() + st::passcodeLittleSkip + _passwordName->height() + st::passcodeSkip + st::passcodeAboutSkip + _aboutHeight + st::passcodePadding.bottom());
        } else {
            _oldPasscode->hide();
            setTitle(tr::lng_fakepasscode_create());
            setDimensions(st::boxWidth, st::passcodePadding.top() + _newPasscode->height() + st::passcodeLittleSkip + _reenterPasscode->height() + st::passcodeLittleSkip + _passwordName->height() + st::passcodeSkip + st::passcodeAboutSkip + _aboutHeight + st::passcodePadding.bottom());
        }
    }

    connect(_oldPasscode, &Ui::MaskedInputField::changed, [=] { oldChanged(); });
    connect(_newPasscode, &Ui::MaskedInputField::changed, [=] { newChanged(); });
    connect(_reenterPasscode, &Ui::MaskedInputField::changed, [=] { newChanged(); });
    connect(_passwordName, &Ui::InputField::changed, [=] { newChanged(); });
    connect(_passwordHint, &Ui::InputField::changed, [=] { newChanged(); });

    const auto fieldSubmit = [=] { submit(); };
    connect(_oldPasscode, &Ui::MaskedInputField::submitted, fieldSubmit);
    connect(_newPasscode, &Ui::MaskedInputField::submitted, fieldSubmit);
    connect(_reenterPasscode, &Ui::MaskedInputField::submitted, fieldSubmit);
    connect(_passwordName, &Ui::InputField::submitted, fieldSubmit);
    connect(_passwordHint, &Ui::InputField::submitted, fieldSubmit);

    const auto has = currentlyHave();
    _oldPasscode->setVisible(onlyCheck || has);
    _newPasscode->setVisible(!onlyCheck);
    _reenterPasscode->setVisible(!onlyCheck);
    _passwordHint->hide();
    _passwordName->setVisible(!onlyCheck);
    if (!_turningOn) {
        _passwordName->setText(_session->domain().local().GetCurrentFakePasscodeName(_fakeIndex));
    }
}

void FakePasscodeBox::submit() {
    const auto has = currentlyHave();
    if (_oldPasscode->hasFocus()) {
        if (onlyCheckCurrent()) {
            save();
        } else {
            _newPasscode->setFocus();
        }
    } else if (_newPasscode->hasFocus()) {
        _reenterPasscode->setFocus();
    } else {
        if (has && _oldPasscode->text().isEmpty()) {
            _oldPasscode->setFocus();
            _oldPasscode->showError();
        } else if (!_passwordHint->isHidden()) {
            _passwordHint->setFocus();
        } else if (_session->domain().local().CheckFakePasscodeExists(_newPasscode->text().toUtf8())) {
            _newPasscode->setFocus();
            _newPasscode->showError();
        } else {
            save();
        }
    }
}

void FakePasscodeBox::paintEvent(QPaintEvent *e) {
    BoxContent::paintEvent(e);

    Painter p(this);

    int32 w = st::boxWidth - st::boxPadding.left() * 1.5;
    int32 abouty = (_passwordHint->isHidden() ? ((_reenterPasscode->isHidden() ? (_oldPasscode->y()) : _reenterPasscode->y()) + st::passcodeSkip) : _passwordHint->y()) + _oldPasscode->height() + st::passcodeLittleSkip + st::passcodeAboutSkip;
    p.setPen(st::boxTextFg);
    _about.drawLeft(p, st::boxPadding.left(), abouty, w, width());

    if (!_hintText.isEmpty() && _oldError.isEmpty()) {
        _hintText.drawLeftElided(p, st::boxPadding.left(), _oldPasscode->y() + _oldPasscode->height() + ((st::passcodeTextLine - st::normalFont->height) / 2), w, width(), 1, style::al_topleft);
    }

    if (!_oldError.isEmpty()) {
        p.setPen(st::boxTextFgError);
        p.drawText(QRect(st::boxPadding.left(), _oldPasscode->y() + _oldPasscode->height(), w, st::passcodeTextLine), _oldError, style::al_left);
    }

    if (!_newError.isEmpty()) {
        p.setPen(st::boxTextFgError);
        p.drawText(QRect(st::boxPadding.left(), _reenterPasscode->y() + _reenterPasscode->height(), w, st::passcodeTextLine), _newError, style::al_left);
    }
}

void FakePasscodeBox::resizeEvent(QResizeEvent *e) {
    BoxContent::resizeEvent(e);

    const auto has = currentlyHave();
    int32 w = st::boxWidth - st::boxPadding.left() - st::boxPadding.right();
    _oldPasscode->resize(w, _oldPasscode->height());
    _oldPasscode->moveToLeft(st::boxPadding.left(), st::passcodePadding.top());
    _newPasscode->resize(w, _newPasscode->height());
    _newPasscode->moveToLeft(st::boxPadding.left(), _oldPasscode->y() + ((_turningOff || has) ? (_oldPasscode->height() + st::passcodeTextLine) : 0));
    _reenterPasscode->resize(w, _reenterPasscode->height());
    _reenterPasscode->moveToLeft(st::boxPadding.left(), _newPasscode->y() + _newPasscode->height() + st::passcodeLittleSkip);
    _passwordName->resize(w, _passwordName->height());
    _passwordName->moveToLeft(st::boxPadding.left(), _reenterPasscode->y() + _reenterPasscode->height() + st::passcodeSkip);
    _passwordHint->resize(w, _passwordHint->height());
    _passwordHint->moveToLeft(st::boxPadding.left(), _reenterPasscode->y() + _reenterPasscode->height() + st::passcodeSkip);
}

void FakePasscodeBox::setInnerFocus() {
    if (_oldPasscode->isHidden()) {
        _newPasscode->setFocusFast();
    } else {
        _oldPasscode->setFocusFast();
    }
}

void FakePasscodeBox::save(bool force) {
    QString old = _oldPasscode->text(), pwd = _newPasscode->text(), conf = _reenterPasscode->text();
    QString name = _passwordName->getLastText();
    const auto has = currentlyHave();
    if (_turningOff || has) {
        if (!passcodeCanTry()) {
            _oldError = tr::lng_flood_error(tr::now);
            _oldPasscode->setFocus();
            _oldPasscode->showError();
            update();
            return;
        }

        if (_session->domain().local().checkFakePasscode(old.toUtf8(), _fakeIndex)) {
            cSetPasscodeBadTries(0);
            if (_turningOff) pwd = conf = QString();
        } else {
            cSetPasscodeBadTries(cPasscodeBadTries() + 1);
            cSetPasscodeLastTry(crl::now());
            badOldPasscode();
            return;
        }
    }
    const auto onlyCheck = onlyCheckCurrent();
    if (!onlyCheck && pwd != conf) {
        _reenterPasscode->selectAll();
        _reenterPasscode->setFocus();
        _reenterPasscode->showError();
        _newError = tr::lng_passcode_differ(tr::now);
        update();
        return;
    } else if (!onlyCheck && _turningOn && pwd.isEmpty()) {
        _newPasscode->setFocus();
        _newPasscode->showError();
        update();
        return;
    } else if (!onlyCheck && has && old == pwd) {
        _newPasscode->setFocus();
        _newPasscode->showError();
        _newError = tr::lng_passcode_is_same(tr::now);
        update();
        return;
    } else if (!onlyCheck && name.isEmpty()) {
        _passwordName->setFocus();
        _passwordName->showError();
        return;
    } else if (_session->domain().local().CheckFakePasscodeExists(pwd.toUtf8())) {
        _newPasscode->selectAll();
        _newPasscode->setFocus();
        _newPasscode->showError();
        _newError = tr::lng_passcode_exists(tr::now);
        update();
        return;
    } else {
        const auto weak = Ui::MakeWeak(this);
        cSetPasscodeBadTries(0);
        if (_turningOn) {
            _fakeIndex = _session->domain().local().AddFakePasscode(pwd.toUtf8(), name);
            _controller->show(Box<FakePasscodeContentBox>(&_session->domain(), _controller, _fakeIndex),
                Ui::LayerOption::KeepOther);
        } else {
            if (pwd.isEmpty()) {
                _session->domain().local().SetFakePasscodeName(name, _fakeIndex);
            } else {
                _session->domain().local().SetFakePasscode(pwd.toUtf8(), name, _fakeIndex);
            }
        }
        if (weak) {
            closeBox();
        }
    }
}

void FakePasscodeBox::badOldPasscode() {
    _oldPasscode->selectAll();
    _oldPasscode->setFocus();
    _oldPasscode->showError();
    _oldError = tr::lng_passcode_wrong(tr::now);
    update();
}

void FakePasscodeBox::oldChanged() {
    if (!_oldError.isEmpty()) {
        _oldError = QString();
        update();
    }
}

void FakePasscodeBox::newChanged() {
    if (!_newError.isEmpty()) {
        _newError = QString();
        update();
    }
}
