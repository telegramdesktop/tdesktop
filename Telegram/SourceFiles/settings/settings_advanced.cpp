/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_advanced.h"

#include "api/api_global_privacy.h"
#include "apiwrap.h"
#include "settings/settings_common.h"
#include "settings/settings_chat.h"
#include "settings/settings_power_saving.h"
#include "settings/settings_privacy_security.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/buttons.h"
#include "ui/gl/gl_detection.h"
#include "ui/layers/generic_box.h"
#include "ui/text/format_values.h"
#include "ui/boxes/single_choice_box.h"
#include "ui/painter.h"
#include "boxes/connection_box.h"
#include "boxes/about_box.h"
#include "ui/boxes/confirm_box.h"
#include "platform/platform_specific.h"
#include "ui/platform/ui_platform_window.h"
#include "base/platform/base_platform_custom_app_icon.h"
#include "base/platform/base_platform_info.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "lang/lang_keys.h"
#include "core/update_checker.h"
#include "core/launcher.h"
#include "core/application.h"
#include "tray.h"
#include "storage/localstorage.h"
#include "storage/storage_domain.h"
#include "data/data_session.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "mtproto/facade.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

#ifdef Q_OS_MAC
#include "base/platform/mac/base_confirm_quit.h"
#endif // Q_OS_MAC

#ifndef TDESKTOP_DISABLE_SPELLCHECK
#include "boxes/dictionaries_manager.h"
#include "chat_helpers/spellchecker_common.h"
#include "spellcheck/platform/platform_spellcheck.h"
#endif // !TDESKTOP_DISABLE_SPELLCHECK

namespace Settings {
namespace {

#if defined Q_OS_MAC && !defined OS_MAC_STORE
[[nodiscard]] const QImage &IconMacRound() {
	static const auto result = QImage(u":/gui/art/icon_round512@2x.png"_q);
	return result;
}
#endif // Q_OS_MAC && !OS_MAC_STORE

} // namespace

void SetupConnectionType(
		not_null<Window::Controller*> controller,
		not_null<Main::Account*> account,
		not_null<Ui::VerticalLayout*> container) {
	const auto connectionType = [=] {
		const auto transport = account->mtp().dctransport();
		if (!Core::App().settings().proxy().isEnabled()) {
			return transport.isEmpty()
				? tr::lng_connection_auto_connecting(tr::now)
				: tr::lng_connection_auto(tr::now, lt_transport, transport);
		} else {
			return transport.isEmpty()
				? tr::lng_connection_proxy_connecting(tr::now)
				: tr::lng_connection_proxy(tr::now, lt_transport, transport);
		}
	};
	const auto button = AddButtonWithLabel(
		container,
		tr::lng_settings_connection_type(),
		rpl::merge(
			Core::App().settings().proxy().connectionTypeChanges(),
			// Handle language switch.
			tr::lng_connection_auto_connecting() | rpl::to_empty
		) | rpl::map(connectionType),
		st::settingsButton,
		{ &st::menuIconNetwork });
	button->addClickHandler([=] {
		controller->show(ProxiesBoxController::CreateOwningBox(account));
	});
}

bool HasUpdate() {
	return !Core::UpdaterDisabled();
}

void SetupUpdate(not_null<Ui::VerticalLayout*> container) {
	if (!HasUpdate()) {
		return;
	}

	const auto texts = Ui::CreateChild<rpl::event_stream<QString>>(
		container.get());
	const auto downloading = Ui::CreateChild<rpl::event_stream<bool>>(
		container.get());
	const auto version = tr::lng_settings_current_version(
		tr::now,
		lt_version,
		currentVersionText());
	const auto toggle = AddButton(
		container,
		tr::lng_settings_update_automatically(),
		st::settingsUpdateToggle);
	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		toggle.get(),
		texts->events(),
		st::settingsUpdateState);

	const auto options = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	const auto inner = options->entity();
	const auto install = cAlphaVersion() ? nullptr : AddButton(
		inner,
		tr::lng_settings_install_beta(),
		st::settingsButtonNoIcon).get();

