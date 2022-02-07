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
#include "mainwindow.h"
#include "data/data_session.h"
#include "data/data_cloud_themes.h"
#include "main/main_session.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "ui/boxes/confirm_box.h"
#include "lang/lang_cloud_manager.h"
#include "lang/lang_instance.h"
#include "core/application.h"
#include "mtproto/mtp_instance.h"
#include "mtproto/mtproto_dc_options.h"
#include "core/file_utilities.h"
#include "core/update_checker.h"
#include "window/themes/window_theme.h"
#include "window/themes/window_theme_editor.h"
#include "window/window_session_controller.h"
#include "media/audio/media_audio_track.h"
#include "settings/settings_common.h"
#include "api/api_updates.h"
#include "base/qt/qt_common_adapters.h"
#include "base/custom_app_icon.h"

#include "zlib.h"

namespace Settings {
namespace {

using SessionController = Window::SessionController;

[[nodiscard]] QByteArray UnpackRawGzip(const QByteArray &bytes) {
	z_stream stream;
	stream.zalloc = nullptr;
	stream.zfree = nullptr;
	stream.opaque = nullptr;
	stream.avail_in = 0;
	stream.next_in = nullptr;
	int res = inflateInit2(&stream, -MAX_WBITS);
	if (res != Z_OK) {
		return QByteArray();
	}
	const auto guard = gsl::finally([&] { inflateEnd(&stream); });

	auto result = QByteArray(1024 * 1024, char(0));
	stream.avail_in = bytes.size();
	stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(bytes.data()));
	stream.avail_out = 0;
	while (!stream.avail_out) {
		stream.avail_out = result.size();
		stream.next_out = reinterpret_cast<Bytef*>(result.data());
		int res = inflate(&stream, Z_NO_FLUSH);
		if (res != Z_OK && res != Z_STREAM_END) {
			return QByteArray();
		} else if (!stream.avail_out) {
			return QByteArray();
		}
	}
	result.resize(result.size() - stream.avail_out);
	return result;
}

auto GenerateCodes() {
	auto codes = std::map<QString, Fn<void(SessionController*)>>();
	codes.emplace(qsl("debugmode"), [](SessionController *window) {
		QString text = Logs::DebugEnabled()
			? qsl("Do you want to disable DEBUG logs?")
			: qsl("Do you want to enable DEBUG logs?\n\n"
				"All network events will be logged.");
		Ui::show(Box<Ui::ConfirmBox>(text, [] {
			Core::App().switchDebugMode();
		}));
	});
	codes.emplace(qsl("viewlogs"), [](SessionController *window) {
		File::ShowInFolder(cWorkingDir() + "log.txt");
	});
	if (!Core::UpdaterDisabled()) {
		codes.emplace(qsl("testupdate"), [](SessionController *window) {
			Core::UpdateChecker().test();
		});
	}
	codes.emplace(qsl("loadlang"), [](SessionController *window) {
		Lang::CurrentCloudManager().switchToLanguage({ qsl("#custom") });
	});
	codes.emplace(qsl("crashplease"), [](SessionController *window) {
		Unexpected("Crashed in Settings!");
	});
	codes.emplace(qsl("moderate"), [](SessionController *window) {
		auto text = Core::App().settings().moderateModeEnabled() ? qsl("Disable moderate mode?") : qsl("Enable moderate mode?");
		Ui::show(Box<Ui::ConfirmBox>(text, [=] {
			Core::App().settings().setModerateModeEnabled(!Core::App().settings().moderateModeEnabled());
			Core::App().saveSettingsDelayed();
			Ui::hideLayer();
		}));
	});
	codes.emplace(qsl("getdifference"), [](SessionController *window) {
		if (window) {
			window->session().updates().getDifference();
		}
	});
	codes.emplace(qsl("loadcolors"), [](SessionController *window) {
		FileDialog::GetOpenPath(Core::App().getFileDialogParent(), "Open palette file", "Palette (*.tdesktop-palette)", [](const FileDialog::OpenResult &result) {
			if (!result.paths.isEmpty()) {
				Window::Theme::Apply(result.paths.front());
			}
		});
	});
	codes.emplace(qsl("videoplayer"), [](SessionController *window) {
		if (!window) {
			return;
		}
		auto text = cUseExternalVideoPlayer() ? qsl("Use internal video player?") : qsl("Use external video player?");
		Ui::show(Box<Ui::ConfirmBox>(text, [=] {
			cSetUseExternalVideoPlayer(!cUseExternalVideoPlayer());
			window->session().saveSettingsDelayed();
			Ui::hideLayer();
		}));
	});
	codes.emplace(qsl("endpoints"), [](SessionController *window) {
		if (!Core::App().domain().started()) {
			return;
		}
		const auto weak = window
			? base::make_weak(&window->session().account())
			: nullptr;
		FileDialog::GetOpenPath(Core::App().getFileDialogParent(), "Open DC endpoints", "DC Endpoints (*.tdesktop-endpoints)", [weak](const FileDialog::OpenResult &result) {
			if (!result.paths.isEmpty()) {
				const auto loadFor = [&](not_null<Main::Account*> account) {
					if (!account->mtp().dcOptions().loadFromFile(result.paths.front())) {
						Ui::show(Box<Ui::InformBox>("Could not load endpoints"
							" :( Errors in 'log.txt'."));
					}
				};
				if (const auto strong = weak.get()) {
					loadFor(strong);
				} else {
					for (const auto &pair : Core::App().domain().accounts()) {
						loadFor(pair.account.get());
					}
				}
			}
		});
	});
	codes.emplace(qsl("testmode"), [](SessionController *window) {
		auto &domain = Core::App().domain();
		if (domain.started()
			&& (domain.accounts().size() == 1)
			&& !domain.active().sessionExists()) {
			const auto environment = domain.active().mtp().environment();
			domain.addActivated([&] {
				return (environment == MTP::Environment::Production)
					? MTP::Environment::Test
					: MTP::Environment::Production;
			}());
			Ui::Toast::Show((environment == MTP::Environment::Production)
				? "Switched to the test environment."
				: "Switched to the production environment.");
		}
	});
	codes.emplace(qsl("folders"), [](SessionController *window) {
		if (window) {
			window->showSettings(Settings::Type::Folders);
		}
	});
	codes.emplace(qsl("registertg"), [](SessionController *window) {
		Core::Application::RegisterUrlScheme();
		Ui::Toast::Show("Forced custom scheme register.");
	});

#if defined Q_OS_WIN || defined Q_OS_MAC
	codes.emplace(qsl("freetype"), [](SessionController *window) {
		auto text = cUseFreeType()
#ifdef Q_OS_WIN
			? qsl("Switch font engine to GDI?")
#else // Q_OS_WIN
			? qsl("Switch font engine to Cocoa?")
#endif // !Q_OS_WIN
			: qsl("Switch font engine to FreeType?");

		Ui::show(Box<Ui::ConfirmBox>(text, [] {
			Core::App().switchFreeType();
		}));
	});
#endif // Q_OS_WIN || Q_OS_MAC

#if defined Q_OS_UNIX && !defined Q_OS_MAC
	codes.emplace(qsl("installlauncher"), [](SessionController *window) {
		Platform::InstallLauncher(true);
		Ui::Toast::Show("Forced launcher installation.");
	});
#endif // Q_OS_UNIX && !Q_OS_MAC

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
		codes.emplace(key, [=](SessionController *window) {
			FileDialog::GetOpenPath(Core::App().getFileDialogParent(), "Open audio file", audioFilters, [=](const FileDialog::OpenResult &result) {
				if (!result.paths.isEmpty()) {
					auto track = Media::Audio::Current().createTrack();
					track->fillFromFile(result.paths.front());
					if (track->failed()) {
						Ui::show(Box<Ui::InformBox>(
							"Could not audio :( Errors in 'log.txt'."));
					} else {
						Core::App().settings().setSoundOverride(
							key,
							result.paths.front());
						Core::App().saveSettingsDelayed();
					}
				}
			});
		});
	}
	codes.emplace(qsl("sounds_reset"), [](SessionController *window) {
		Core::App().settings().clearSoundOverrides();
		Core::App().saveSettingsDelayed();
		Ui::show(Box<Ui::InformBox>("All sound overrides were reset."));
	});
	codes.emplace(qsl("unpacklog"), [](SessionController *window) {
		FileDialog::GetOpenPath(Core::App().getFileDialogParent(), "Open crash log file", "Crash dump (*.txt)", [=](const FileDialog::OpenResult &result) {
			if (result.paths.isEmpty()) {
				return;
			}
			auto f = QFile(result.paths.front());
			if (!f.open(QIODevice::ReadOnly)) {
				Ui::Toast::Show("Could not open log :(");
				return;
			}
			const auto all = f.readAll();
			const auto log = all.indexOf("Log: ");
			if (log < 0) {
				Ui::Toast::Show("Could not find log :(");
				return;
			}
			const auto base = all.mid(log + 5);
			const auto end = base.indexOf('\n');
			if (end <= 0) {
				Ui::Toast::Show("Could not find log end :(");
				return;
			}
			const auto based = QByteArray::fromBase64(base.mid(0, end));
			const auto uncompressed = UnpackRawGzip(based);
			if (uncompressed.isEmpty()) {
				Ui::Toast::Show("Could not unpack log :(");
				return;
			}
			FileDialog::GetWritePath(Core::App().getFileDialogParent(), "Save detailed log", "Crash dump (*.txt)", QString(), [=](QString &&result) {
				if (result.isEmpty()) {
					return;
				}
				auto f = QFile(result);
				if (!f.open(QIODevice::WriteOnly)) {
					Ui::Toast::Show("Could not open details :(");
				} else if (f.write(uncompressed) != uncompressed.size()) {
					Ui::Toast::Show("Could not write details :(");
				} else {
					f.close();
					Ui::Toast::Show("Done!");
				}
			});
		});
	});
	codes.emplace(qsl("testchatcolors"), [](SessionController *window) {
		const auto now = !Data::CloudThemes::TestingColors();
		Data::CloudThemes::SetTestingColors(now);
		Ui::Toast::Show(now ? "Testing chat theme colors!" : "Not testing..");
	});

#ifdef Q_OS_MAC
	codes.emplace(qsl("customicon"), [](SessionController *window) {
		const auto iconFilters = qsl("Icon files (*.icns *.png);;") + FileDialog::AllFilesFilter();
		const auto change = [](const QString &path) {
			const auto success = path.isEmpty()
				? base::ClearCustomAppIcon()
				: base::SetCustomAppIcon(path);
			Ui::Toast::Show(success
				? (path.isEmpty()
					? "Icon cleared. Restarting the Dock."
					: "Icon updated. Restarting the Dock.")
				: (path.isEmpty()
					? "Icon clear failed. See log.txt for details."
					: "Icon update failed. See log.txt for details."));
		};
		FileDialog::GetOpenPath(Core::App().getFileDialogParent(), "Choose custom icon", iconFilters, [=](const FileDialog::OpenResult &result) {
			change(result.paths.isEmpty() ? QString() : result.paths.front());
		}, [=] {
			change(QString());
		});
	});
#endif // Q_OS_MAC

	return codes;
}

} // namespace

void CodesFeedString(SessionController *window, const QString &text) {
	static const auto codes = GenerateCodes();
	static auto secret = QString();

	secret += text.toLower();
	int size = secret.size(), from = 0;
	while (size > from) {
		auto piece = base::StringViewMid(secret,from);
		auto found = false;
		for (const auto &[key, method] : codes) {
			if (piece == key) {
				method(window);
				from = size;
				found = true;
				break;
			}
		}
		if (found) break;

		found = ranges::any_of(codes, [&](const auto &pair) {
			return pair.first.startsWith(piece);
		});
		if (found) break;

		++from;
	}
	secret = (size > from) ? secret.mid(from) : QString();
}

} // namespace Settings
