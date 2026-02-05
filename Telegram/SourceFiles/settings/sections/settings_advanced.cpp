/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/sections/settings_advanced.h"

#include "settings/settings_common_session.h"

#include "api/api_global_privacy.h"
#include "apiwrap.h"
#include "base/call_delayed.h"
#include "base/platform/base_platform_custom_app_icon.h"
#include "base/platform/base_platform_info.h"
#include "boxes/about_box.h"
#include "boxes/auto_download_box.h"
#include "boxes/connection_box.h"
#include "boxes/download_path_box.h"
#include "boxes/local_storage_box.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/file_utilities.h"
#include "core/launcher.h"
#include "core/update_checker.h"
#include "data/data_auto_download.h"
#include "export/export_manager.h"
#include "info/downloads/info_downloads_widget.h"
#include "info/info_memento.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "mtproto/facade.h"
#include "mtproto/mtp_instance.h"
#include "platform/platform_specific.h"
#include "settings/settings_builder.h"
#include "settings/sections/settings_main.h"
#include "settings/sections/settings_chat.h"
#include "settings/settings_experimental.h"
#include "settings/settings_power_saving.h"
#include "settings/sections/settings_privacy_security.h"
#include "storage/localstorage.h"
#include "storage/storage_domain.h"
#include "tray.h"
#include "ui/boxes/confirm_box.h"
#include "ui/boxes/single_choice_box.h"
#include "ui/gl/gl_detection.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/platform/ui_platform_window.h"
#include "ui/power_saving.h"
#include "ui/rp_widget.h"
#include "ui/text/format_values.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
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

using namespace Builder;

#if defined Q_OS_MAC && !defined OS_MAC_STORE
[[nodiscard]] const QImage &IconMacRound() {
	static const auto result = QImage(u":/gui/art/icon_round512@2x.png"_q);
	return result;
}
#endif // Q_OS_MAC && !OS_MAC_STORE

void BuildDataStorageSection(SectionBuilder &builder) {
	const auto controller = builder.controller();
	const auto container = builder.container();
	const auto session = builder.session();
	const auto account = &session->account();

	builder.addSkip();
	builder.addSubsectionTitle({
		.id = u"advanced/data_storage"_q,
		.title = tr::lng_settings_data_storage(),
		.keywords = { u"storage"_q, u"data"_q, u"download"_q, u"connection"_q },
	});

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

	builder.addButton({
		.id = u"advanced/connection_type"_q,
		.title = tr::lng_settings_connection_type(),
		.icon = { &st::menuIconNetwork },
		.label = rpl::merge(
			Core::App().settings().proxy().connectionTypeChanges(),
			tr::lng_connection_auto_connecting() | rpl::to_empty
		) | rpl::map(connectionType),
		.onClick = [=] {
			controller->window().show(
				ProxiesBoxController::CreateOwningBox(account));
		},
		.keywords = { u"connection"_q, u"proxy"_q, u"network"_q, u"vpn"_q },
	});

	const auto showDownloadPath = container
		? container->lifetime().make_state<rpl::variable<bool>>(
			!Core::App().settings().askDownloadPath())
		: nullptr;

	auto downloadLabel = Core::App().settings().downloadPathValue(
	) | rpl::map([](const QString &text) {
		if (text.isEmpty()) {
			return Core::App().canReadDefaultDownloadPath()
				? tr::lng_download_path_default(tr::now)
				: tr::lng_download_path_temp(tr::now);
		} else if (text == FileDialog::Tmp()) {
			return tr::lng_download_path_temp(tr::now);
		}
		return QDir::toNativeSeparators(text);
	});

	builder.addButton({
		.id = u"advanced/download_path"_q,
		.title = tr::lng_download_path(),
		.icon = { &st::menuIconShowInFolder },
		.label = std::move(downloadLabel),
		.onClick = [=] {
			controller->show(Box<DownloadPathBox>(controller));
		},
		.keywords = { u"download"_q, u"path"_q, u"folder"_q },
		.shown = showDownloadPath
			? showDownloadPath->value()
			: rpl::single(true) | rpl::type_erased,
	});

	builder.addButton({
		.id = u"advanced/storage"_q,
		.title = tr::lng_settings_manage_local_storage(),
		.icon = { &st::menuIconStorage },
		.onClick = [=] {
			LocalStorageBox::Show(controller);
		},
		.keywords = { u"storage"_q, u"cache"_q, u"local"_q },
	});

	builder.addButton({
		.id = u"advanced/downloads"_q,
		.title = tr::lng_downloads_section(),
		.icon = { &st::menuIconDownload },
		.onClick = [=] {
			if (controller) {
				controller->showSection(
					Info::Downloads::Make(controller->session().user()));
			}
		},
		.keywords = { u"downloads"_q, u"files"_q },
	});

	const auto askDownloadPath = builder.addButton({
		.id = u"advanced/ask_download"_q,
		.title = tr::lng_download_path_ask(),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(Core::App().settings().askDownloadPath()),
		.keywords = { u"download"_q, u"path"_q, u"ask"_q },
	});

	if (askDownloadPath) {
		askDownloadPath->toggledValue(
		) | rpl::filter([](bool checked) {
			return (checked != Core::App().settings().askDownloadPath());
		}) | rpl::on_next([=](bool checked) {
			Core::App().settings().setAskDownloadPath(checked);
			Core::App().saveSettingsDelayed();
			if (showDownloadPath) {
				*showDownloadPath = !checked;
			}
		}, askDownloadPath->lifetime());
	}

	builder.addSkip(st::settingsCheckboxesSkip);
}

