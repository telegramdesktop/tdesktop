/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_widget.h"

#include "settings/settings_inner_widget.h"
#include "settings/settings_fixed_bar.h"
#include "styles/style_settings.h"
#include "styles/style_window.h"
#include "styles/style_boxes.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/buttons.h"
#include "mainwindow.h"
#include "mainwidget.h"
#include "storage/localstorage.h"
#include "boxes/confirm_box.h"
#include "lang/lang_keys.h"
#include "lang/lang_cloud_manager.h"
#include "messenger.h"
#include "mtproto/mtp_instance.h"
#include "mtproto/dc_options.h"
#include "core/file_utilities.h"
#include "core/update_checker.h"
#include "window/themes/window_theme.h"
#include "window/themes/window_theme_editor.h"
#include "media/media_audio_track.h"

namespace Settings {
namespace {

QString SecretText;
QMap<QString, base::lambda<void()>> Codes;

void fillCodes() {
	Codes.insert(qsl("debugmode"), [] {
		QString text = cDebug() ? qsl("Do you want to disable DEBUG logs?") : qsl("Do you want to enable DEBUG logs?\n\nAll network events will be logged.");
		Ui::show(Box<ConfirmBox>(text, [] {
			Messenger::Instance().onSwitchDebugMode();
		}));
	});
	Codes.insert(qsl("testmode"), [] {
		auto text = cTestMode() ? qsl("Do you want to disable TEST mode?") : qsl("Do you want to enable TEST mode?\n\nYou will be switched to test cloud.");
		Ui::show(Box<ConfirmBox>(text, [] {
			Messenger::Instance().onSwitchTestMode();
		}));
	});
#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	Codes.insert(qsl("testupdate"), [] {
		Core::UpdateChecker().test();
	});
#endif // TDESKTOP_DISABLE_AUTOUPDATE
	Codes.insert(qsl("loadlang"), [] {
		Lang::CurrentCloudManager().switchToLanguage(qsl("custom"));
	});
	Codes.insert(qsl("debugfiles"), [] {
		if (!cDebug()) return;
		if (DebugLogging::FileLoader()) {
			Global::RefDebugLoggingFlags() &= ~DebugLogging::FileLoaderFlag;
		} else {
			Global::RefDebugLoggingFlags() |= DebugLogging::FileLoaderFlag;
		}
		Ui::show(Box<InformBox>(DebugLogging::FileLoader() ? qsl("Enabled file download logging") : qsl("Disabled file download logging")));
	});
	Codes.insert(qsl("crashplease"), [] {
		Unexpected("Crashed in Settings!");
	});
	Codes.insert(qsl("workmode"), [] {
		auto text = Global::DialogsModeEnabled() ? qsl("Disable work mode?") : qsl("Enable work mode?");
		Ui::show(Box<ConfirmBox>(text, [] {
			Messenger::Instance().onSwitchWorkMode();
		}));
	});
	Codes.insert(qsl("moderate"), [] {
		auto text = Global::ModerateModeEnabled() ? qsl("Disable moderate mode?") : qsl("Enable moderate mode?");
		Ui::show(Box<ConfirmBox>(text, []() {
			Global::SetModerateModeEnabled(!Global::ModerateModeEnabled());
			Local::writeUserSettings();
			Ui::hideLayer();
		}));
	});
	Codes.insert(qsl("getdifference"), [] {
		if (auto main = App::main()) {
			main->getDifference();
		}
	});
	Codes.insert(qsl("loadcolors"), [] {
		FileDialog::GetOpenPath("Open palette file", "Palette (*.tdesktop-palette)", [](const FileDialog::OpenResult &result) {
			if (!result.paths.isEmpty()) {
				Window::Theme::Apply(result.paths.front());
			}
		});
	});
	Codes.insert(qsl("edittheme"), [] {
		Window::Theme::Editor::Start();
	});
	Codes.insert(qsl("videoplayer"), [] {
		auto text = cUseExternalVideoPlayer() ? qsl("Use internal video player?") : qsl("Use external video player?");
		Ui::show(Box<ConfirmBox>(text, [] {
			cSetUseExternalVideoPlayer(!cUseExternalVideoPlayer());
			Local::writeUserSettings();
			Ui::hideLayer();
		}));
	});
	Codes.insert(qsl("endpoints"), [] {
		FileDialog::GetOpenPath("Open DC endpoints", "DC Endpoints (*.tdesktop-endpoints)", [](const FileDialog::OpenResult &result) {
			if (!result.paths.isEmpty()) {
				if (!Messenger::Instance().mtp()->dcOptions()->loadFromFile(result.paths.front())) {
					Ui::show(Box<InformBox>("Could not load endpoints :( Errors in 'log.txt'."));
				}
			}
		});
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
		Codes.insert(key, [audioFilters, key] {
			if (!AuthSession::Exists()) {
				return;
			}

			FileDialog::GetOpenPath("Open audio file", audioFilters, [key](const FileDialog::OpenResult &result) {
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
	Codes.insert(qsl("sounds_reset"), [] {
		if (AuthSession::Exists()) {
			Auth().settings().clearSoundOverrides();
			Local::writeUserSettings();
			Ui::show(Box<InformBox>("All sound overrides were reset."));
		}
	});
}

void codesFeedString(const QString &text) {
	if (Codes.isEmpty()) fillCodes();

	SecretText += text.toLower();
	int size = SecretText.size(), from = 0;
	while (size > from) {
		auto piece = SecretText.midRef(from);
		auto found = false;
		for (auto i = Codes.cbegin(), e = Codes.cend(); i != e; ++i) {
			if (piece == i.key()) {
				(*i)();
				from = size;
				found = true;
				break;
			}
		}
		if (found) break;

		for (auto i = Codes.cbegin(), e = Codes.cend(); i != e; ++i) {
			if (i.key().startsWith(piece)) {
				found = true;
				break;
			}
		}
		if (found) break;

		++from;
	}
	SecretText = (size > from) ? SecretText.mid(from) : QString();
}

} // namespace

Widget::Widget(QWidget *parent) {
	refreshLang();
	subscribe(Lang::Current().updated(), [this] { refreshLang(); });

	_inner = setInnerWidget(object_ptr<InnerWidget>(this));
	setCloseClickHandler([]() {
		Ui::hideSettingsAndLayer();
	});
}

void Widget::refreshLang() {
	setTitle(lang(lng_menu_settings));

	update();
}

void Widget::keyPressEvent(QKeyEvent *e) {
	codesFeedString(e->text());
	return LayerWidget::keyPressEvent(e);
}

void Widget::parentResized() {
	auto parentSize = parentWidget()->size();
	auto windowWidth = parentSize.width();
	auto newWidth = st::settingsMaxWidth;
	auto newContentLeft = st::settingsMaxPadding;
	if (windowWidth <= st::settingsMaxWidth) {
		newWidth = windowWidth;
		newContentLeft = st::settingsMinPadding;
		if (windowWidth > st::windowMinWidth) {
			// Width changes from st::windowMinWidth to st::settingsMaxWidth.
			// Padding changes from st::settingsMinPadding to st::settingsMaxPadding.
			newContentLeft += ((newWidth - st::windowMinWidth) * (st::settingsMaxPadding - st::settingsMinPadding)) / (st::settingsMaxWidth - st::windowMinWidth);
		}
	} else if (windowWidth < st::settingsMaxWidth + 2 * st::settingsMargin) {
		newWidth = windowWidth - 2 * st::settingsMargin;
		newContentLeft = st::settingsMinPadding;
		if (windowWidth > st::windowMinWidth) {
			// Width changes from st::windowMinWidth to st::settingsMaxWidth.
			// Padding changes from st::settingsMinPadding to st::settingsMaxPadding.
			newContentLeft += ((newWidth - st::windowMinWidth) * (st::settingsMaxPadding - st::settingsMinPadding)) / (st::settingsMaxWidth - st::windowMinWidth);
		}
	}
	resizeToWidth(newWidth, newContentLeft);
}

void Widget::resizeUsingInnerHeight(int newWidth, int innerHeight) {
	if (!parentWidget()) return;

	auto parentSize = parentWidget()->size();
	auto windowWidth = parentSize.width();
	auto windowHeight = parentSize.height();
	auto maxHeight = st::settingsFixedBarHeight + innerHeight;
	auto newHeight = maxHeight + st::boxRadius;
	if (newHeight > windowHeight || newWidth >= windowWidth) {
		newHeight = windowHeight;
	}

	auto roundedCorners = newHeight < windowHeight;
	setRoundedCorners(roundedCorners);
	setAttribute(Qt::WA_OpaquePaintEvent, !roundedCorners);

	setGeometry((windowWidth - newWidth) / 2, (windowHeight - newHeight) / 2, newWidth, newHeight);
	update();
}

} // namespace Settings
