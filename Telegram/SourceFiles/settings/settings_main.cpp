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
#include "lang/lang_keys.h"
#include "storage/localstorage.h"
#include "auth_session.h"
#include "apiwrap.h"
#include "core/file_utilities.h"
#include "styles/style_settings.h"

namespace Settings {

void SetupLanguageButton(
		not_null<Ui::VerticalLayout*> container,
		bool icon) {
	const auto button = AddButtonWithLabel(
		container,
		lng_settings_language,
		rpl::single(Lang::Current().nativeName()),
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
		not_null<Ui::VerticalLayout*> container,
		Fn<void(Type)> showOther) {
	AddDivider(container);
	AddSkip(container);

	const auto addSection = [&](
			LangKey label,
			Type type,
			const style::icon *icon) {
		AddButton(
			container,
			label,
			st::settingsSectionButton,
			icon
		)->addClickHandler([=] { showOther(type); });
	};
	if (Auth().supportMode()) {
		SetupSupport(container);

		AddDivider(container);
		AddSkip(container);
	} else {
		addSection(
			lng_settings_information,
			Type::Information,
			&st::settingsIconInformation);
	}
	addSection(
		lng_settings_section_notify,
		Type::Notifications,
		&st::settingsIconNotifications);
	addSection(
		lng_settings_section_privacy,
		Type::PrivacySecurity,
		&st::settingsIconPrivacySecurity);
	addSection(
		lng_settings_section_chat_settings,
		Type::Chat,
		&st::settingsIconChat);
	addSection(
		lng_settings_section_call_settings,
		Type::Calls,
		&st::settingsIconCalls);
	addSection(
		lng_settings_advanced,
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
		lng_settings_default_scale,
		icon ? st::settingsSectionButton : st::settingsButton,
		icon ? &st::settingsIconInterfaceScale : nullptr
	)->toggleOn(toggled->events_starting_with_copy(switched));

	const auto slider = container->add(
		object_ptr<Ui::SettingsSlider>(container, st::settingsSlider),
		icon ? st::settingsScalePadding : st::settingsBigScalePadding);

	static const auto ScaleValues = (cIntRetinaFactor() > 1)
		? std::vector<int>{ 100, 110, 120, 130, 140, 150 }
		: std::vector<int>{ 100, 125, 150, 200, 250, 300 };
	const auto sectionFromScale = [](int scale) {
		scale = cEvalScale(scale);
		auto result = 0;
		for (const auto value : ScaleValues) {
			if (scale <= value) {
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
				lang(lng_settings_need_restart),
				lang(lng_settings_restart_now),
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
		lng_settings_faq,
		icon ? st::settingsSectionButton : st::settingsButton,
		icon ? &st::settingsIconFaq : nullptr
	)->addClickHandler(OpenFaq);
}

void SetupHelp(not_null<Ui::VerticalLayout*> container) {
	AddDivider(container);
	AddSkip(container);

	SetupFaq(container);

	if (AuthSession::Exists()) {
		const auto button = AddButton(
			container,
			lng_settings_ask_question,
			st::settingsSectionButton);
		button->addClickHandler([=] {
			const auto ready = crl::guard(button, [](const MTPUser &data) {
				const auto users = MTP_vector<MTPUser>(1, data);
				if (const auto user = App::feedUsers(users)) {
					Ui::showPeerHistory(user, ShowAtUnreadMsgId);
				}
			});
			const auto sure = crl::guard(button, [=] {
				Auth().api().requestSupportContact(ready);
			});
			auto box = Box<ConfirmBox>(
				lang(lng_settings_ask_sure),
				lang(lng_settings_ask_ok),
				lang(lng_settings_faq_button),
				sure,
				OpenFaq);
			box->setStrictCancel(true);
			Ui::show(std::move(box));
		});
	}

	AddSkip(container);
}

Main::Main(
	QWidget *parent,
	not_null<Window::Controller*> controller,
	not_null<UserData*> self)
: Section(parent)
, _self(self) {
	setupContent(controller);
}

void Main::keyPressEvent(QKeyEvent *e) {
	CodesFeedString(e->text());
	return Section::keyPressEvent(e);
}

void Main::setupContent(not_null<Window::Controller*> controller) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	const auto cover = content->add(object_ptr<Info::Profile::Cover>(
		content,
		_self,
		controller));
	cover->setOnlineCount(rpl::single(0));

	SetupSections(content, [=](Type type) {
		_showOther.fire_copy(type);
	});
	if (HasInterfaceScale()) {
		AddDivider(content);
		AddSkip(content);
		SetupInterfaceScale(content);
		AddSkip(content);
	}
	SetupHelp(content);

	Ui::ResizeFitChild(this, content);

	// If we load this in advance it won't jump when we open its' section.
	Auth().api().reloadPasswordState();
}

rpl::producer<Type> Main::sectionShowOther() {
	return _showOther.events();
}

} // namespace Settings
