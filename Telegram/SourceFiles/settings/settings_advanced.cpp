/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_advanced.h"

#include "settings/settings_common.h"
#include "settings/settings_chat.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/buttons.h"
#include "ui/gl/gl_detection.h"
#include "ui/text/text_utilities.h" // Ui::Text::ToUpper
#include "ui/text/format_values.h"
#include "ui/boxes/single_choice_box.h"
#include "boxes/connection_box.h"
#include "boxes/about_box.h"
#include "boxes/confirm_box.h"
#include "platform/platform_specific.h"
#include "platform/platform_window_title.h"
#include "base/platform/base_platform_info.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "lang/lang_keys.h"
#include "core/update_checker.h"
#include "core/application.h"
#include "storage/localstorage.h"
#include "storage/storage_domain.h"
#include "data/data_session.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "mtproto/facade.h"
#include "app.h"
#include "styles/style_settings.h"

#ifndef TDESKTOP_DISABLE_SPELLCHECK
#include "boxes/dictionaries_manager.h"
#include "chat_helpers/spellchecker_common.h"
#include "spellcheck/platform/platform_spellcheck.h"
#endif // !TDESKTOP_DISABLE_SPELLCHECK

namespace Settings {

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
		st::settingsButton);
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
		st::settingsButton).get();

	const auto check = AddButton(
		inner,
		tr::lng_settings_check_now(),
		st::settingsButton);
	const auto update = Ui::CreateChild<Button>(
		check.get(),
		tr::lng_update_telegram() | Ui::Text::ToUpper(),
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
			Core::App().writeInstallBetaVersionsSetting();

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
		App::restart();
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
		st::settingsButton
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
		st::settingsButton
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
		st::settingsButton
	)->addClickHandler([=] {
		controller->show(Box<Ui::ManageDictionariesBox>(controller));
	});

	button->toggledValue(
	) | rpl::start_with_next([=](bool enabled) {
		sliding->toggle(enabled, anim::type::normal);
	}, container->lifetime());