	const auto check = AddButton(
		inner,
		tr::lng_settings_check_now(),
		st::settingsButtonNoIcon);
	const auto update = Ui::CreateChild<Button>(
		check.get(),
		tr::lng_update_telegram(),
		st::settingsUpdate);
	update->hide();
	check->widthValue() | rpl::start_with_next([=](int width) {
		update->resizeToWidth(width);
		update->moveToLeft(0, 0);
	}, update->lifetime());

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
		texts->fire(tr::lng_settings_downloading_update(
			tr::now,
			lt_progress,
			Ui::FormatDownloadText(ready, total)));
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
			texts->fire(tr::lng_settings_update_ready(tr::now));
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
			setDefaultStatus(checker);
		}
	}, toggle->lifetime());

	if (install) {
		install->toggleOn(rpl::single(cInstallBetaVersion()));
		install->toggledValue(
		) | rpl::filter([](bool toggled) {
			return (toggled != cInstallBetaVersion());
		}) | rpl::start_with_next([=](bool toggled) {
			cSetInstallBetaVersion(toggled);
			Core::Launcher::Instance().writeInstallBetaVersionsSetting();

			Core::UpdateChecker checker;
			checker.stop();
			if (toggled) {
				cSetLastUpdateCheck(0);
			}
			checker.start();
		}, toggle->lifetime());
	}

	Core::UpdateChecker checker;
	options->toggleOn(rpl::combine(
		toggle->toggledValue(),
		downloading->events_starting_with(
			checker.state() == Core::UpdateChecker::State::Download)
	) | rpl::map([](bool check, bool downloading) {
		return check && !downloading;
	}));

	checker.checking() | rpl::start_with_next([=] {
		options->setAttribute(Qt::WA_TransparentForMouseEvents);
		texts->fire(tr::lng_settings_update_checking(tr::now));
		downloading->fire(false);
	}, options->lifetime());
	checker.isLatest() | rpl::start_with_next([=] {
		options->setAttribute(Qt::WA_TransparentForMouseEvents, false);
		texts->fire(tr::lng_settings_latest_installed(tr::now));
		downloading->fire(false);
	}, options->lifetime());
	checker.progress(
	) | rpl::start_with_next([=](Core::UpdateChecker::Progress progress) {
		showDownloadProgress(progress.already, progress.size);
	}, options->lifetime());
	checker.failed() | rpl::start_with_next([=] {
		options->setAttribute(Qt::WA_TransparentForMouseEvents, false);
		texts->fire(tr::lng_settings_update_fail(tr::now));
		downloading->fire(false);
	}, options->lifetime());
	checker.ready() | rpl::start_with_next([=] {
		options->setAttribute(Qt::WA_TransparentForMouseEvents, false);
		texts->fire(tr::lng_settings_update_ready(tr::now));
		update->show();
		downloading->fire(false);
	}, options->lifetime());

	setDefaultStatus(checker);

	check->addClickHandler([] {
		Core::UpdateChecker checker;

		cSetLastUpdateCheck(0);
		checker.start();
	});
	update->addClickHandler([] {
		if (!Core::UpdaterDisabled()) {
			Core::checkReadyUpdate();
		}
		Core::Restart();
	});
}

bool HasSystemSpellchecker() {
#ifdef TDESKTOP_DISABLE_SPELLCHECK
	return false;
#endif // TDESKTOP_DISABLE_SPELLCHECK
	return true;
}

