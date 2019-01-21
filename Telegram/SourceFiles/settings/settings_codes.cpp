/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_codes.h"

#include "platform/platform_specific.h"
#include "ui/toast/toast.h"
#include "mainwidget.h"
#include "data/data_session.h"
#include "storage/localstorage.h"
#include "boxes/confirm_box.h"
#include "lang/lang_cloud_manager.h"
#include "lang/lang_instance.h"
#include "messenger.h"
#include "mtproto/mtp_instance.h"
#include "mtproto/dc_options.h"
#include "core/file_utilities.h"
#include "core/update_checker.h"
#include "window/themes/window_theme.h"
#include "window/themes/window_theme_editor.h"
#include "media/media_audio_track.h"

namespace Settings {

auto GenerateCodes() {
	auto codes = std::map<QString, Fn<void()>>();
	codes.emplace(qsl("debugmode"), [] {
		QString text = Logs::DebugEnabled()
			? qsl("Do you want to disable DEBUG logs?")
			: qsl("Do you want to enable DEBUG logs?\n\n"
				"All network events will be logged.");
		Ui::show(Box<ConfirmBox>(text, [] {
			Messenger::Instance().onSwitchDebugMode();
		}));
	});
	codes.emplace(qsl("viewlogs"), [] {
		File::ShowInFolder(cWorkingDir() + "log.txt");
	});
	codes.emplace(qsl("testmode"), [] {
		auto text = cTestMode() ? qsl("Do you want to disable TEST mode?") : qsl("Do you want to enable TEST mode?\n\nYou will be switched to test cloud.");
		Ui::show(Box<ConfirmBox>(text, [] {
			Messenger::Instance().onSwitchTestMode();
		}));
	});
	if (!Core::UpdaterDisabled()) {
		codes.emplace(qsl("testupdate"), [] {
			Core::UpdateChecker().test();
		});
	}
	codes.emplace(qsl("loadlang"), [] {
		Lang::CurrentCloudManager().switchToLanguage({ qsl("#custom") });
	});
	codes.emplace(qsl("debugfiles"), [] {
		if (!Logs::DebugEnabled()) {
			return;
		}
		if (DebugLogging::FileLoader()) {
			Global::RefDebugLoggingFlags() &= ~DebugLogging::FileLoaderFlag;
		} else {
			Global::RefDebugLoggingFlags() |= DebugLogging::FileLoaderFlag;
		}
		Ui::show(Box<InformBox>(DebugLogging::FileLoader() ? qsl("Enabled file download logging") : qsl("Disabled file download logging")));
	});
	codes.emplace(qsl("crashplease"), [] {
		Unexpected("Crashed in Settings!");
	});
	codes.emplace(qsl("workmode"), [] {
		auto text = Global::DialogsModeEnabled() ? qsl("Disable work mode?") : qsl("Enable work mode?");
		Ui::show(Box<ConfirmBox>(text, [] {
			Messenger::Instance().onSwitchWorkMode();
		}));
	});
	codes.emplace(qsl("moderate"), [] {
		auto text = Global::ModerateModeEnabled() ? qsl("Disable moderate mode?") : qsl("Enable moderate mode?");
		Ui::show(Box<ConfirmBox>(text, []() {
			Global::SetModerateModeEnabled(!Global::ModerateModeEnabled());
			Local::writeUserSettings();
			Ui::hideLayer();
		}));
	});
	codes.emplace(qsl("getdifference"), [] {
		if (auto main = App::main()) {
			main->getDifference();
		}
	});
	codes.emplace(qsl("loadcolors"), [] {
		FileDialog::GetOpenPath(Messenger::Instance().getFileDialogParent(), "Open palette file", "Palette (*.tdesktop-palette)", [](const FileDialog::OpenResult &result) {
			if (!result.paths.isEmpty()) {
				Window::Theme::Apply(result.paths.front());
			}
		});
	});
	codes.emplace(qsl("edittheme"), [] {
		Window::Theme::Editor::Start();
	});
	codes.emplace(qsl("videoplayer"), [] {
		auto text = cUseExternalVideoPlayer() ? qsl("Use internal video player?") : qsl("Use external video player?");
		Ui::show(Box<ConfirmBox>(text, [] {
			cSetUseExternalVideoPlayer(!cUseExternalVideoPlayer());
			Local::writeUserSettings();
			Ui::hideLayer();
		}));
	});
	codes.emplace(qsl("endpoints"), [] {
		FileDialog::GetOpenPath(Messenger::Instance().getFileDialogParent(), "Open DC endpoints", "DC Endpoints (*.tdesktop-endpoints)", [](const FileDialog::OpenResult &result) {
			if (!result.paths.isEmpty()) {
				if (!Messenger::Instance().mtp()->dcOptions()->loadFromFile(result.paths.front())) {
					Ui::show(Box<InformBox>("Could not load endpoints :( Errors in 'log.txt'."));
				}
			}
		});
	});
	codes.emplace(qsl("registertg"), [] {
		Platform::RegisterCustomScheme();
		Ui::Toast::Show("Forced custom scheme register.");
	});
	codes.emplace(qsl("export"), [] {
		Auth().data().startExport();
	});

	auto audioFilters = qsl("Audio files (*.wav *.mp3);;") + FileDialog::AllFilesFilter();
	auto audioKeys = {
		qsl("msg_incoming"),
		qsl("call_incoming"),
		qsl("call_outgoing"),
		qsl("call_busy"),
		qsl("call_connect"),
		qsl("call_end"),
	};
	for (auto &key : audioKeys) {
		codes.emplace(key, [audioFilters, key] {
			if (!AuthSession::Exists()) {
				return;
			}

			FileDialog::GetOpenPath(Messenger::Instance().getFileDialogParent(), "Open audio file", audioFilters, [key](const FileDialog::OpenResult &result) {
				if (AuthSession::Exists() && !result.paths.isEmpty()) {
					auto track = Media::Audio::Current().createTrack();
					track->fillFromFile(result.paths.front());
					if (track->failed()) {
						Ui::show(Box<InformBox>("Could not audio :( Errors in 'log.txt'."));
					} else {
						Auth().settings().setSoundOverride(key, result.paths.front());
						Local::writeUserSettings();
					}
				}
			});
		});
	}
	codes.emplace(qsl("sounds_reset"), [] {
		if (AuthSession::Exists()) {
			Auth().settings().clearSoundOverrides();
			Local::writeUserSettings();
			Ui::show(Box<InformBox>("All sound overrides were reset."));
		}
	});
	return codes;
}

void CodesFeedString(const QString &text) {
	static const auto codes = GenerateCodes();
	static auto secret = QString();

	secret += text.toLower();
	int size = secret.size(), from = 0;
	while (size > from) {
		auto piece = secret.midRef(from);
		auto found = false;
		for (const auto &[key, method] : codes) {
			if (piece == key) {
				method();
				from = size;
				found = true;
				break;
			}
		}
		if (found) break;

		found = ranges::find_if(codes, [&](const auto &pair) {
			return pair.first.startsWith(piece);
		}) != end(codes);
		if (found) break;

		++from;
	}
	secret = (size > from) ? secret.mid(from) : QString();
}

} // namespace Settings
