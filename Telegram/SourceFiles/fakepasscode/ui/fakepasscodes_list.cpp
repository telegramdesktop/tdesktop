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
#include "fakepasscode/ui/fakepasscode_box.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/buttons.h"
#include "fakepasscode/ui/action_ui.h"
#include "main/main_domain.h"
#include "storage/storage_domain.h"
#include "boxes/abstract_box.h"
#include "ui/text/text_utilities.h"
#include "styles/style_menu_icons.h"
#include "fakepasscode/log/fake_log.h"

class FakePasscodeContentBox;

class FakePasscodeContent : public Ui::RpWidget {
public:
    FakePasscodeContent(QWidget* parent,
                        Main::Domain* domain, not_null<Window::SessionController*> controller,
                        size_t passcodeIndex, FakePasscodeContentBox* outerBox);

    void setupContent();

private:
    Main::Domain* _domain;
    Window::SessionController* _controller;
    size_t _passcodeIndex;
    FakePasscodeContentBox* _outerBox;
};

FakePasscodeContent::FakePasscodeContent(QWidget *parent,
                                         Main::Domain* domain, not_null<Window::SessionController*> controller,
                                         size_t passcodeIndex, FakePasscodeContentBox* outerBox)
: Ui::RpWidget(parent)
, _domain(domain)
, _controller(controller)
, _passcodeIndex(passcodeIndex)
, _outerBox(outerBox) {
}

void FakePasscodeContent::setupContent() {
    const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
    Settings::AddSubsectionTitle(content, tr::lng_fakeaction_list());

    for (const auto& type : FakePasscode::kAvailableActions) {
        const auto ui = GetUIByAction(type, _domain, _passcodeIndex, this);
        ui->Create(content, _controller);
        Settings::AddDivider(content);
    }
    Settings::AddButton(content, tr::lng_fakepasscode_change(), st::settingsButton,
                        {&st::menuIconEdit})
            ->addClickHandler([this] {
                _controller->show(Box<FakePasscodeBox>(_controller, false, false,
                                                       _passcodeIndex),
                                  Ui::LayerOption::KeepOther);
            });
    Settings::AddButton(content, tr::lng_remove_fakepasscode(), st::settingsAttentionButton)
            ->addClickHandler([this] {
                destroy();
                _domain->local().RemoveFakePasscode(_passcodeIndex);
                _outerBox->closeBox();
            });
    Ui::ResizeFitChild(this, content);
}

class FakePasscodeList : public Ui::RpWidget {
public:
    FakePasscodeList(QWidget*, not_null<Main::Domain*> domain,
                     not_null<Window::SessionController*> controller);

    void setupContent();

private:
    Main::Domain* _domain;
    Window::SessionController* _controller;

    void draw(size_t passcodesSize);
};

FakePasscodeList::FakePasscodeList(QWidget * parent, not_null<Main::Domain *> domain,
                                   not_null<Window::SessionController*> controller)
: Ui::RpWidget(parent), _domain(domain), _controller(controller) {
}

void FakePasscodeList::draw(size_t passcodesSize) {
    using namespace Settings;
    FAKE_LOG(("Draw %1 passcodes").arg(passcodesSize));
    const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
    for (size_t i = 0; i < passcodesSize; ++i) {
        AddButton(content, tr::lng_fakepasscode(lt_caption, _domain->local().GetFakePasscodeName(i)),
                  st::settingsButton, { &st::menuIconLock }
                  )->addClickHandler([this, i]{
            _controller->show(Box<FakePasscodeContentBox>(_domain, _controller, i),
                              Ui::LayerOption::KeepOther);
        });
    }
    AddDivider(content);
    AddButton(content, tr::lng_add_fakepasscode(), st::settingsButton,
              {&st::settingsIconAdd})->addClickHandler([this] {
        _controller->show(Box<FakePasscodeBox>(_controller, false, true, 0), // _domain
                          Ui::LayerOption::KeepOther);
    });
    AddDividerText(content, tr::lng_special_actions());
    const auto toggledCacheCleaning = Ui::CreateChild<rpl::event_stream<bool>>(this);
    auto buttonCacheCleaning = AddButton(content, tr::lng_clear_cache_on_lock(), st::settingsButton,
                                         {&st::menuIconClear})
            ->toggleOn(toggledCacheCleaning->events_starting_with_copy(_domain->local().IsCacheCleanedUpOnLock()));

    buttonCacheCleaning->addClickHandler([=] {
        _domain->local().SetCacheCleanedUpOnLock(buttonCacheCleaning->toggled());
        _domain->local().writeAccounts();
    });

    const auto toggledLogging = Ui::CreateChild<rpl::event_stream<bool>>(this);
    auto buttonLogging = AddButton(content, tr::lng_enable_advance_logging(), st::settingsButton,
                                   {&st::menuIconSavedMessages})
            ->toggleOn(toggledLogging->events_starting_with_copy(_domain->local().IsAdvancedLoggingEnabled()));

    buttonLogging->addClickHandler([=] {
        _domain->local().SetAdvancedLoggingEnabled(buttonLogging->toggled());
        _domain->local().writeAccounts();
    });

    const auto toggledErasingCleaning = Ui::CreateChild<rpl::event_stream<bool>>(this);
    auto buttonErasing = AddButton(content, tr::lng_enable_dod_cleaning(), st::settingsButton,
                                       {&st::menuIconClear})
        ->toggleOn(toggledErasingCleaning->events_starting_with_copy(_domain->local().IsErasingEnabled()));

    buttonErasing->addClickHandler([=] {
        _domain->local().SetErasingEnabled(buttonErasing->toggled());
        _domain->local().writeAccounts();
    });

    Ui::ResizeFitChild(this, content);
    FAKE_LOG(("Draw %1 passcodes: success").arg(passcodesSize));
}

void FakePasscodeList::setupContent() {
    using namespace Settings;
    auto size = _domain->local().GetFakePasscodesSize();
    std::move(size) | rpl::start_with_next([this](size_t value) {
        draw(value);
    }, lifetime());
}

FakePasscodeContentBox::FakePasscodeContentBox(QWidget *,
                                               Main::Domain* domain, not_null<Window::SessionController*> controller,
                                               size_t passcodeIndex)
: _domain(domain)
, _controller(controller)
, _passcodeIndex(passcodeIndex) {
}

void FakePasscodeContentBox::prepare() {
    using namespace Settings;
    addButton(tr::lng_close(), [=] { closeBox(); });
    const auto content =
            setInnerWidget(object_ptr<FakePasscodeContent>(this, _domain, _controller,
                                                           _passcodeIndex, this),
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