void SetupSpellchecker(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
#ifndef TDESKTOP_DISABLE_SPELLCHECK
	const auto session = &controller->session();
	const auto settings = &Core::App().settings();
	const auto isSystem = Platform::Spellchecker::IsSystemSpellchecker();
	const auto button = AddButton(
		container,
		isSystem
			? tr::lng_settings_system_spellchecker()
			: tr::lng_settings_custom_spellchecker(),
		st::settingsButtonNoIcon
	)->toggleOn(
		rpl::single(settings->spellcheckerEnabled())
	);

	button->toggledValue(
	) | rpl::filter([=](bool enabled) {
		return (enabled != settings->spellcheckerEnabled());
	}) | rpl::start_with_next([=](bool enabled) {
		settings->setSpellcheckerEnabled(enabled);
		Core::App().saveSettingsDelayed();
	}, container->lifetime());

	if (isSystem) {
		return;
	}

	const auto sliding = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));

	AddButton(
		sliding->entity(),
		tr::lng_settings_auto_download_dictionaries(),
		st::settingsButtonNoIcon
	)->toggleOn(
		rpl::single(settings->autoDownloadDictionaries())
	)->toggledValue(
	) | rpl::filter([=](bool enabled) {
		return (enabled != settings->autoDownloadDictionaries());
	}) | rpl::start_with_next([=](bool enabled) {
		settings->setAutoDownloadDictionaries(enabled);
		Core::App().saveSettingsDelayed();
	}, sliding->entity()->lifetime());

	AddButtonWithLabel(
		sliding->entity(),
		tr::lng_settings_manage_dictionaries(),
		Spellchecker::ButtonManageDictsState(session),
		st::settingsButtonNoIcon
	)->addClickHandler([=] {
		controller->show(
			Box<Ui::ManageDictionariesBox>(&controller->session()));
	});

	button->toggledValue(
	) | rpl::start_with_next([=](bool enabled) {
		sliding->toggle(enabled, anim::type::normal);
	}, container->lifetime());
#endif // !TDESKTOP_DISABLE_SPELLCHECK
}

void SetupWindowTitleContent(
		Window::SessionController *controller,
		not_null<Ui::VerticalLayout*> container) {
	const auto checkbox = [&](rpl::producer<QString> &&label, bool checked) {
		return object_ptr<Ui::Checkbox>(
			container,
			std::move(label),
			checked,
			st::settingsCheckbox);
	};
	const auto addCheckbox = [&](
			rpl::producer<QString> &&label,
			bool checked) {
		return container->add(
			checkbox(std::move(label), checked),
			st::settingsCheckboxPadding);
	};
	const auto settings = &Core::App().settings();
	if (controller) {
		const auto content = [=] {
			return settings->windowTitleContent();
		};
		const auto showChatName = addCheckbox(
			tr::lng_settings_title_chat_name(),
			!content().hideChatName);
		showChatName->checkedChanges(
		) | rpl::filter([=](bool checked) {
			return (checked == content().hideChatName);
		}) | rpl::start_with_next([=](bool checked) {
			auto updated = content();
			updated.hideChatName = !checked;
			settings->setWindowTitleContent(updated);
			Core::App().saveSettingsDelayed();
		}, showChatName->lifetime());

		if (Core::App().domain().accountsAuthedCount() > 1) {
			const auto showAccountName = addCheckbox(
				tr::lng_settings_title_account_name(),
				!content().hideAccountName);
			showAccountName->checkedChanges(
			) | rpl::filter([=](bool checked) {
				return (checked == content().hideAccountName);
			}) | rpl::start_with_next([=](bool checked) {
				auto updated = content();
				updated.hideAccountName = !checked;
				settings->setWindowTitleContent(updated);
				Core::App().saveSettingsDelayed();
			}, showAccountName->lifetime());
		}

		const auto showTotalUnread = addCheckbox(
			tr::lng_settings_title_total_count(),
			!content().hideTotalUnread);
		showTotalUnread->checkedChanges(
		) | rpl::filter([=](bool checked) {
			return (checked == content().hideTotalUnread);
		}) | rpl::start_with_next([=](bool checked) {
			auto updated = content();
			updated.hideTotalUnread = !checked;
			settings->setWindowTitleContent(updated);
			Core::App().saveSettingsDelayed();
		}, showTotalUnread->lifetime());
	}

	if (Ui::Platform::NativeWindowFrameSupported()) {
		const auto nativeFrame = addCheckbox(
			Platform::IsWayland()
				? tr::lng_settings_qt_frame()
				: tr::lng_settings_native_frame(),
			Core::App().settings().nativeWindowFrame());

		nativeFrame->checkedChanges(
		) | rpl::filter([](bool checked) {
			return (checked != Core::App().settings().nativeWindowFrame());
		}) | rpl::start_with_next([=](bool checked) {
			Core::App().settings().setNativeWindowFrame(checked);
			Core::App().saveSettingsDelayed();
		}, nativeFrame->lifetime());
	}
}

