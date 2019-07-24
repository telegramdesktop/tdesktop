/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_main.h"

#include "settings/settings_common.h"
#include "settings/settings_codes.h"
#include "settings/settings_chat.h"
#include "boxes/language_box.h"
#include "boxes/confirm_box.h"
#include "boxes/about_box.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/discrete_sliders.h"
#include "info/profile/info_profile_button.h"
#include "info/profile/info_profile_cover.h"
#include "data/data_user.h"
#include "data/data_session.h"
#include "lang/lang_keys.h"
#include "storage/localstorage.h"
#include "main/main_session.h"
#include "apiwrap.h"
#include "window/window_session_controller.h"
#include "core/file_utilities.h"
#include "styles/style_settings.h"

namespace Settings {

void SetupLanguageButton(
		not_null<Ui::VerticalLayout*> container,
		bool icon) {
	const auto button = AddButtonWithLabel(
		container,
		tr::lng_settings_language(),
		rpl::single(
			Lang::Current().id()
		) | rpl::then(
			Lang::Current().idChanges()
		) | rpl::map([] { return Lang::Current().nativeName(); }),
		icon ? st::settingsSectionButton : st::settingsButton,
		icon ? &st::settingsIconLanguage : nullptr);
	const auto guard = Ui::CreateChild<base::binary_guard>(button.get());
	button->addClickHandler([=] {
		const auto m = button->clickModifiers();
		if ((m & Qt::ShiftModifier) && (m & Qt::AltModifier)) {
			Lang::CurrentCloudManager().switchToLanguage({ qsl("#custom") });
		} else {
			*guard = LanguageBox::Show();
		}
	});
}

void SetupSections(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container,
		Fn<void(Type)> showOther) {
	AddDivider(container);
	AddSkip(container);

	const auto addSection = [&](
			rpl::producer<QString> label,
			Type type,
			const style::icon *icon) {
		AddButton(
			container,
			std::move(label),
			st::settingsSectionButton,
			icon
		)->addClickHandler([=] { showOther(type); });
	};
	if (controller->session().supportMode()) {
		SetupSupport(controller, container);

		AddDivider(container);
		AddSkip(container);
	} else {
		addSection(
			tr::lng_settings_information(),
			Type::Information,
			&st::settingsIconInformation);
	}
	addSection(
		tr::lng_settings_section_notify(),
		Type::Notifications,
		&st::settingsIconNotifications);
	addSection(
		tr::lng_settings_section_privacy(),
		Type::PrivacySecurity,
		&st::settingsIconPrivacySecurity);
	addSection(
		tr::lng_settings_section_chat_settings(),
		Type::Chat,
		&st::settingsIconChat);
	addSection(
		tr::lng_settings_advanced(),
		Type::Advanced,
		&st::settingsIconGeneral);

	SetupLanguageButton(container);

	AddSkip(container);
}

bool HasInterfaceScale() {
	return true;
}

void SetupInterfaceScale(
		not_null<Ui::VerticalLayout*> container,
		bool icon) {
	if (!HasInterfaceScale()) {
		return;
	}

	const auto toggled = Ui::CreateChild<rpl::event_stream<bool>>(
		container.get());

	const auto switched = (cConfigScale() == kInterfaceScaleAuto);
	const auto button = AddButton(
		container,
		tr::lng_settings_default_scale(),
		icon ? st::settingsSectionButton : st::settingsButton,
		icon ? &st::settingsIconInterfaceScale : nullptr
	)->toggleOn(toggled->events_starting_with_copy(switched));

	const auto slider = container->add(
		object_ptr<Ui::SettingsSlider>(container, st::settingsSlider),
		icon ? st::settingsScalePadding : st::settingsBigScalePadding);

	static const auto ScaleValues = [&] {
		auto values = (cIntRetinaFactor() > 1)
			? std::vector<int>{ 100, 110, 120, 130, 140, 150 }
			: std::vector<int>{ 100, 125, 150, 200, 250, 300 };
		if (cConfigScale() == kInterfaceScaleAuto) {
			return values;
		}
		if (ranges::find(values, cConfigScale()) == end(values)) {
			values.push_back(cConfigScale());
		}
		return values;
	}();

	const auto sectionFromScale = [](int scale) {
		scale = cEvalScale(scale);
		auto result = 0;
		for (const auto value : ScaleValues) {
			if (scale == value) {
				break;
			}
			++result;
		}
		return (result == ScaleValues.size()) ? (result - 1) : result;
	};
	const auto inSetScale = Ui::CreateChild<bool>(container.get());
	const auto setScale = std::make_shared<Fn<void(int)>>();
	*setScale = [=](int scale) {
		if (*inSetScale) return;
		*inSetScale = true;
		const auto guard = gsl::finally([=] { *inSetScale = false; });

		toggled->fire(scale == kInterfaceScaleAuto);
		slider->setActiveSection(sectionFromScale(scale));
		if (cEvalScale(scale) != cEvalScale(cConfigScale())) {
			const auto confirmed = crl::guard(button, [=] {
				cSetConfigScale(scale);
				Local::writeSettings();
				App::restart();
			});
			const auto cancelled = crl::guard(button, [=] {
				App::CallDelayed(
					st::defaultSettingsSlider.duration,
					button,
					[=] { (*setScale)(cConfigScale()); });
			});
			Ui::show(Box<ConfirmBox>(
				tr::lng_settings_need_restart(tr::now),
				tr::lng_settings_restart_now(tr::now),
				confirmed,
				cancelled));
		} else if (scale != cConfigScale()) {
			cSetConfigScale(scale);
			Local::writeSettings();
		}
	};

	const auto label = [](int scale) {
		return QString::number(scale) + '%';
	};
	const auto scaleByIndex = [](int index) {
		return *(ScaleValues.begin() + index);
	};

	for (const auto value : ScaleValues) {
		slider->addSection(label(value));
	}
	slider->setActiveSectionFast(sectionFromScale(cConfigScale()));
	slider->sectionActivated(
	) | rpl::map([=](int section) {
		return scaleByIndex(section);
	}) | rpl::start_with_next([=](int scale) {
		(*setScale)((scale == cScreenScale())
			? kInterfaceScaleAuto
			: scale);
	}, slider->lifetime());

	button->toggledValue(
	) | rpl::map([](bool checked) {
		return checked ? kInterfaceScaleAuto : cEvalScale(cConfigScale());
	}) | rpl::start_with_next([=](int scale) {
		(*setScale)(scale);
	}, button->lifetime());
}

void OpenFaq() {
	QDesktopServices::openUrl(telegramFaqLink());
}

void SetupFaq(not_null<Ui::VerticalLayout*> container, bool icon) {
	AddButton(
		container,
		tr::lng_settings_faq(),
		icon ? st::settingsSectionButton : st::settingsButton,
		icon ? &st::settingsIconFaq : nullptr
	)->addClickHandler(OpenFaq);
}

void SetupHelp(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
	AddDivider(container);
	AddSkip(container);

	SetupFaq(container);

	const auto button = AddButton(
		container,
		tr::lng_settings_ask_question(),
		st::settingsSectionButton);
	button->addClickHandler([=] {
		const auto ready = crl::guard(button, [=](const MTPUser &data) {
			if (const auto user = controller->session().data().processUser(data)) {
				Ui::showPeerHistory(user, ShowAtUnreadMsgId);
			}
		});
		const auto sure = crl::guard(button, [=] {
			controller->session().api().requestSupportContact(ready);
		});
		auto box = Box<ConfirmBox>(
			tr::lng_settings_ask_sure(tr::now),
			tr::lng_settings_ask_ok(tr::now),
			tr::lng_settings_faq_button(tr::now),
			sure,
			OpenFaq);
		box->setStrictCancel(true);
		Ui::show(std::move(box));
	});

	AddSkip(container);
}

Main::Main(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent)
, _controller(controller) {
	setupContent(controller);
}

void Main::keyPressEvent(QKeyEvent *e) {
	CodesFeedString(&_controller->session(), e->text());
	return Section::keyPressEvent(e);
}

void Main::setupContent(not_null<Window::SessionController*> controller) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	const auto cover = content->add(object_ptr<Info::Profile::Cover>(
		content,
		controller->session().user(),
		controller));
	cover->setOnlineCount(rpl::single(0));

	SetupSections(controller, content, [=](Type type) {
		_showOther.fire_copy(type);
	});
	if (HasInterfaceScale()) {
		AddDivider(content);
		AddSkip(content);
		SetupInterfaceScale(content);
		AddSkip(content);
	}
	SetupHelp(controller, content);

	Ui::ResizeFitChild(this, content);

	// If we load this in advance it won't jump when we open its' section.
	controller->session().api().reloadPasswordState();
	controller->session().api().reloadContactSignupSilent();
}

rpl::producer<Type> Main::sectionShowOther() {
	return _showOther.events();
}

} // namespace Settings
