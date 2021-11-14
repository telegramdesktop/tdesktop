#include "fakepasscodes_list.h"
#include "lang/lang_keys.h"
#include "ui/wrap/vertical_layout.h"
#include "fakepasscode/fake_passcode.h"
#include "fakepasscode/action.h"
#include "settings/settings_common.h"
#include "styles/style_boxes.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "fakepasscode/action_ui/fakepasscode_box.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/buttons.h"
#include "fakepasscode/action_ui/action_ui.h"
#include "main/main_domain.h"
#include "storage/storage_domain.h"
#include "boxes/abstract_box.h"
#include "ui/text/text_utilities.h"

class FakePasscodeContent : public Ui::RpWidget {
public:
    FakePasscodeContent(QWidget* parent, FakePasscode::FakePasscode* passcode,
                        Main::Domain* domain, not_null<Window::SessionController*> controller,
                        size_t passcodeIndex);

    void setupContent();

private:
    FakePasscode::FakePasscode* _passcode;
    Main::Domain* _domain;
    Window::SessionController* _controller;
    size_t _passcodeIndex;
};

void FakePasscodeContent::setupContent() {
    const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
    Settings::AddSubsectionTitle(content, tr::lng_fakeaction_list());
    for (const auto& type : FakePasscode::kAvailableActions) {
        std::shared_ptr<FakePasscode::Action> action = FakePasscode::CreateAction(type);
        const auto ui = GetUIByAction(action, _passcode, this);
        ui->Create(content);
        Settings::AddDivider(content);
    }
    Settings::AddButton(content, tr::lng_fakepasscode_change(), st::settingsButton)
        ->addClickHandler([this] {
            _controller->show(Box<FakePasscodeBox>(&_controller->session(), false, false,
                                                   _passcodeIndex),
                              Ui::LayerOption::KeepOther);
            update();
        });
    Settings::AddButton(content, tr::lng_remove_fakepasscode(), st::terminateSessionsButton)
        ->addClickHandler([this] {
            _domain->local().RemoveFakePasscode(*_passcode);
            close();
        });
    Ui::ResizeFitChild(this, content);
}

FakePasscodeContent::FakePasscodeContent(QWidget *parent, FakePasscode::FakePasscode* passcode,
                                         Main::Domain* domain, not_null<Window::SessionController*> controller,
                                         size_t passcodeIndex)
: Ui::RpWidget(parent)
, _passcode(passcode)
, _domain(domain)
, _controller(controller)
, _passcodeIndex(passcodeIndex) {
}

class FakePasscodeContentBox : public Ui::BoxContent {
public:
    FakePasscodeContentBox(QWidget* parent, FakePasscode::FakePasscode* passcode,
                           Main::Domain* domain, not_null<Window::SessionController*> controller,
                           size_t passcodeIndex);

protected:
    void prepare() override;

private:
    FakePasscode::FakePasscode* _passcode;
    Main::Domain* _domain;
    Window::SessionController* _controller;
    size_t _passcodeIndex;
};

class FakePasscodeList : public Ui::RpWidget {
public:
    FakePasscodeList(QWidget*, not_null<Main::Domain*> domain,
                     not_null<Window::SessionController*> controller);

    void setupContent();

private:
    Main::Domain* _domain;
    Window::SessionController* _controller;
};

FakePasscodeList::FakePasscodeList(QWidget * parent, not_null<Main::Domain *> domain,
                                   not_null<Window::SessionController*> controller)
: Ui::RpWidget(parent), _domain(domain), _controller(controller) {
}

void FakePasscodeList::setupContent() {
    using namespace Settings;
    const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
    auto& passcodes = _domain->local().GetFakePasscodesMutable();
    LOG(("Draw " + QString::number(passcodes.size()) + " passcodes"));
    for (size_t i = 0; i < passcodes.size(); ++i) {
        AddButton(content, tr::lng_fakepasscode(lt_caption, rpl::single(passcodes[i].GetName())),
                  st::settingsButton)->addClickHandler([this, &passcodes, i]{
            _controller->show(Box<FakePasscodeContentBox>(&(passcodes[i]), _domain,
                                                          _controller, i),
                    Ui::LayerOption::KeepOther);
            update();
        });
    }
    AddDivider(content);
    AddButton(content, tr::lng_add_fakepasscode(), st::settingsButton)->addClickHandler([this] {
        _controller->show(Box<FakePasscodeBox>(&_controller->session(), false, true, 0), // _domain
                          Ui::LayerOption::KeepOther);
        update();
    });
    Ui::ResizeFitChild(this, content);
}

FakePasscodeContentBox::FakePasscodeContentBox(QWidget *, FakePasscode::FakePasscode* passcode,
                                               Main::Domain* domain, not_null<Window::SessionController*> controller,
                                               size_t passcodeIndex)
: _passcode(passcode)
, _domain(domain)
, _controller(controller)
, _passcodeIndex(passcodeIndex) {
}

void FakePasscodeContentBox::prepare() {
    using namespace Settings;
    LOG(("Prepare fake content"));
//    Ui::ResizeFitChild(this, content);
    addButton(tr::lng_close(), [=] { closeBox(); });
    const auto content =
            setInnerWidget(object_ptr<FakePasscodeContent>(this, _passcode, _domain, _controller,
                                                           _passcodeIndex),
                    st::sessionsScroll);
    content->resize(st::boxWideWidth, st::noContactsHeight);
    content->setupContent();
    setDimensions(st::boxWideWidth, st::sessionsHeight);
}

void FakePasscodeListBox::prepare() {
    setTitle(tr::lng_fakepasscodes_list());
    addButton(tr::lng_close(), [=] { closeBox(); });

    const auto w = st::boxWideWidth;

    const auto content = setInnerWidget(
            object_ptr<FakePasscodeList>(this, _domain, _controller),
            st::sessionsScroll);
    content->resize(w, st::noContactsHeight);
    content->setupContent();

    setDimensions(w, st::sessionsHeight);
}

FakePasscodeListBox::FakePasscodeListBox(QWidget *, not_null<Main::Domain *> domain,
                                         not_null<Window::SessionController*> controller)
: _domain(domain), _controller(controller) {
}