void SetupSystemIntegrationContent(
		Window::SessionController *controller,
		not_null<Ui::VerticalLayout*> container) {
	using WorkMode = Core::Settings::WorkMode;

	const auto checkbox = [&](rpl::producer<QString> &&label, bool checked) {
		return object_ptr<Ui::Checkbox>(
			container,
			std::move(label),
			checked,
			st::settingsCheckbox);
	};
	const auto addCheckbox = [&](
			rpl::producer<QString> &&label,
			bool checked) {
		return container->add(
			checkbox(std::move(label), checked),
			st::settingsCheckboxPadding);
	};
	const auto addSlidingCheckbox = [&](
			rpl::producer<QString> &&label,
			bool checked) {
		return container->add(
			object_ptr<Ui::SlideWrap<Ui::Checkbox>>(
				container,
				checkbox(std::move(label), checked),
				st::settingsCheckboxPadding));
	};

	if (Platform::TrayIconSupported()) {
		const auto trayEnabled = [] {
			const auto workMode = Core::App().settings().workMode();
			return (workMode == WorkMode::TrayOnly)
				|| (workMode == WorkMode::WindowAndTray);
		};
		const auto tray = addCheckbox(
			tr::lng_settings_workmode_tray(),
			trayEnabled());

		const auto taskbarEnabled = [] {
			const auto workMode = Core::App().settings().workMode();
			return (workMode == WorkMode::WindowOnly)
				|| (workMode == WorkMode::WindowAndTray);
		};
		const auto taskbar = Platform::SkipTaskbarSupported()
			? addCheckbox(
				tr::lng_settings_workmode_window(),
				taskbarEnabled())
			: nullptr;

		const auto updateWorkmode = [=] {
			const auto newMode = tray->checked()
				? ((!taskbar || taskbar->checked())
					? WorkMode::WindowAndTray
					: WorkMode::TrayOnly)
				: WorkMode::WindowOnly;
			if ((newMode == WorkMode::WindowAndTray
				|| newMode == WorkMode::TrayOnly)
				&& Core::App().settings().workMode() != newMode) {
				cSetSeenTrayTooltip(false);
			}
			Core::App().settings().setWorkMode(newMode);
			Core::App().saveSettingsDelayed();
		};

		tray->checkedChanges(
		) | rpl::filter([=](bool checked) {
			return (checked != trayEnabled());
		}) | rpl::start_with_next([=](bool checked) {
			if (!checked && taskbar && !taskbar->checked()) {
				taskbar->setChecked(true);
			} else {
				updateWorkmode();
			}
		}, tray->lifetime());

		if (taskbar) {
			taskbar->checkedChanges(
			) | rpl::filter([=](bool checked) {
				return (checked != taskbarEnabled());
			}) | rpl::start_with_next([=](bool checked) {
				if (!checked && !tray->checked()) {
					tray->setChecked(true);
				} else {
					updateWorkmode();
				}
			}, taskbar->lifetime());
		}
	}

#ifdef Q_OS_MAC
	const auto warnBeforeQuit = addCheckbox(
		tr::lng_settings_mac_warn_before_quit(
			lt_text,
			rpl::single(Platform::ConfirmQuit::QuitKeysString())),
		Core::App().settings().macWarnBeforeQuit());
	warnBeforeQuit->checkedChanges(
	) | rpl::filter([=](bool checked) {
		return (checked != Core::App().settings().macWarnBeforeQuit());
	}) | rpl::start_with_next([=](bool checked) {
		Core::App().settings().setMacWarnBeforeQuit(checked);
		Core::App().saveSettingsDelayed();
	}, warnBeforeQuit->lifetime());

#ifndef OS_MAC_STORE
	const auto enabled = [] {
		const auto digest = base::Platform::CurrentCustomAppIconDigest();
		return digest && (Core::App().settings().macRoundIconDigest() == digest);
	};
	const auto roundIcon = addCheckbox(
		tr::lng_settings_mac_round_icon(),
		enabled());
	roundIcon->checkedChanges(
	) | rpl::filter([=](bool checked) {
		return (checked != enabled());
	}) | rpl::start_with_next([=](bool checked) {
		const auto digest = checked
			? base::Platform::SetCustomAppIcon(IconMacRound())
			: std::optional<uint64>();
		if (!checked) {
			base::Platform::ClearCustomAppIcon();
		}
		Window::OverrideApplicationIcon(checked ? IconMacRound() : QImage());
		Core::App().refreshApplicationIcon();
		Core::App().settings().setMacRoundIconDigest(digest);
		Core::App().saveSettings();
	}, roundIcon->lifetime());
#endif // OS_MAC_STORE
#endif // Q_OS_MAC

	if (!Platform::RunInBackground()) {
		const auto closeToTaskbar = addSlidingCheckbox(
			tr::lng_settings_close_to_taskbar(),
			Core::App().settings().closeToTaskbar());

		const auto closeToTaskbarShown = std::make_shared<rpl::variable<bool>>(false);
		Core::App().settings().workModeValue(
		) | rpl::start_with_next([=](WorkMode workMode) {
			*closeToTaskbarShown = !Core::App().tray().has();
		}, closeToTaskbar->lifetime());

		closeToTaskbar->toggleOn(closeToTaskbarShown->value());
		closeToTaskbar->entity()->checkedChanges(
		) | rpl::filter([=](bool checked) {
			return (checked != Core::App().settings().closeToTaskbar());
		}) | rpl::start_with_next([=](bool checked) {
			Core::App().settings().setCloseToTaskbar(checked);
			Local::writeSettings();
		}, closeToTaskbar->lifetime());
	}

	if (Platform::AutostartSupported() && controller) {
		const auto minimizedToggled = [=] {
			return cStartMinimized()
				&& !controller->session().domain().local().hasLocalPasscode();
		};

		const auto autostart = addCheckbox(
			tr::lng_settings_auto_start(),
			cAutoStart());
		const auto minimized = addSlidingCheckbox(
			tr::lng_settings_start_min(),
			minimizedToggled());

		autostart->checkedChanges(
		) | rpl::filter([](bool checked) {
			return (checked != cAutoStart());
		}) | rpl::start_with_next([=](bool checked) {
			const auto weak = base::make_weak(controller);
			cSetAutoStart(checked);
			Platform::AutostartToggle(checked, crl::guard(autostart, [=](
					bool enabled) {
				if (checked && !enabled && weak) {
					weak->window().showToast(
						Lang::Hard::AutostartEnableError());
				}
				Ui::PostponeCall(autostart, [=] {
					autostart->setChecked(enabled);
				});
				if (enabled || !minimized->entity()->checked()) {
					Local::writeSettings();
				} else {
					minimized->entity()->setChecked(false);
				}
			}));
		}, autostart->lifetime());

		Platform::AutostartRequestStateFromSystem(crl::guard(
			controller,
			[=](bool enabled) { autostart->setChecked(enabled); }));

		minimized->toggleOn(autostart->checkedValue());
		minimized->entity()->checkedChanges(
		) | rpl::filter([=](bool checked) {
			return (checked != minimizedToggled());
		}) | rpl::start_with_next([=](bool checked) {
			if (controller->session().domain().local().hasLocalPasscode()) {
				minimized->entity()->setChecked(false);
				controller->show(Ui::MakeInformBox(
					tr::lng_error_start_minimized_passcoded()));
			} else {
				cSetStartMinimized(checked);
				Local::writeSettings();
			}
		}, minimized->lifetime());

		controller->session().domain().local().localPasscodeChanged(
		) | rpl::start_with_next([=] {
			minimized->entity()->setChecked(minimizedToggled());
		}, minimized->lifetime());
	}

	if (Platform::IsWindows() && !Platform::IsWindowsStoreBuild()) {
		const auto sendto = addCheckbox(
			tr::lng_settings_add_sendto(),
			cSendToMenu());

		sendto->checkedChanges(
		) | rpl::filter([](bool checked) {
			return (checked != cSendToMenu());
		}) | rpl::start_with_next([](bool checked) {
			cSetSendToMenu(checked);
			psSendToMenu(checked);
			Local::writeSettings();
		}, sendto->lifetime());
	}
}