void BuildAutoDownloadSection(SectionBuilder &builder) {
	const auto controller = builder.controller();
	const auto session = builder.session();
	builder.addDivider();
	builder.addSkip();
	builder.addSubsectionTitle({
		.id = u"advanced/auto_download"_q,
		.title = tr::lng_media_auto_settings(),
		.keywords = { u"auto"_q, u"download"_q, u"media"_q },
	});

	using Source = Data::AutoDownload::Source;

	builder.addButton({
		.id = u"advanced/auto_download_private"_q,
		.title = tr::lng_media_auto_in_private(),
		.icon = { &st::menuIconProfile },
		.onClick = [=] {
			controller->show(Box<AutoDownloadBox>(session, Source::User));
		},
		.keywords = { u"auto"_q, u"download"_q, u"private"_q, u"media"_q },
	});

	builder.addButton({
		.id = u"advanced/auto_download_groups"_q,
		.title = tr::lng_media_auto_in_groups(),
		.icon = { &st::menuIconGroups },
		.onClick = [=] {
			controller->show(Box<AutoDownloadBox>(session, Source::Group));
		},
		.keywords = { u"auto"_q, u"download"_q, u"groups"_q, u"media"_q },
	});

	builder.addButton({
		.id = u"advanced/auto_download_channels"_q,
		.title = tr::lng_media_auto_in_channels(),
		.icon = { &st::menuIconChannel },
		.onClick = [=] {
			controller->show(Box<AutoDownloadBox>(session, Source::Channel));
		},
		.keywords = { u"auto"_q, u"download"_q, u"channels"_q, u"media"_q },
	});

	builder.addSkip(st::settingsCheckboxesSkip);
}

void BuildWindowTitleSection(SectionBuilder &builder) {
	const auto settings = &Core::App().settings();

	builder.addDivider();
	builder.addSkip();
	builder.addSubsectionTitle({
		.id = u"advanced/window_title"_q,
		.title = tr::lng_settings_window_system(),
		.keywords = { u"window"_q, u"title"_q, u"frame"_q },
	});

	const auto content = [=] {
		return settings->windowTitleContent();
	};

	const auto showChatName = builder.addCheckbox({
		.id = u"advanced/title_chat_name"_q,
		.title = tr::lng_settings_title_chat_name(),
		.checked = !content().hideChatName,
		.keywords = { u"title"_q, u"chat"_q, u"name"_q },
	});
	if (showChatName) {
		showChatName->checkedChanges(
		) | rpl::filter([=](bool checked) {
			return (checked == content().hideChatName);
		}) | rpl::on_next([=](bool checked) {
			auto updated = content();
			updated.hideChatName = !checked;
			settings->setWindowTitleContent(updated);
			Core::App().saveSettingsDelayed();
		}, showChatName->lifetime());
	}

	const auto showAccountName = (Core::App().domain().accountsAuthedCount() > 1)
		? builder.addCheckbox({
			.id = u"advanced/title_account_name"_q,
			.title = tr::lng_settings_title_account_name(),
			.checked = !content().hideAccountName,
			.keywords = { u"title"_q, u"account"_q, u"name"_q },
		})
		: nullptr;
	if (showAccountName) {
		showAccountName->checkedChanges(
		) | rpl::filter([=](bool checked) {
			return (checked == content().hideAccountName);
		}) | rpl::on_next([=](bool checked) {
			auto updated = content();
			updated.hideAccountName = !checked;
			settings->setWindowTitleContent(updated);
			Core::App().saveSettingsDelayed();
		}, showAccountName->lifetime());
	}

	const auto showTotalUnread = builder.addCheckbox({
		.id = u"advanced/title_total_unread"_q,
		.title = tr::lng_settings_title_total_count(),
		.checked = !content().hideTotalUnread,
		.keywords = { u"title"_q, u"unread"_q, u"count"_q, u"badge"_q },
	});
	if (showTotalUnread) {
		showTotalUnread->checkedChanges(
		) | rpl::filter([=](bool checked) {
			return (checked == content().hideTotalUnread);
		}) | rpl::on_next([=](bool checked) {
			auto updated = content();
			updated.hideTotalUnread = !checked;
			settings->setWindowTitleContent(updated);
			Core::App().saveSettingsDelayed();
		}, showTotalUnread->lifetime());
	}

	if (Ui::Platform::NativeWindowFrameSupported()) {
		const auto nativeFrame = builder.addCheckbox({
			.id = u"advanced/native_frame"_q,
			.title = Platform::IsWayland()
				? tr::lng_settings_qt_frame()
				: tr::lng_settings_native_frame(),
			.checked = settings->nativeWindowFrame(),
			.keywords = { u"frame"_q, u"native"_q, u"window"_q, u"border"_q },
		});
		if (nativeFrame) {
			nativeFrame->checkedChanges(
			) | rpl::filter([](bool checked) {
				return (checked != Core::App().settings().nativeWindowFrame());
			}) | rpl::on_next([=](bool checked) {
				Core::App().settings().setNativeWindowFrame(checked);
				Core::App().saveSettingsDelayed();
			}, nativeFrame->lifetime());
		}
	}

	builder.addSkip();
}

