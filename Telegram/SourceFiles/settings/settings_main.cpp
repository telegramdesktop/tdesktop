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
#include "ui/wrap/slide_wrap.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/discrete_sliders.h"
#include "ui/widgets/buttons.h"
#include "info/profile/info_profile_cover.h"
#include "data/data_user.h"
#include "data/data_session.h"
#include "data/data_cloud_themes.h"
#include "data/data_chat_filters.h"
#include "lang/lang_keys.h"
#include "lang/lang_instance.h"
#include "storage/localstorage.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "main/main_account.h"
#include "main/main_app_config.h"
#include "apiwrap.h"
#include "api/api_sensitive_content.h"
#include "api/api_global_privacy.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "core/click_handler_types.h"
#include "base/call_delayed.h"
#include "facades.h"
#include "app.h"
#include "styles/style_settings.h"

namespace Settings {

void SetupLanguageButton(
		not_null<Ui::VerticalLayout*> container,
		bool icon) {
	const auto button = AddButtonWithLabel(
		container,
		tr::lng_settings_language(),
		rpl::single(
			Lang::GetInstance().id()
		) | rpl::then(
			Lang::GetInstance().idChanges()
		) | rpl::map([] { return Lang::GetInstance().nativeName(); }),
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

	const auto preload = [=] {
		controller->session().data().chatsFilters().requestSuggested();
	};
	const auto account = &controller->session().account();
	const auto slided = container->add(
		object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
			container,
			CreateButton(
				container,
				tr::lng_settings_section_filters(),
				st::settingsSectionButton,
				&st::settingsIconFolders)))->setDuration(0);
	if (!controller->session().data().chatsFilters().list().empty()
		|| controller->session().settings().dialogsFiltersEnabled()) {
		slided->show(anim::type::instant);
		preload();
	} else {
		const auto enabled = [=] {
			const auto result = account->appConfig().get<bool>(
				"dialog_filters_enabled",
				false);
			if (result) {
				preload();
			}
			return result;
		};
		const auto preloadIfEnabled = [=](bool enabled) {
			if (enabled) {
				preload();
			}
		};
		slided->toggleOn(
			rpl::single(
				rpl::empty_value()
			) | rpl::then(
				account->appConfig().refreshed()
			) | rpl::map(
				enabled
			) | rpl::before_next(preloadIfEnabled));
	}
	slided->entity()->setClickedCallback([=] {
		showOther(Type::Folders);
	});

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
		not_null<Window::Controller*> window,
		not_null<Ui::VerticalLayout*> container,
		bool icon) {
	if (!HasInterfaceScale()) {
		return;
	}

	const auto toggled = Ui::CreateChild<rpl::event_stream<bool>>(
		container.get());

	const auto switched = (cConfigScale() == style::kScaleAuto);
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
		if (cConfigScale() == style::kScaleAuto) {
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
	const auto inSetScale = container->lifetime().make_state<bool>();
	const auto setScale = [=](int scale, const auto &repeatSetScale) -> void {
		if (*inSetScale) {
			return;
		}
		*inSetScale = true;
		const auto guard = gsl::finally([=] { *inSetScale = false; });

		toggled->fire(scale == style::kScaleAuto);
		slider->setActiveSection(sectionFromScale(scale));
		if (cEvalScale(scale) != cEvalScale(cConfigScale())) {
			const auto confirmed = crl::guard(button, [=] {
				cSetConfigScale(scale);
				Local::writeSettings();
				App::restart();
			});
			const auto cancelled = crl::guard(button, [=] {
				base::call_delayed(
					st::defaultSettingsSlider.duration,
					button,
					[=] { repeatSetScale(cConfigScale(), repeatSetScale); });
			});
			window->show(Box<ConfirmBox>(
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
	}) | rpl::filter([=](int scale) {
		return cEvalScale(scale) != cEvalScale(cConfigScale());
	}) | rpl::start_with_next([=](int scale) {
		setScale(
			(scale == cScreenScale()) ? style::kScaleAuto : scale,
			setScale);
	}, slider->lifetime());

	button->toggledValue(
	) | rpl::map([](bool checked) {
		return checked ? style::kScaleAuto : cEvalScale(cConfigScale());
	}) | rpl::start_with_next([=](int scale) {
		setScale(scale, setScale);
	}, button->lifetime());
}

void OpenFaq() {
	UrlClickHandler::Open(telegramFaqLink());
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
	const auto requestId = button->lifetime().make_state<mtpRequestId>();
	button->lifetime().add([=] {
		if (*requestId) {
			controller->session().api().request(*requestId).cancel();
		}
	});
	button->addClickHandler([=] {
		const auto sure = crl::guard(button, [=] {
			if (*requestId) {
				return;
			}
			*requestId = controller->session().api().request(
				MTPhelp_GetSupport()
			).done([=](const MTPhelp_Support &result) {
				*requestId = 0;
				result.match([&](const MTPDhelp_support &data) {
					auto &owner = controller->session().data();
					if (const auto user = owner.processUser(data.vuser())) {
						Ui::showPeerHistory(user, ShowAtUnreadMsgId);
					}
				});
			}).fail([=](const MTP::Error &error) {
				*requestId = 0;
			}).send();
		});
		auto box = Box<ConfirmBox>(
			tr::lng_settings_ask_sure(tr::now),
			tr::lng_settings_ask_ok(tr::now),
			tr::lng_settings_faq_button(tr::now),
			sure,
			OpenFaq);
		box->setStrictCancel(true);
		controller->show(std::move(box));
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
	crl::on_main(this, [=, text = e->text()]{
		CodesFeedString(_controller, text);
	});
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
		SetupInterfaceScale(&controller->window(), content);
		AddSkip(content);
	}
	SetupHelp(controller, content);

	Ui::ResizeFitChild(this, content);

	// If we load this in advance it won't jump when we open its' section.
	controller->session().api().reloadPasswordState();
	controller->session().api().reloadContactSignupSilent();
	controller->session().api().sensitiveContent().reload();
	controller->session().api().globalPrivacy().reload();
	controller->session().data().cloudThemes().refresh();
}

rpl::producer<Type> Main::sectionShowOther() {
	return _showOther.events();
}

} // namespace Settings
