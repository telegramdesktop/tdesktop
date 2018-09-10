/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_general.h"

#include "settings/settings_common.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/widgets/labels.h"
#include "boxes/local_storage_box.h"
#include "boxes/connection_box.h"
#include "boxes/about_box.h"
#include "boxes/confirm_box.h"
#include "info/profile/info_profile_button.h"
#include "info/profile/info_profile_values.h"
#include "data/data_session.h"
#include "platform/platform_specific.h"
#include "lang/lang_keys.h"
#include "core/update_checker.h"
#include "storage/localstorage.h"
#include "auth_session.h"
#include "layout.h"
#include "styles/style_settings.h"

namespace Settings {
namespace {

void SetupConnectionType(not_null<Ui::VerticalLayout*> container) {
#ifndef TDESKTOP_DISABLE_NETWORK_PROXY
	const auto connectionType = [] {
		const auto transport = MTP::dctransport();
		if (!Global::UseProxy()) {
			return transport.isEmpty()
				? lang(lng_connection_auto_connecting)
				: lng_connection_auto(lt_transport, transport);
		} else {
			return transport.isEmpty()
				? lang(lng_connection_proxy_connecting)
				: lng_connection_proxy(lt_transport, transport);
		}
	};
	const auto button = AddButtonWithLabel(
		container,
		lng_settings_connection_type,
		rpl::single(
			rpl::empty_value()
		) | rpl::then(base::ObservableViewer(
			Global::RefConnectionTypeChanged()
		)) | rpl::map(connectionType),
		st::settingsGeneralButton);
	button->addClickHandler([] {
		Ui::show(ProxiesBoxController::CreateOwningBox());
	});
#endif // TDESKTOP_DISABLE_NETWORK_PROXY
}

void SetupStorageAndConnection(not_null<Ui::VerticalLayout*> container) {
	AddDivider(container);
	AddSkip(container);

	AddButton(
		container,
		lng_settings_local_storage,
		st::settingsGeneralButton
	)->addClickHandler([] {
		LocalStorageBox::Show(&Auth().data().cache());
	});
	SetupConnectionType(container);

	AddSkip(container);
}

void SetupUpdate(not_null<Ui::VerticalLayout*> container) {
	if (Core::UpdaterDisabled()) {
		return;
	}

	AddDivider(container);
	AddSkip(container);

	const auto texts = Ui::AttachAsChild(
		container,
		rpl::event_stream<QString>());
	const auto downloading = Ui::AttachAsChild(
		container,
		rpl::event_stream<bool>());
	const auto version = lng_settings_current_version(
		lt_version,
		currentVersionText());
	const auto toggle = AddButton(
		container,
		lng_settings_update_automatically,
		st::settingsUpdateToggle);
	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		toggle.get(),
		texts->events(),
		st::settingsUpdateState);

	const auto check = container->add(object_ptr<Ui::SlideWrap<Button>>(
		container,
		object_ptr<Button>(
			container,
			Lang::Viewer(lng_settings_check_now),
			st::settingsGeneralButton)));
	const auto update = Ui::CreateChild<Button>(
		check->entity(),
		Lang::Viewer(lng_update_telegram) | Info::Profile::ToUpperValue(),
		st::settingsUpdate);
	update->hide();
	check->entity()->widthValue() | rpl::start_with_next([=](int width) {
		update->resizeToWidth(width);
		update->moveToLeft(0, 0);
	}, update->lifetime());

	AddSkip(container);

	rpl::combine(
		toggle->widthValue(),
		label->widthValue()
	) | rpl::start_with_next([=] {
		label->moveToLeft(
			st::settingsUpdateStatePosition.x(),
			st::settingsUpdateStatePosition.y());
	}, label->lifetime());
	label->setAttribute(Qt::WA_TransparentForMouseEvents);