template <typename Fill>
void CheckNonEmptyOptions(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container,
		Fill fill) {
	auto wrap = object_ptr<Ui::VerticalLayout>(container);
	fill(controller, wrap.data());
	if (wrap->count() > 0) {
		container->add(object_ptr<Ui::OverrideMargins>(
			container,
			std::move(wrap)));
		AddSkip(container, st::settingsCheckboxesSkip);
	}
}

void SetupSystemIntegrationOptions(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
	CheckNonEmptyOptions(
		controller,
		container,
		SetupSystemIntegrationContent);
}

void SetupWindowTitleOptions(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
	CheckNonEmptyOptions(
		controller,
		container,
		SetupWindowTitleContent);
}

void SetupAnimations(
		not_null<Window::Controller*> window,
		not_null<Ui::VerticalLayout*> container) {
	AddButton(
		container,
		tr::lng_settings_power_menu(),
		st::settingsButtonNoIcon
	)->setClickedCallback([=] {
		window->show(Box(PowerSavingBox));
	});
}

void ArchiveSettingsBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller) {
	box->setTitle(tr::lng_settings_archive_title());
	box->setWidth(st::boxWideWidth);

	box->addButton(tr::lng_about_done(), [=] { box->closeBox(); });

	PreloadArchiveSettings(&controller->session());

	struct State {
		Ui::SlideWrap<Ui::VerticalLayout> *foldersWrap = nullptr;
		Ui::SettingsButton *folders = nullptr;
	};
	const auto state = box->lifetime().make_state<State>();
	const auto privacy = &controller->session().api().globalPrivacy();

	const auto container = box->verticalLayout();
	AddSkip(container);
	AddSubsectionTitle(container, tr::lng_settings_unmuted_chats());

	using Unarchive = Api::UnarchiveOnNewMessage;
	AddButton(
		container,
		tr::lng_settings_always_in_archive(),
		st::settingsButtonNoIcon
	)->toggleOn(privacy->unarchiveOnNewMessage(
	) | rpl::map(
		rpl::mappers::_1 == Unarchive::None
	))->toggledChanges(
	) | rpl::filter([=](bool toggled) {
		const auto current = privacy->unarchiveOnNewMessageCurrent();
		state->foldersWrap->toggle(!toggled, anim::type::normal);
		return toggled != (current == Unarchive::None);
	}) | rpl::start_with_next([=](bool toggled) {
		privacy->updateUnarchiveOnNewMessage(toggled
			? Unarchive::None
			: state->folders->toggled()
			? Unarchive::NotInFoldersUnmuted
			: Unarchive::AnyUnmuted);
	}, container->lifetime());

	AddSkip(container);
	AddDividerText(container, tr::lng_settings_unmuted_chats_about());

	state->foldersWrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	const auto inner = state->foldersWrap->entity();
	AddSkip(inner);
	AddSubsectionTitle(inner, tr::lng_settings_chats_from_folders());

	state->folders = AddButton(
		inner,
		tr::lng_settings_always_in_archive(),
		st::settingsButtonNoIcon
	)->toggleOn(privacy->unarchiveOnNewMessage(
	) | rpl::map(
		rpl::mappers::_1 != Unarchive::AnyUnmuted
	));
	state->folders->toggledChanges(
	) | rpl::filter([=](bool toggled) {
		const auto current = privacy->unarchiveOnNewMessageCurrent();
		return toggled != (current != Unarchive::AnyUnmuted);
	}) | rpl::start_with_next([=](bool toggled) {
		const auto current = privacy->unarchiveOnNewMessageCurrent();
		privacy->updateUnarchiveOnNewMessage(!toggled
			? Unarchive::AnyUnmuted
			: (current == Unarchive::AnyUnmuted)
			? Unarchive::NotInFoldersUnmuted
			: current);
	}, inner->lifetime());

	AddSkip(inner);
	AddDividerText(inner, tr::lng_settings_chats_from_folders_about());

	state->foldersWrap->toggle(
		privacy->unarchiveOnNewMessageCurrent() != Unarchive::None,
		anim::type::instant);

	SetupArchiveAndMute(controller, box->verticalLayout());
}