#endif // !TDESKTOP_DISABLE_SPELLCHECK
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
			Local::writeSettings();
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
	if (Platform::AllowNativeWindowFrameToggle()) {
		const auto nativeFrame = addCheckbox(
			tr::lng_settings_native_frame(),
			Core::App().settings().nativeWindowFrame());

		nativeFrame->checkedChanges(
		) | rpl::filter([](bool checked) {
			return (checked != Core::App().settings().nativeWindowFrame());
		}) | rpl::start_with_next([=](bool checked) {
			Core::App().settings().setNativeWindowFrame(checked);
			Core::App().saveSettingsDelayed();
		}, nativeFrame->lifetime());
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
			cSetAutoStart(checked);
			psAutoStart(checked);
			if (checked) {
				Local::writeSettings();
			} else if (minimized->entity()->checked()) {
				minimized->entity()->setChecked(false);
			} else {
				Local::writeSettings();
			}
		}, autostart->lifetime());

		minimized->toggleOn(autostart->checkedValue());
		minimized->entity()->checkedChanges(
		) | rpl::filter([=](bool checked) {
			return (checked != minimizedToggled());
		}) | rpl::start_with_next([=](bool checked) {
			if (controller->session().domain().local().hasLocalPasscode()) {
				minimized->entity()->setChecked(false);
				controller->show(Box<InformBox>(
					tr::lng_error_start_minimized_passcoded(tr::now)));
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

void SetupSystemIntegrationOptions(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
	auto wrap = object_ptr<Ui::VerticalLayout>(container);
	SetupSystemIntegrationContent(controller, wrap.data());
	if (wrap->count() > 0) {
		container->add(object_ptr<Ui::OverrideMargins>(
			container,
			std::move(wrap)));

		AddSkip(container, st::settingsCheckboxesSkip);
	}
}

void SetupAnimations(not_null<Ui::VerticalLayout*> container) {
	AddButton(
		container,
		tr::lng_settings_enable_animations(),
		st::settingsButton
	)->toggleOn(
		rpl::single(!anim::Disabled())
	)->toggledValue(
	) | rpl::filter([](bool enabled) {
		return (enabled == anim::Disabled());
	}) | rpl::start_with_next([](bool enabled) {
		anim::SetDisabled(!enabled);
		Local::writeSettings();
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
		tr::lng_settings_angle_backend_opengl(tr::now),
		tr::lng_settings_angle_backend_disabled(tr::now),
	};
	const auto backendIndex = [] {
		if (Core::App().settings().disableOpenGL()) {
			return 5;
		} else switch (Ui::GL::CurrentANGLE()) {
		case ANGLE::Auto: return 0;
		case ANGLE::D3D11: return 1;
		case ANGLE::D3D9: return 2;
		case ANGLE::D3D11on12: return 3;
		case ANGLE::OpenGL: return 4;
		}
		Unexpected("Ui::GL::CurrentANGLE value in SetupANGLE.");
	}();
	const auto button = AddButtonWithLabel(
		container,
		tr::lng_settings_angle_backend(),
		rpl::single(options[backendIndex]),
		st::settingsButton);
	button->addClickHandler([=] {
		controller->show(Box([=](not_null<Ui::GenericBox*> box) {
			const auto save = [=](int index) {
				if (index == backendIndex) {
					return;
				}
				const auto confirmed = crl::guard(button, [=] {
					const auto nowDisabled = (index == 5);
					if (!nowDisabled) {
						Ui::GL::ChangeANGLE([&] {
							switch (index) {
							case 0: return ANGLE::Auto;
							case 1: return ANGLE::D3D11;
							case 2: return ANGLE::D3D9;
							case 3: return ANGLE::D3D11on12;
							case 4: return ANGLE::OpenGL;
							}
							Unexpected("Index in SetupANGLE.");
						}());
					}
					const auto wasDisabled = (backendIndex == 5);
					if (nowDisabled != wasDisabled) {
						Core::App().settings().setDisableOpenGL(nowDisabled);
						Local::writeSettings();
					}
					App::restart();
				});
				controller->show(Box<ConfirmBox>(
					tr::lng_settings_need_restart(tr::now),
					tr::lng_settings_restart_now(tr::now),
					confirmed));
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
		st::settingsButton
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
			App::restart();
		});
		const auto cancelled = crl::guard(button, [=] {
			toggles->fire(!enabled);
		});
		controller->show(Box<ConfirmBox>(
			tr::lng_settings_need_restart(tr::now),
			tr::lng_settings_restart_now(tr::now),
			confirmed,
			cancelled));
	}, container->lifetime());
}

void SetupPerformance(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
	SetupAnimations(container);
#ifdef Q_OS_WIN
	SetupANGLE(controller, container);
#else // Q_OS_WIN
	if constexpr (!Platform::IsMac()) {
		SetupOpenGL(controller, container);
	}
#endif // Q_OS_WIN
}

void SetupSystemIntegration(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container,
		Fn<void(Type)> showOther) {
	AddDivider(container);
	AddSkip(container);
	AddSubsectionTitle(container, tr::lng_settings_system_integration());
	AddButton(
		container,
		tr::lng_settings_section_call_settings(),
		st::settingsButton
	)->addClickHandler([=] {
		showOther(Type::Calls);
	});
	SetupSystemIntegrationOptions(controller, container);
	AddSkip(container);
}

Advanced::Advanced(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent) {
	setupContent(controller);
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
	AddSkip(content);
	AddSubsectionTitle(content, tr::lng_settings_network_proxy());
	SetupConnectionType(
		&controller->window(),
		&controller->session().account(),
		content);
	AddSkip(content);
	SetupDataStorage(controller, content);
	SetupAutoDownload(controller, content);
	SetupSystemIntegration(controller, content, [=](Type type) {
		_showOther.fire_copy(type);
	});

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

	Ui::ResizeFitChild(this, content);
}

} // namespace Settings