	const auto showDownloadProgress = [=](int64 ready, int64 total) {
		texts->fire(lng_settings_downloading_update(
			lt_progress,
			formatDownloadText(ready, total)));
		downloading->fire(true);
	};
	const auto setDefaultStatus = [=](const Core::UpdateChecker &checker) {
		using State = Core::UpdateChecker::State;
		const auto state = checker.state();
		switch (state) {
		case State::Download:
			showDownloadProgress(checker.already(), checker.size());
			break;
		case State::Ready:
			texts->fire(lang(lng_settings_update_ready));
			update->show();
			break;
		default:
			texts->fire_copy(version);
			break;
		}
	};

	toggle->toggleOn(rpl::single(cAutoUpdate()));
	toggle->toggledValue(
	) | rpl::filter([](bool toggled) {
		return (toggled != cAutoUpdate());
	}) | rpl::start_with_next([=](bool toggled) {
		cSetAutoUpdate(toggled);

		Local::writeSettings();
		Core::UpdateChecker checker;
		if (cAutoUpdate()) {
			checker.start();
		} else {
			checker.stop();
		}
		setDefaultStatus(checker);
	}, toggle->lifetime());

	Core::UpdateChecker checker;
	check->toggleOn(rpl::combine(
		toggle->toggledValue(),
		downloading->events_starting_with(
			checker.state() == Core::UpdateChecker::State::Download)
	) | rpl::map([](bool check, bool downloading) {
		return check && !downloading;
	}));

	checker.checking() | rpl::start_with_next([=] {
		check->setAttribute(Qt::WA_TransparentForMouseEvents);
		texts->fire(lang(lng_settings_update_checking));
		downloading->fire(false);
	}, check->lifetime());
	checker.isLatest() | rpl::start_with_next([=] {
		check->setAttribute(Qt::WA_TransparentForMouseEvents, false);
		texts->fire(lang(lng_settings_latest_installed));
		downloading->fire(false);
	}, check->lifetime());
	checker.progress(
	) | rpl::start_with_next([=](Core::UpdateChecker::Progress progress) {
		showDownloadProgress(progress.already, progress.size);
	}, check->lifetime());
	checker.failed() | rpl::start_with_next([=] {
		check->setAttribute(Qt::WA_TransparentForMouseEvents, false);
		texts->fire(lang(lng_settings_update_fail));
		downloading->fire(false);
	}, check->lifetime());
	checker.ready() | rpl::start_with_next([=] {
		texts->fire(lang(lng_settings_update_ready));
		update->show();
		downloading->fire(false);
	}, check->lifetime());

	setDefaultStatus(checker);