void BuildSystemIntegrationSection(SectionBuilder &builder) {
	const auto controller = builder.controller();
	const auto settings = &Core::App().settings();

	builder.addDivider();
	builder.addSkip();
	builder.addSubsectionTitle({
		.id = u"advanced/system_integration"_q,
		.title = tr::lng_settings_system_integration(),
		.keywords = { u"system"_q, u"tray"_q, u"startup"_q, u"autostart"_q },
	});

	using WorkMode = Core::Settings::WorkMode;

	if (Platform::TrayIconSupported()) {
		const auto trayEnabled = [=] {
			const auto workMode = settings->workMode();
			return (workMode == WorkMode::TrayOnly)
				|| (workMode == WorkMode::WindowAndTray);
		};
		const auto tray = builder.addCheckbox({
			.id = u"advanced/tray"_q,
			.title = tr::lng_settings_workmode_tray(),
			.checked = trayEnabled(),
			.keywords = { u"tray"_q, u"icon"_q, u"system"_q },
		});

		const auto taskbarEnabled = [=] {
			const auto workMode = settings->workMode();
			return (workMode == WorkMode::WindowOnly)
				|| (workMode == WorkMode::WindowAndTray);
		};
		const auto taskbar = Platform::SkipTaskbarSupported()
			? builder.addCheckbox({
				.id = u"advanced/taskbar"_q,
				.title = tr::lng_settings_workmode_window(),
				.checked = taskbarEnabled(),
				.keywords = { u"taskbar"_q, u"window"_q },
			})
			: nullptr;

		const auto monochrome = Platform::HasMonochromeSetting()
			? builder.addCheckbox({
				.id = u"advanced/monochrome_icon"_q,
				.title = tr::lng_settings_monochrome_icon(),
				.checked = settings->trayIconMonochrome(),
				.keywords = { u"monochrome"_q, u"icon"_q, u"tray"_q },
				.shown = tray
					? tray->checkedValue()
					: rpl::single(trayEnabled()),
			})
			: nullptr;

		if (monochrome) {
			monochrome->checkedChanges(
			) | rpl::filter([=](bool value) {
				return (value != settings->trayIconMonochrome());
			}) | rpl::on_next([=](bool value) {
				settings->setTrayIconMonochrome(value);
				Core::App().saveSettingsDelayed();
			}, monochrome->lifetime());
		}

		const auto updateWorkmode = [=] {
			const auto newMode = (tray && tray->checked())
				? ((!taskbar || taskbar->checked())
					? WorkMode::WindowAndTray
					: WorkMode::TrayOnly)
				: WorkMode::WindowOnly;
			if ((newMode == WorkMode::WindowAndTray
				|| newMode == WorkMode::TrayOnly)
				&& settings->workMode() != newMode) {
				cSetSeenTrayTooltip(false);
			}
			settings->setWorkMode(newMode);
			Core::App().saveSettingsDelayed();
		};

		if (tray) {
			tray->checkedChanges(
			) | rpl::filter([=](bool checked) {
				return (checked != trayEnabled());
			}) | rpl::on_next([=](bool checked) {
				if (!checked && taskbar && !taskbar->checked()) {
					taskbar->setChecked(true);
				} else {
					updateWorkmode();
				}
			}, tray->lifetime());
		}

		if (taskbar) {
			taskbar->checkedChanges(
			) | rpl::filter([=](bool checked) {
				return (checked != taskbarEnabled());
			}) | rpl::on_next([=](bool checked) {
				if (!checked && tray && !tray->checked()) {
					tray->setChecked(true);
				} else {
					updateWorkmode();
				}
			}, taskbar->lifetime());
		}
	}

#ifdef Q_OS_MAC
	const auto warnBeforeQuit = builder.addCheckbox({
		.id = u"advanced/warn_before_quit"_q,
		.title = tr::lng_settings_mac_warn_before_quit(
			lt_text,
			rpl::single(Platform::ConfirmQuit::QuitKeysString())),
		.checked = settings->macWarnBeforeQuit(),
		.keywords = { u"quit"_q, u"warn"_q, u"close"_q },
	});
	if (warnBeforeQuit) {
		warnBeforeQuit->checkedChanges(
		) | rpl::filter([=](bool checked) {
			return (checked != settings->macWarnBeforeQuit());
		}) | rpl::on_next([=](bool checked) {
			settings->setMacWarnBeforeQuit(checked);
			Core::App().saveSettingsDelayed();
		}, warnBeforeQuit->lifetime());
	}

#ifndef OS_MAC_STORE
	const auto roundIconEnabled = [=] {
		const auto digest = base::Platform::CurrentCustomAppIconDigest();
		return digest && (settings->macRoundIconDigest() == digest);
	};
	const auto roundIcon = builder.addCheckbox({
		.id = u"advanced/round_icon"_q,
		.title = tr::lng_settings_mac_round_icon(),
		.checked = roundIconEnabled(),
		.keywords = { u"icon"_q, u"round"_q, u"dock"_q },
	});
	if (roundIcon) {
		roundIcon->checkedChanges(
		) | rpl::filter([=](bool checked) {
			return (checked != roundIconEnabled());
		}) | rpl::on_next([=](bool checked) {
			const auto digest = checked
				? base::Platform::SetCustomAppIcon(IconMacRound())
				: std::optional<uint64>();
			if (!checked) {
				base::Platform::ClearCustomAppIcon();
			}
			Window::OverrideApplicationIcon(checked ? IconMacRound() : QImage());
			Core::App().refreshApplicationIcon();
			settings->setMacRoundIconDigest(digest);
			Core::App().saveSettings();
		}, roundIcon->lifetime());
	}
#endif // OS_MAC_STORE
#elif defined Q_OS_WIN // Q_OS_MAC
	using Behavior = Core::Settings::CloseBehavior;

	const auto container = builder.container();
	const auto closeToTaskbarShown = container
		? container->lifetime().make_state<rpl::variable<bool>>(
			!Core::App().tray().has())
		: nullptr;

	if (closeToTaskbarShown) {
		settings->workModeValue(
		) | rpl::on_next([=](WorkMode) {
			*closeToTaskbarShown = !Core::App().tray().has();
		}, container->lifetime());
	}

	const auto closeToTaskbar = builder.addCheckbox({
		.id = u"advanced/close_to_taskbar"_q,
		.title = tr::lng_settings_close_to_taskbar(),
		.checked = settings->closeBehavior() == Behavior::CloseToTaskbar,
		.keywords = { u"close"_q, u"taskbar"_q, u"minimize"_q },
		.shown = closeToTaskbarShown
			? closeToTaskbarShown->value()
			: rpl::single(false),
	});
	if (closeToTaskbar) {
		closeToTaskbar->checkedChanges(
		) | rpl::map([=](bool checked) {
			return checked ? Behavior::CloseToTaskbar : Behavior::Quit;
		}) | rpl::filter([=](Behavior value) {
			return (settings->closeBehavior() != value);
		}) | rpl::on_next([=](Behavior value) {
			settings->setCloseBehavior(value);
			Local::writeSettings();
		}, closeToTaskbar->lifetime());
	}
#endif // Q_OS_MAC || Q_OS_WIN

	if (Platform::AutostartSupported()) {
		const auto minimizedToggled = [=] {
			return cStartMinimized()
				&& controller
				&& !controller->session().domain().local().hasLocalPasscode();
		};

		const auto autostart = builder.addCheckbox({
			.id = u"advanced/autostart"_q,
			.title = tr::lng_settings_auto_start(),
			.checked = cAutoStart(),
			.keywords = { u"autostart"_q, u"startup"_q, u"boot"_q },
		});

		const auto minimized = builder.addCheckbox({
			.id = u"advanced/start_minimized"_q,
			.title = tr::lng_settings_start_min(),
			.checked = minimizedToggled(),
			.keywords = { u"minimized"_q, u"startup"_q, u"hidden"_q },
			.shown = autostart
				? autostart->checkedValue()
				: rpl::single(cAutoStart()),
		});

		if (autostart) {
			autostart->checkedChanges(
			) | rpl::filter([](bool checked) {
				return (checked != cAutoStart());
			}) | rpl::on_next([=](bool checked) {
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
					if (enabled || !minimized || !minimized->checked()) {
						Local::writeSettings();
					} else if (minimized) {
						minimized->setChecked(false);
					}
				}));
			}, autostart->lifetime());

			if (controller) {
				Platform::AutostartRequestStateFromSystem(crl::guard(
					controller,
					[=](bool enabled) { autostart->setChecked(enabled); }));
			}
		}

		if (minimized && controller) {
			minimized->checkedChanges(
			) | rpl::filter([=](bool checked) {
				return (checked != minimizedToggled());
			}) | rpl::on_next([=](bool checked) {
				if (controller->session().domain().local().hasLocalPasscode()) {
					minimized->setChecked(false);
					controller->show(Ui::MakeInformBox(
						tr::lng_error_start_minimized_passcoded()));
				} else {
					cSetStartMinimized(checked);
					Local::writeSettings();
				}
			}, minimized->lifetime());

			controller->session().domain().local().localPasscodeChanged(
			) | rpl::on_next([=] {
				minimized->setChecked(minimizedToggled());
			}, minimized->lifetime());
		}
	}

	if (Platform::IsWindows() && !Platform::IsWindowsStoreBuild()) {
		const auto sendto = builder.addCheckbox({
			.id = u"advanced/sendto"_q,
			.title = tr::lng_settings_add_sendto(),
			.checked = cSendToMenu(),
			.keywords = { u"sendto"_q, u"send"_q, u"menu"_q, u"context"_q },
		});
		if (sendto) {
			sendto->checkedChanges(
			) | rpl::filter([](bool checked) {
				return (checked != cSendToMenu());
			}) | rpl::on_next([](bool checked) {
				cSetSendToMenu(checked);
				psSendToMenu(checked);
				Local::writeSettings();
			}, sendto->lifetime());
		}
	}

	builder.addSkip();
}