void PreloadArchiveSettings(not_null<::Main::Session*> session) {
	session->api().globalPrivacy().reload();
}

void SetupHardwareAcceleration(not_null<Ui::VerticalLayout*> container) {
	const auto settings = &Core::App().settings();
	AddButton(
		container,
		tr::lng_settings_enable_hwaccel(),
		st::settingsButtonNoIcon
	)->toggleOn(
		rpl::single(settings->hardwareAcceleratedVideo())
	)->toggledValue(
	) | rpl::filter([=](bool enabled) {
		return (enabled != settings->hardwareAcceleratedVideo());
	}) | rpl::start_with_next([=](bool enabled) {
		settings->setHardwareAcceleratedVideo(enabled);
		Core::App().saveSettingsDelayed();
	}, container->lifetime());
}

#ifdef Q_OS_WIN
void SetupANGLE(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
	using ANGLE = Ui::GL::ANGLE;
	const auto options = std::vector{
		tr::lng_settings_angle_backend_auto(tr::now),
		tr::lng_settings_angle_backend_d3d11(tr::now),
		tr::lng_settings_angle_backend_d3d9(tr::now),
		tr::lng_settings_angle_backend_d3d11on12(tr::now),
		//tr::lng_settings_angle_backend_opengl(tr::now),
		tr::lng_settings_angle_backend_disabled(tr::now),
	};
	const auto disabled = int(options.size()) - 1;
	const auto backendIndex = [=] {
		if (Core::App().settings().disableOpenGL()) {
			return disabled;
		} else switch (Ui::GL::CurrentANGLE()) {
		case ANGLE::Auto: return 0;
		case ANGLE::D3D11: return 1;
		case ANGLE::D3D9: return 2;
		case ANGLE::D3D11on12: return 3;
		//case ANGLE::OpenGL: return 4;
		}
		Unexpected("Ui::GL::CurrentANGLE value in SetupANGLE.");
	}();
	const auto button = AddButtonWithLabel(
		container,
		tr::lng_settings_angle_backend(),
		rpl::single(options[backendIndex]),
		st::settingsButtonNoIcon);
	button->addClickHandler([=] {
		controller->show(Box([=](not_null<Ui::GenericBox*> box) {
			const auto save = [=](int index) {
				if (index == backendIndex) {
					return;
				}
				const auto confirmed = crl::guard(button, [=] {
					const auto nowDisabled = (index == disabled);
					if (!nowDisabled) {
						Ui::GL::ChangeANGLE([&] {
							switch (index) {
							case 0: return ANGLE::Auto;
							case 1: return ANGLE::D3D11;
							case 2: return ANGLE::D3D9;
							case 3: return ANGLE::D3D11on12;
							//case 4: return ANGLE::OpenGL;
							}
							Unexpected("Index in SetupANGLE.");
						}());
					}
					const auto wasDisabled = (backendIndex == disabled);
					if (nowDisabled != wasDisabled) {
						Core::App().settings().setDisableOpenGL(nowDisabled);
						Local::writeSettings();
					}
					Core::Restart();
				});
				controller->show(Ui::MakeConfirmBox({
					.text = tr::lng_settings_need_restart(),
					.confirmed = confirmed,
					.confirmText = tr::lng_settings_restart_now(),
				}));
			};
			SingleChoiceBox(box, {
				.title = tr::lng_settings_angle_backend(),
				.options = options,
				.initialSelection = backendIndex,
				.callback = save,
			});
		}));
	});
}
#endif // Q_OS_WIN

