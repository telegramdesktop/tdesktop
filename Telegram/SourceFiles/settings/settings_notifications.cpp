/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_notifications.h"

#include "settings/settings_common.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/buttons.h"
#include "lang/lang_keys.h"
#include "info/profile/info_profile_button.h"
#include "storage/localstorage.h"
#include "window/notifications_manager.h"
#include "boxes/notifications_box.h"
#include "platform/platform_notifications_manager.h"
#include "mainwindow.h"
#include "auth_session.h"
#include "styles/style_settings.h"

namespace Settings {
namespace {

void SetupNotificationsContent(not_null<Ui::VerticalLayout*> container) {
	const auto checkbox = [&](LangKey label, bool checked) {
		return object_ptr<Ui::Checkbox>(
			container,
			lang(label),
			checked,
			st::settingsCheckbox);
	};
	const auto addCheckbox = [&](LangKey label, bool checked) {
		return container->add(
			checkbox(label, checked),
			st::settingsCheckboxPadding);
	};
	const auto addSlidingCheckbox = [&](LangKey label, bool checked) {
		return container->add(
			object_ptr<Ui::SlideWrap<Ui::Checkbox>>(
				container,
				checkbox(label, checked),
				st::settingsCheckboxPadding));
	};
	const auto desktop = addCheckbox(
		lng_settings_desktop_notify,
		Global::DesktopNotify());
	const auto name = addSlidingCheckbox(
		lng_settings_show_name,
		(Global::NotifyView() <= dbinvShowName));
	const auto preview = addSlidingCheckbox(
		lng_settings_show_preview,
		(Global::NotifyView() <= dbinvShowPreview));
	const auto sound = addCheckbox(
		lng_settings_sound_notify,
		Global::SoundNotify());
	const auto muted = addCheckbox(
		lng_settings_include_muted,
		Global::IncludeMuted());

	const auto nativeNotificationsKey = [&] {
		if (!Platform::Notifications::Supported()) {
			return LangKey();
		} else if (cPlatform() == dbipWindows) {
			return lng_settings_use_windows;
		} else if (cPlatform() == dbipLinux32
			|| cPlatform() == dbipLinux64) {
			return lng_settings_use_native_notifications;
		}
		return LangKey();
	}();
	const auto native = nativeNotificationsKey
		? addCheckbox(nativeNotificationsKey, Global::NativeNotifications())
		: nullptr;

	const auto advancedSlide = (cPlatform() != dbipMac)
		? container->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				container,
				object_ptr<Ui::VerticalLayout>(container)))
		: nullptr;
	const auto advancedWrap = advancedSlide
		? advancedSlide->entity()
		: nullptr;
	if (advancedWrap) {
		AddSkip(advancedWrap, st::settingsCheckboxesSkip);
		AddDivider(advancedWrap);
		AddSkip(advancedWrap, st::settingsCheckboxesSkip);
	}
	const auto advanced = advancedWrap
		? AddButton(
			advancedWrap,
			lng_settings_advanced_notifications,
			st::settingsButton).get()
		: nullptr;

	if (!name->entity()->checked()) {
		preview->hide(anim::type::instant);
	}
	if (!desktop->checked()) {
		name->hide(anim::type::instant);
		preview->hide(anim::type::instant);
	}
	if (native && advancedSlide && Global::NativeNotifications()) {
		advancedSlide->hide(anim::type::instant);
	}

	using Change = Window::Notifications::ChangeType;
	const auto changed = [](Change change) {
		Local::writeUserSettings();
		Auth().notifications().settingsChanged().notify(change);
	};
	base::ObservableViewer(
		desktop->checkedChanged
	) | rpl::filter([](bool checked) {
		return (checked != Global::DesktopNotify());
	}) | rpl::start_with_next([=](bool checked) {
		Global::SetDesktopNotify(checked);
		changed(Change::DesktopEnabled);
	}, desktop->lifetime());

	base::ObservableViewer(
		name->entity()->checkedChanged
	) | rpl::map([=](bool checked) {
		if (!checked) {
			return dbinvShowNothing;
		} else if (!preview->entity()->checked()) {
			return dbinvShowName;
		}
		return dbinvShowPreview;
	}) | rpl::filter([=](DBINotifyView value) {
		return (value != Global::NotifyView());
	}) | rpl::start_with_next([=](DBINotifyView value) {
		Global::SetNotifyView(value);
		changed(Change::ViewParams);
	}, name->lifetime());

	base::ObservableViewer(
		preview->entity()->checkedChanged
	) | rpl::map([=](bool checked) {
		if (checked) {
			return dbinvShowPreview;
		} else if (name->entity()->checked()) {
			return dbinvShowName;
		}
		return dbinvShowNothing;
	}) | rpl::filter([=](DBINotifyView value) {
		return (value != Global::NotifyView());
	}) | rpl::start_with_next([=](DBINotifyView value) {
		Global::SetNotifyView(value);
		changed(Change::ViewParams);
	}, preview->lifetime());

	base::ObservableViewer(
		sound->checkedChanged
	) | rpl::filter([](bool checked) {
		return (checked != Global::SoundNotify());
	}) | rpl::start_with_next([=](bool checked) {
		Global::SetSoundNotify(checked);
		changed(Change::SoundEnabled);
	}, sound->lifetime());

	base::ObservableViewer(
		muted->checkedChanged
	) | rpl::filter([](bool checked) {
		return (checked != Global::IncludeMuted());
	}) | rpl::start_with_next([=](bool checked) {
		Global::SetIncludeMuted(checked);
		changed(Change::IncludeMuted);
	}, muted->lifetime());

	base::ObservableViewer(
		Auth().notifications().settingsChanged()
	) | rpl::start_with_next([=](Change change) {
		if (change == Change::DesktopEnabled) {
			desktop->setChecked(Global::DesktopNotify());
			name->toggle(Global::DesktopNotify(), anim::type::normal);
			preview->toggle(
				Global::DesktopNotify() && name->entity()->checked(),
				anim::type::normal);
		} else if (change == Change::ViewParams) {
			preview->toggle(name->entity()->checked(), anim::type::normal);
		} else if (change == Change::SoundEnabled) {
			sound->setChecked(Global::SoundNotify());
		}
	}, desktop->lifetime());

	if (native) {
		base::ObservableViewer(
			native->checkedChanged
		) | rpl::filter([](bool checked) {
			return (checked != Global::NativeNotifications());
		}) | rpl::start_with_next([=](bool checked) {
			Global::SetNativeNotifications(checked);
			Local::writeUserSettings();

			Auth().notifications().createManager();

			if (advancedSlide) {
				advancedSlide->toggle(
					!Global::NativeNotifications(),
					anim::type::normal);
			}
		}, native->lifetime());
	}
	if (advanced) {
		advanced->addClickHandler([=] {
			Ui::show(Box<NotificationsBox>());
		});
	}
}

void SetupNotifications(not_null<Ui::VerticalLayout*> container) {
	AddSkip(container, st::settingsCheckboxesSkip);

	auto wrap = object_ptr<Ui::VerticalLayout>(container);
	SetupNotificationsContent(wrap.data());

	container->add(object_ptr<Ui::OverrideMargins>(
		container,
		std::move(wrap)));

	AddSkip(container, st::settingsCheckboxesSkip);
}

} // namespace

Notifications::Notifications(QWidget *parent, not_null<UserData*> self)
: Section(parent)
, _self(self) {
	setupContent();
}

void Notifications::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	SetupNotifications(content);

	Ui::ResizeFitChild(this, content);
}

} // namespace Settings