#ifdef DESKTOP_APP_USE_ANGLE
void BuildANGLEOption(SectionBuilder &builder) {
	const auto controller = builder.controller();
	using ANGLE = Ui::GL::ANGLE;

	const auto options = std::vector{
		tr::lng_settings_angle_backend_auto(tr::now),
		tr::lng_settings_angle_backend_d3d11(tr::now),
		tr::lng_settings_angle_backend_d3d9(tr::now),
		tr::lng_settings_angle_backend_d3d11on12(tr::now),
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
		}
		Unexpected("Ui::GL::CurrentANGLE value in BuildANGLEOption.");
	}();

	builder.addButton({
		.id = u"advanced/angle_backend"_q,
		.title = tr::lng_settings_angle_backend(),
		.st = &st::settingsButtonNoIcon,
		.label = rpl::single(options[backendIndex]),
		.onClick = [=] {
			controller->show(Box([=](not_null<Ui::GenericBox*> box) {
				const auto save = [=](int index) {
					if (index == backendIndex) {
						return;
					}
					const auto confirmed = crl::guard(box, [=] {
						const auto nowDisabled = (index == disabled);
						if (!nowDisabled) {
							Ui::GL::ChangeANGLE([&] {
								switch (index) {
								case 0: return ANGLE::Auto;
								case 1: return ANGLE::D3D11;
								case 2: return ANGLE::D3D9;
								case 3: return ANGLE::D3D11on12;
								}
								Unexpected("Index in BuildANGLEOption.");
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
		},
		.keywords = { u"angle"_q, u"opengl"_q, u"d3d"_q, u"graphics"_q },
	});
}
#else
void BuildOpenGLOption(SectionBuilder &builder) {
	const auto controller = builder.controller();
	const auto opengl = builder.addButton({
		.id = u"advanced/opengl"_q,
		.title = tr::lng_settings_enable_opengl(),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(!Core::App().settings().disableOpenGL()),
		.keywords = { u"opengl"_q, u"graphics"_q, u"gpu"_q },
	});

	if (opengl) {
		opengl->toggledValue(
		) | rpl::filter([](bool enabled) {
			return (enabled == Core::App().settings().disableOpenGL());
		}) | rpl::on_next([=](bool enabled) {
			const auto confirmed = crl::guard(opengl, [=] {
				Core::App().settings().setDisableOpenGL(!enabled);
				Local::writeSettings();
				Core::Restart();
			});
			controller->show(Ui::MakeConfirmBox({
				.text = tr::lng_settings_need_restart(),
				.confirmed = confirmed,
				.confirmText = tr::lng_settings_restart_now(),
			}));
		}, opengl->lifetime());
	}
}
#endif

void BuildPerformanceSection(SectionBuilder &builder) {
	const auto controller = builder.controller();
	builder.addDivider();
	builder.addSkip();
	builder.addSubsectionTitle({
		.id = u"advanced/performance"_q,
		.title = tr::lng_settings_performance(),
		.keywords = { u"performance"_q, u"power"_q, u"graphics"_q, u"hardware"_q },
	});

	builder.addButton({
		.id = u"advanced/power_saving"_q,
		.title = tr::lng_settings_power_menu(),
		.st = &st::settingsButtonNoIcon,
		.onClick = [=] {
			controller->window().show(Box(PowerSavingBox, PowerSaving::Flags()));
		},
		.keywords = { u"power"_q, u"saving"_q, u"battery"_q, u"animation"_q },
	});

	const auto hwAccel = builder.addButton({
		.id = u"advanced/hw_accel"_q,
		.title = tr::lng_settings_enable_hwaccel(),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(
			Core::App().settings().hardwareAcceleratedVideo()),
		.keywords = { u"hardware"_q, u"acceleration"_q, u"video"_q },
	});

	if (hwAccel) {
		hwAccel->toggledValue(
		) | rpl::filter([](bool enabled) {
			return (enabled !=
				Core::App().settings().hardwareAcceleratedVideo());
		}) | rpl::on_next([=](bool enabled) {
			Core::App().settings().setHardwareAcceleratedVideo(enabled);
			Core::App().saveSettingsDelayed();
		}, hwAccel->lifetime());
	}

#ifdef DESKTOP_APP_USE_ANGLE
	BuildANGLEOption(builder);
#else
	if constexpr (!Platform::IsMac()) {
		BuildOpenGLOption(builder);
	}
#endif

	builder.addSkip();
}

void BuildSpellcheckerSection(SectionBuilder &builder) {
#ifndef TDESKTOP_DISABLE_SPELLCHECK
	const auto controller = builder.controller();
	const auto session = builder.session();
	const auto settings = &Core::App().settings();
	const auto isSystem = Platform::Spellchecker::IsSystemSpellchecker();
	const auto container = builder.container();

	builder.addDivider();
	builder.addSkip();
	builder.addSubsectionTitle({
		.id = u"advanced/spellchecker"_q,
		.title = tr::lng_settings_spellchecker(),
		.keywords = { u"spellcheck"_q, u"spelling"_q, u"dictionary"_q },
	});

	const auto spellchecker = builder.addButton({
		.id = u"advanced/spellchecker_toggle"_q,
		.title = isSystem
			? tr::lng_settings_system_spellchecker()
			: tr::lng_settings_custom_spellchecker(),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(settings->spellcheckerEnabled()),
		.keywords = { u"spellcheck"_q, u"spelling"_q, u"dictionary"_q },
	});

	if (spellchecker) {
		spellchecker->toggledValue(
		) | rpl::filter([=](bool enabled) {
			return (enabled != settings->spellcheckerEnabled());
		}) | rpl::on_next([=](bool enabled) {
			settings->setSpellcheckerEnabled(enabled);
			Core::App().saveSettingsDelayed();
		}, spellchecker->lifetime());
	}

	const auto sliding = (!isSystem && container)
		? container->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				container,
				object_ptr<Ui::VerticalLayout>(container)))
		: nullptr;
	const auto inner = sliding ? sliding->entity() : nullptr;

	if (!isSystem) {
		const auto autoDownload = builder.addButton({
			.id = u"advanced/auto_download_dictionaries"_q,
			.title = tr::lng_settings_auto_download_dictionaries(),
			.st = &st::settingsButtonNoIcon,
			.container = inner,
			.toggled = rpl::single(settings->autoDownloadDictionaries()),
			.keywords = { u"dictionary"_q, u"download"_q, u"spellcheck"_q },
		});

		if (autoDownload) {
			autoDownload->toggledValue(
			) | rpl::filter([=](bool enabled) {
				return (enabled != settings->autoDownloadDictionaries());
			}) | rpl::on_next([=](bool enabled) {
				settings->setAutoDownloadDictionaries(enabled);
				Core::App().saveSettingsDelayed();
			}, autoDownload->lifetime());
		}

		builder.addButton({
			.id = u"advanced/manage_dictionaries"_q,
			.title = tr::lng_settings_manage_dictionaries(),
			.st = &st::settingsButtonNoIcon,
			.container = inner,
			.label = Spellchecker::ButtonManageDictsState(session),
			.onClick = [=] {
				controller->show(Box<Ui::ManageDictionariesBox>(session));
			},
			.keywords = { u"dictionary"_q, u"manage"_q, u"spellcheck"_q },
		});

		if (spellchecker && sliding) {
			spellchecker->toggledValue(
			) | rpl::on_next([=](bool enabled) {
				sliding->toggle(enabled, anim::type::normal);
			}, container->lifetime());
		}
	}

	builder.addSkip();
#endif // !TDESKTOP_DISABLE_SPELLCHECK
}

void BuildUpdateSection(SectionBuilder &builder, bool atTop) {
	if (!HasUpdate()) {
		return;
	}
	const auto container = builder.container();

	if (!atTop) {
		builder.addDivider();
	}
	builder.addSkip();
	builder.addSubsectionTitle({
		.id = u"advanced/version"_q,
		.title = tr::lng_settings_version_info(),
		.keywords = { u"version"_q, u"update"_q, u"check"_q },
	});

	const auto version = tr::lng_settings_current_version(
		tr::now,
		lt_version,
		currentVersionText());

	const auto texts = container
		? Ui::CreateChild<rpl::event_stream<QString>>(container)
		: nullptr;
	const auto downloading = container
		? Ui::CreateChild<rpl::event_stream<bool>>(container)
		: nullptr;

	const auto toggle = builder.addButton({
		.id = u"advanced/auto_update"_q,
		.title = tr::lng_settings_update_automatically(),
		.st = &st::settingsUpdateToggle,
		.toggled = rpl::single(cAutoUpdate()),
		.keywords = { u"update"_q, u"automatic"_q, u"version"_q },
	});

	if (toggle) {
		const auto label = Ui::CreateChild<Ui::FlatLabel>(
			toggle,
			texts->events(),
			st::settingsUpdateState);

		rpl::combine(
			toggle->widthValue(),
			label->widthValue()
		) | rpl::on_next([=] {
			label->moveToLeft(
				st::settingsUpdateStatePosition.x(),
				st::settingsUpdateStatePosition.y());
		}, label->lifetime());
		label->setAttribute(Qt::WA_TransparentForMouseEvents);
	}

	const auto options = container
		? container->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				container,
				object_ptr<Ui::VerticalLayout>(container)))
		: nullptr;
	const auto inner = options ? options->entity() : nullptr;

	const auto install = cAlphaVersion()
		? nullptr
		: builder.addButton({
			.id = u"advanced/install_beta"_q,
			.title = tr::lng_settings_install_beta(),
			.st = &st::settingsButtonNoIcon,
			.container = inner,
			.toggled = rpl::single(cInstallBetaVersion()),
			.keywords = { u"beta"_q, u"update"_q, u"version"_q },
		});

	const auto check = builder.addButton({
		.id = u"advanced/check_update"_q,
		.title = tr::lng_settings_check_now(),
		.st = &st::settingsButtonNoIcon,
		.container = inner,
		.onClick = [] {
			Core::UpdateChecker checker;
			cSetLastUpdateCheck(0);
			checker.start();
		},
		.keywords = { u"check"_q, u"update"_q, u"version"_q },
	});

	if (check && container) {
		const auto update = Ui::CreateChild<Ui::SettingsButton>(
			check,
			tr::lng_update_telegram(),
			st::settingsUpdate);
		update->hide();
		check->widthValue() | rpl::on_next([=](int width) {
			update->resizeToWidth(width);
			update->moveToLeft(0, 0);
		}, update->lifetime());

		const auto showDownloadProgress = [=](int64 ready, int64 total) {
			texts->fire(tr::lng_settings_downloading_update(
				tr::now,
				lt_progress,
				Ui::FormatDownloadText(ready, total)));
			downloading->fire(true);
		};
		const auto setDefaultStatus = [=](
				const Core::UpdateChecker &checker) {
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

		toggle->toggledValue(
		) | rpl::filter([](bool toggled) {
			return (toggled != cAutoUpdate());
		}) | rpl::on_next([=](bool toggled) {
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
			install->toggledValue(
			) | rpl::filter([](bool toggled) {
				return (toggled != cInstallBetaVersion());
			}) | rpl::on_next([=](bool toggled) {
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

		checker.checking() | rpl::on_next([=] {
			options->setAttribute(Qt::WA_TransparentForMouseEvents);
			texts->fire(tr::lng_settings_update_checking(tr::now));
			downloading->fire(false);
		}, options->lifetime());
		checker.isLatest() | rpl::on_next([=] {
			options->setAttribute(Qt::WA_TransparentForMouseEvents, false);
			texts->fire(tr::lng_settings_latest_installed(tr::now));
			downloading->fire(false);
		}, options->lifetime());
		checker.progress(
		) | rpl::on_next([=](Core::UpdateChecker::Progress progress) {
			showDownloadProgress(progress.already, progress.size);
		}, options->lifetime());
		checker.failed() | rpl::on_next([=] {
			options->setAttribute(Qt::WA_TransparentForMouseEvents, false);
			texts->fire(tr::lng_settings_update_fail(tr::now));
			downloading->fire(false);
		}, options->lifetime());
		checker.ready() | rpl::on_next([=] {
			options->setAttribute(Qt::WA_TransparentForMouseEvents, false);
			texts->fire(tr::lng_settings_update_ready(tr::now));
			update->show();
			downloading->fire(false);
		}, options->lifetime());

		setDefaultStatus(checker);

		update->setClickedCallback([] {
			if (!Core::UpdaterDisabled()) {
				Core::checkReadyUpdate();
			}
			Core::Restart();
		});
	}

	builder.addSkip();
	if (atTop) {
		builder.addDivider();
	}
}

void BuildExportSection(SectionBuilder &builder) {
	const auto controller = builder.controller();
	const auto session = builder.session();
	const auto showOther = builder.showOther();
	builder.addSkip();
	builder.addDivider();
	builder.addSkip();

	builder.addButton({
		.id = u"advanced/export"_q,
		.title = tr::lng_settings_export_data(),
		.icon = { &st::menuIconExport },
		.onClick = [=] {
			controller->window().hideSettingsAndLayer();
			base::call_delayed(
				st::boxDuration,
				session,
				[=] { Core::App().exportManager().start(session); });
		},
		.keywords = { u"export"_q, u"data"_q, u"backup"_q },
	});

	builder.addButton({
		.id = u"advanced/experimental"_q,
		.title = tr::lng_settings_experimental(),
		.icon = { &st::menuIconExperimental },
		.onClick = [showOther] { showOther(Experimental::Id()); },
		.keywords = { u"experimental"_q, u"beta"_q, u"features"_q },
	});
}

class Advanced : public Section<Advanced> {
public:
	Advanced(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent();

};

const auto kMeta = BuildHelper({
	.id = Advanced::Id(),
	.parentId = MainId(),
	.title = &tr::lng_settings_advanced,
	.icon = &st::menuIconManage,
}, [](SectionBuilder &builder) {
	const auto autoUpdate = cAutoUpdate();

	if (!autoUpdate) {
		BuildUpdateSection(builder, true);
	}
	BuildDataStorageSection(builder);
	BuildAutoDownloadSection(builder);
	BuildWindowTitleSection(builder);
	BuildSystemIntegrationSection(builder);
	BuildPerformanceSection(builder);
	BuildSpellcheckerSection(builder);
	if (autoUpdate) {
		BuildUpdateSection(builder, false);
	}
	BuildExportSection(builder);
});

const SectionBuildMethod kAdvancedSection = kMeta.build;

Advanced::Advanced(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

rpl::producer<QString> Advanced::title() {
	return tr::lng_settings_advanced();
}

void Advanced::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	build(content, kAdvancedSection);

	Ui::ResizeFitChild(this, content);
}

} // namespace

void SetupConnectionType(
		not_null<Window::Controller*> controller,
		not_null<::Main::Account*> account,
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
			tr::lng_connection_auto_connecting() | rpl::to_empty
		) | rpl::map([=] { return connectionType(); }),
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
	const auto toggle = container->add(object_ptr<Button>(
		container,
		tr::lng_settings_update_automatically(),
		st::settingsUpdateToggle));
	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		toggle,
		texts->events(),
		st::settingsUpdateState);

	const auto options = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	const auto inner = options->entity();
	const auto install = cAlphaVersion()
		? nullptr
		: inner->add(object_ptr<Button>(
			inner,
			tr::lng_settings_install_beta(),
			st::settingsButtonNoIcon));

	const auto check = inner->add(object_ptr<Button>(
		inner,
		tr::lng_settings_check_now(),
		st::settingsButtonNoIcon));
	const auto update = Ui::CreateChild<Button>(
		check,
		tr::lng_update_telegram(),
		st::settingsUpdate);
	update->hide();
	check->widthValue() | rpl::on_next([=](int width) {
		update->resizeToWidth(width);
		update->moveToLeft(0, 0);
	}, update->lifetime());

	rpl::combine(
		toggle->widthValue(),
		label->widthValue()
	) | rpl::on_next([=] {
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
	}) | rpl::on_next([=](bool toggled) {
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
		}) | rpl::on_next([=](bool toggled) {
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

	checker.checking() | rpl::on_next([=] {
		options->setAttribute(Qt::WA_TransparentForMouseEvents);
		texts->fire(tr::lng_settings_update_checking(tr::now));
		downloading->fire(false);
	}, options->lifetime());
	checker.isLatest() | rpl::on_next([=] {
		options->setAttribute(Qt::WA_TransparentForMouseEvents, false);
		texts->fire(tr::lng_settings_latest_installed(tr::now));
		downloading->fire(false);
	}, options->lifetime());
	checker.progress(
	) | rpl::on_next([=](Core::UpdateChecker::Progress progress) {
		showDownloadProgress(progress.already, progress.size);
	}, options->lifetime());
	checker.failed() | rpl::on_next([=] {
		options->setAttribute(Qt::WA_TransparentForMouseEvents, false);
		texts->fire(tr::lng_settings_update_fail(tr::now));
		downloading->fire(false);
	}, options->lifetime());
	checker.ready() | rpl::on_next([=] {
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
		}) | rpl::on_next([=](bool checked) {
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
			}) | rpl::on_next([=](bool checked) {
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
		}) | rpl::on_next([=](bool checked) {
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
		}) | rpl::on_next([=](bool checked) {
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

	const auto settings = &Core::App().settings();
	if (Platform::TrayIconSupported()) {
		const auto trayEnabled = [=] {
			const auto workMode = settings->workMode();
			return (workMode == WorkMode::TrayOnly)
				|| (workMode == WorkMode::WindowAndTray);
		};
		const auto tray = addCheckbox(
			tr::lng_settings_workmode_tray(),
			trayEnabled());
		const auto monochrome = Platform::HasMonochromeSetting()
			? addSlidingCheckbox(
				tr::lng_settings_monochrome_icon(),
				settings->trayIconMonochrome())
			: nullptr;
		if (monochrome) {
			monochrome->toggle(tray->checked(), anim::type::instant);

			monochrome->entity()->checkedChanges(
			) | rpl::filter([=](bool value) {
				return (value != settings->trayIconMonochrome());
			}) | rpl::on_next([=](bool value) {
				settings->setTrayIconMonochrome(value);
				Core::App().saveSettingsDelayed();
			}, monochrome->lifetime());
		}

		const auto taskbarEnabled = [=] {
			const auto workMode = settings->workMode();
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
				&& settings->workMode() != newMode) {
				cSetSeenTrayTooltip(false);
			}
			settings->setWorkMode(newMode);
			Core::App().saveSettingsDelayed();
		};

		tray->checkedChanges(
		) | rpl::filter([=](bool checked) {
			return (checked != trayEnabled());
		}) | rpl::on_next([=](bool checked) {
			if (!checked && taskbar && !taskbar->checked()) {
				taskbar->setChecked(true);
			} else {
				updateWorkmode();
			}
			if (monochrome) {
				monochrome->toggle(checked, anim::type::normal);
			}
		}, tray->lifetime());

		if (taskbar) {
			taskbar->checkedChanges(
			) | rpl::filter([=](bool checked) {
				return (checked != taskbarEnabled());
			}) | rpl::on_next([=](bool checked) {
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
		settings->macWarnBeforeQuit());
	warnBeforeQuit->checkedChanges(
	) | rpl::filter([=](bool checked) {
		return (checked != settings->macWarnBeforeQuit());
	}) | rpl::on_next([=](bool checked) {
		settings->setMacWarnBeforeQuit(checked);
		Core::App().saveSettingsDelayed();
	}, warnBeforeQuit->lifetime());

#ifndef OS_MAC_STORE
	const auto enabled = [=] {
		const auto digest = base::Platform::CurrentCustomAppIconDigest();
		return digest && (settings->macRoundIconDigest() == digest);
	};
	const auto roundIcon = addCheckbox(
		tr::lng_settings_mac_round_icon(),
		enabled());
	roundIcon->checkedChanges(
	) | rpl::filter([=](bool checked) {
		return (checked != enabled());
	}) | rpl::on_next([=](bool checked) {
		const auto digest = checked
			? base::Platform::SetCustomAppIcon(IconMacRound())
			: std::optional<uint64>();
		if (!checked) {
			base::Platform::ClearCustomAppIcon();
		}
		Window::OverrideApplicationIcon(checked ? IconMacRound() : QImage());
		Core::App().refreshApplicationIcon();
		settings->setMacRoundIconDigest(digest);
		Core::App().saveSettings();
	}, roundIcon->lifetime());
#endif // OS_MAC_STORE
#elif defined Q_OS_WIN // Q_OS_MAC
	using Behavior = Core::Settings::CloseBehavior;
	const auto closeToTaskbar = addSlidingCheckbox(
		tr::lng_settings_close_to_taskbar(),
		settings->closeBehavior() == Behavior::CloseToTaskbar);

	const auto closeToTaskbarShown = std::make_shared<
		rpl::variable<bool>
	>(false);
	settings->workModeValue(
	) | rpl::on_next([=](WorkMode workMode) {
		*closeToTaskbarShown = !Core::App().tray().has();
	}, closeToTaskbar->lifetime());

	closeToTaskbar->toggleOn(closeToTaskbarShown->value());
	closeToTaskbar->entity()->checkedChanges(
	) | rpl::map([=](bool checked) {
		return checked ? Behavior::CloseToTaskbar : Behavior::Quit;
	}) | rpl::filter([=](Behavior value) {
		return (settings->closeBehavior() != value);
	}) | rpl::on_next([=](Behavior value) {
		settings->setCloseBehavior(value);
		Local::writeSettings();
	}, closeToTaskbar->lifetime());
#endif // Q_OS_MAC || Q_OS_WIN

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
		}) | rpl::on_next([=](bool checked) {
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
		}) | rpl::on_next([=](bool checked) {
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
		) | rpl::on_next([=] {
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
		}) | rpl::on_next([](bool checked) {
			cSetSendToMenu(checked);
			psSendToMenu(checked);
			Local::writeSettings();
		}, sendto->lifetime());
	}
}

void SetupAnimations(
		not_null<Window::Controller*> window,
		not_null<Ui::VerticalLayout*> container) {
	container->add(object_ptr<Button>(
		container,
		tr::lng_settings_power_menu(),
		st::settingsButtonNoIcon
	))->setClickedCallback([=] {
		window->show(Box(PowerSavingBox, PowerSaving::Flags()));
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
	container->add(object_ptr<Button>(
		container,
		tr::lng_settings_always_in_archive(),
		st::settingsButtonNoIcon
	))->toggleOn(privacy->unarchiveOnNewMessage(
	) | rpl::map(
		rpl::mappers::_1 == Unarchive::None
	))->toggledChanges(
	) | rpl::filter([=](bool toggled) {
		const auto current = privacy->unarchiveOnNewMessageCurrent();
		state->foldersWrap->toggle(!toggled, anim::type::normal);
		return toggled != (current == Unarchive::None);
	}) | rpl::on_next([=](bool toggled) {
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

	state->folders = inner->add(object_ptr<Button>(
		inner,
		tr::lng_settings_always_in_archive(),
		st::settingsButtonNoIcon
	))->toggleOn(privacy->unarchiveOnNewMessage(
	) | rpl::map(
		rpl::mappers::_1 != Unarchive::AnyUnmuted
	));
	state->folders->toggledChanges(
	) | rpl::filter([=](bool toggled) {
		const auto current = privacy->unarchiveOnNewMessageCurrent();
		return toggled != (current != Unarchive::AnyUnmuted);
	}) | rpl::on_next([=](bool toggled) {
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

Type AdvancedId() {
	return Advanced::Id();
}

} // namespace Settings