	check->entity()->addClickHandler([] {
		Core::UpdateChecker checker;

		cSetLastUpdateCheck(0);
		checker.start();
	});
	update->addClickHandler([] {
		if (!Core::UpdaterDisabled()) {
			Core::checkReadyUpdate();
		}
		App::restart();
	});
}

void SetupTray(not_null<Ui::VerticalLayout*> container) {
	if (!cSupportTray() && cPlatform() != dbipWindows) {
		return;
	}

	AddDivider(container);
	AddSkip(container);

	const auto trayEnabler = Ui::AttachAsChild(
		container,
		rpl::event_stream<bool>());
	const auto trayEnabled = [] {
		const auto workMode = Global::WorkMode().value();
		return (workMode == dbiwmTrayOnly)
			|| (workMode == dbiwmWindowAndTray);
	};
	const auto tray = AddButton(
		container,
		lng_settings_workmode_tray,
		st::settingsGeneralButton
	)->toggleOn(trayEnabler->events_starting_with(trayEnabled()));

	const auto taskbarEnabled = [] {
		const auto workMode = Global::WorkMode().value();
		return (workMode == dbiwmWindowOnly)
			|| (workMode == dbiwmWindowAndTray);
	};
	const auto taskbarEnabler = Ui::AttachAsChild(
		container,
		rpl::event_stream<bool>());
	const auto taskbar = (cPlatform() == dbipWindows)
		? AddButton(
			container,
			lng_settings_workmode_window,
			st::settingsGeneralButton
		)->toggleOn(taskbarEnabler->events_starting_with(taskbarEnabled()))
		: nullptr;

	const auto updateWorkmode = [=] {
		const auto newMode = tray->toggled()
			? ((!taskbar || taskbar->toggled())
				? dbiwmWindowAndTray
				: dbiwmTrayOnly)
			: dbiwmWindowOnly;
		if ((newMode == dbiwmWindowAndTray || newMode == dbiwmTrayOnly)
			&& Global::WorkMode().value() != newMode) {
			cSetSeenTrayTooltip(false);
		}
		Global::RefWorkMode().set(newMode);
		Local::writeSettings();
	};

	tray->toggledValue(
	) | rpl::filter([=](bool checked) {
		return (checked != trayEnabled());
	}) | rpl::start_with_next([=](bool checked) {
		if (!checked && taskbar && !taskbar->toggled()) {
			taskbarEnabler->fire(true);
		} else {
			updateWorkmode();
		}
	}, tray->lifetime());

	if (taskbar) {
		taskbar->toggledValue(
		) | rpl::filter([=](bool checked) {
			return (checked != taskbarEnabled());
		}) | rpl::start_with_next([=](bool checked) {
			if (!checked && !tray->toggled()) {
				trayEnabler->fire(true);
			} else {
				updateWorkmode();
			}
		}, taskbar->lifetime());
	}

#ifndef OS_WIN_STORE
	if (cPlatform() == dbipWindows) {
		const auto autostart = AddButton(
			container,
			lng_settings_auto_start,
			st::settingsGeneralButton
		)->toggleOn(rpl::single(cAutoStart()));
		const auto minimized = container->add(
			object_ptr<Ui::SlideWrap<Button>>(
				container,
				object_ptr<Button>(
					container,
					Lang::Viewer(lng_settings_start_min),
					st::settingsGeneralButton)));
		const auto sendto = AddButton(
			container,
			lng_settings_add_sendto,
			st::settingsGeneralButton
		)->toggleOn(rpl::single(cSendToMenu()));

		const auto minimizedToggler = Ui::AttachAsChild(
			minimized,
			rpl::event_stream<bool>());
		const auto minimizedToggled = [] {
			return cStartMinimized() && !Global::LocalPasscode();
		};

		autostart->toggledValue(
		) | rpl::filter([](bool checked) {
			return (checked != cAutoStart());
		}) | rpl::start_with_next([=](bool checked) {
			cSetAutoStart(checked);
			psAutoStart(checked);
			if (checked) {
				Local::writeSettings();
			} else if (minimized->entity()->toggled()) {
				minimizedToggler->fire(false);
			} else {
				Local::writeSettings();
			}
		}, autostart->lifetime());

		minimized->entity()->toggleOn(
			minimizedToggler->events_starting_with(minimizedToggled()));
		minimized->toggleOn(autostart->toggledValue());
		minimized->entity()->toggledValue(
		) | rpl::filter([=](bool checked) {
			return (checked != minimizedToggled());
		}) | rpl::start_with_next([=](bool checked) {
			if (Global::LocalPasscode()) {
				minimizedToggler->fire(false);
				Ui::show(Box<InformBox>(
					lang(lng_error_start_minimized_passcoded)));
			} else {
				cSetStartMinimized(checked);
				Local::writeSettings();
			}
		}, minimized->lifetime());

		base::ObservableViewer(
			Global::RefLocalPasscodeChanged()
		) | rpl::start_with_next([=] {
			minimizedToggler->fire(minimizedToggled());
		}, minimized->lifetime());

		sendto->toggledValue(
		) | rpl::filter([](bool checked) {
			return (checked != cSendToMenu());
		}) | rpl::start_with_next([](bool checked) {
			cSetSendToMenu(checked);
			psSendToMenu(checked);
			Local::writeSettings();
		}, sendto->lifetime());
	}
#endif // OS_WIN_STORE

	AddSkip(container);
}

} // namespace

General::General(QWidget *parent, UserData *self)
: Section(parent)
, _self(self) {
	setupContent();
}

void General::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	AddSkip(content, st::settingsFirstDividerSkip);
	SetupUpdate(content);
	SetupTray(content);
	SetupStorageAndConnection(content);

	Ui::ResizeFitChild(this, content);
}

} // namespace Settings