void SetupOpenGL(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
	const auto toggles = container->lifetime().make_state<
		rpl::event_stream<bool>
	>();
	const auto button = AddButton(
		container,
		tr::lng_settings_enable_opengl(),
		st::settingsButtonNoIcon
	)->toggleOn(
		toggles->events_starting_with_copy(
			!Core::App().settings().disableOpenGL())
	);
	button->toggledValue(
	) | rpl::filter([](bool enabled) {
		return (enabled == Core::App().settings().disableOpenGL());
	}) | rpl::start_with_next([=](bool enabled) {
		const auto confirmed = crl::guard(button, [=] {
			Core::App().settings().setDisableOpenGL(!enabled);
			Local::writeSettings();
			Core::Restart();
		});
		const auto cancelled = crl::guard(button, [=](Fn<void()> close) {
			toggles->fire(!enabled);
			close();
		});
		controller->show(Ui::MakeConfirmBox({
			.text = tr::lng_settings_need_restart(),
			.confirmed = confirmed,
			.cancelled = cancelled,
			.confirmText = tr::lng_settings_restart_now(),
		}));
	}, container->lifetime());
}

void SetupPerformance(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
	SetupAnimations(&controller->window(), container);
	SetupHardwareAcceleration(container);
#ifdef Q_OS_WIN
	SetupANGLE(controller, container);
#else // Q_OS_WIN
	if constexpr (!Platform::IsMac()) {
		SetupOpenGL(controller, container);
	}
#endif // Q_OS_WIN
}

void SetupWindowTitle(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
	AddDivider(container);
	AddSkip(container);
	AddSubsectionTitle(container, tr::lng_settings_window_system());
	SetupWindowTitleOptions(controller, container);
	AddSkip(container);
}

void SetupSystemIntegration(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
	AddDivider(container);
	AddSkip(container);
	AddSubsectionTitle(container, tr::lng_settings_system_integration());
	SetupSystemIntegrationOptions(controller, container);
	AddSkip(container);
}

Advanced::Advanced(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent) {
	setupContent(controller);
}

rpl::producer<QString> Advanced::title() {
	return tr::lng_settings_advanced();
}

rpl::producer<Type> Advanced::sectionShowOther() {
	return _showOther.events();
}

void Advanced::setupContent(not_null<Window::SessionController*> controller) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	auto empty = true;
	const auto addDivider = [&] {
		if (empty) {
			empty = false;
		} else {
			AddDivider(content);
		}
	};
	const auto addUpdate = [&] {
		if (HasUpdate()) {
			addDivider();
			AddSkip(content);
			AddSubsectionTitle(content, tr::lng_settings_version_info());
			SetupUpdate(content);
			AddSkip(content);
		}
	};
	if (!cAutoUpdate()) {
		addUpdate();
	}
	addDivider();
	SetupDataStorage(controller, content);
	SetupAutoDownload(controller, content);
	SetupWindowTitle(controller, content);
	SetupSystemIntegration(controller, content);
	empty = false;

	AddDivider(content);
	AddSkip(content);
	AddSubsectionTitle(content, tr::lng_settings_performance());
	SetupPerformance(controller, content);
	AddSkip(content);

	if (HasSystemSpellchecker()) {
		AddDivider(content);
		AddSkip(content);
		AddSubsectionTitle(content, tr::lng_settings_spellchecker());
		SetupSpellchecker(controller, content);
		AddSkip(content);
	}

	if (cAutoUpdate()) {
		addUpdate();
	}

	AddSkip(content);
	AddDivider(content);
	AddSkip(content);
	SetupExport(controller, content, [=](Type type) {
		_showOther.fire_copy(type);
	});

	Ui::ResizeFitChild(this, content);
}

} // namespace Settings
