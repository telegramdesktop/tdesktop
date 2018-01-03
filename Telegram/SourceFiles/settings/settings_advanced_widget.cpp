/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_advanced_widget.h"

#include "styles/style_settings.h"
#include "lang/lang_keys.h"
#include "boxes/connection_box.h"
#include "boxes/confirm_box.h"
#include "boxes/about_box.h"
#include "boxes/local_storage_box.h"
#include "mainwindow.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/slide_wrap.h"
#include "storage/localstorage.h"
#include "window/themes/window_theme.h"

namespace Settings {

AdvancedWidget::AdvancedWidget(QWidget *parent, UserData *self) : BlockWidget(parent, self, lang(lng_settings_section_advanced_settings)) {
	createControls();
#ifndef TDESKTOP_DISABLE_NETWORK_PROXY
	subscribe(Global::RefConnectionTypeChanged(), [this]() {
		connectionTypeUpdated();
	});
#endif // !TDESKTOP_DISABLE_NETWORK_PROXY
	if (!self) {
		subscribe(Window::Theme::Background(), [this](const Window::Theme::BackgroundUpdate &update) {
			if (update.type == Window::Theme::BackgroundUpdate::Type::ApplyingTheme) {
				checkNonDefaultTheme();
			}
		});
	}
}

void AdvancedWidget::createControls() {
	style::margins marginSmall(0, 0, 0, st::settingsSmallSkip);
	style::margins marginLarge(0, 0, 0, st::settingsLargeSkip);

	style::margins marginLocalStorage = ([&marginSmall, &marginLarge]() {
#ifndef TDESKTOP_DISABLE_NETWORK_PROXY
		return marginSmall;
#else // !TDESKTOP_DISABLE_NETWORK_PROXY
		return marginLarge;
#endif // TDESKTOP_DISABLE_NETWORK_PROXY
	})();
	if (self()) {
		createChildRow(_manageLocalStorage, marginLocalStorage, lang(lng_settings_manage_local_storage), SLOT(onManageLocalStorage()));
	}

#ifndef TDESKTOP_DISABLE_NETWORK_PROXY
	createChildRow(_connectionType, marginLarge, lang(lng_connection_type), lang(lng_connection_auto_connecting), LabeledLink::Type::Primary, SLOT(onConnectionType()));
	connectionTypeUpdated();
#endif // !TDESKTOP_DISABLE_NETWORK_PROXY

	if (self()) {
		createChildRow(_askQuestion, marginSmall, lang(lng_settings_ask_question), SLOT(onAskQuestion()));
	} else {
		style::margins slidedPadding(0, marginLarge.bottom() / 2, 0, marginLarge.bottom() - (marginLarge.bottom() / 2));
		createChildRow(_useDefaultTheme, marginLarge, slidedPadding, lang(lng_settings_bg_use_default), SLOT(onUseDefaultTheme()));
		if (!Window::Theme::IsNonDefaultUsed()) {
			_useDefaultTheme->hide(anim::type::instant);
		}
		createChildRow(_toggleNightTheme, marginLarge, slidedPadding, getNightThemeToggleText(), SLOT(onToggleNightTheme()));
		if (Window::Theme::IsNonDefaultUsed()) {
			_toggleNightTheme->hide(anim::type::instant);
		}
	}
	createChildRow(_telegramFAQ, marginLarge, lang(lng_settings_faq), SLOT(onTelegramFAQ()));
	if (self()) {
		style::margins marginLogout(0, 0, 0, 2 * st::settingsLargeSkip);
		createChildRow(_logOut, marginLogout, lang(lng_settings_logout), SLOT(onLogOut()));
	}
}

void AdvancedWidget::checkNonDefaultTheme() {
	if (self()) return;
	_useDefaultTheme->toggle(
		Window::Theme::IsNonDefaultUsed(),
		anim::type::normal);
	_toggleNightTheme->entity()->setText(getNightThemeToggleText());
	_toggleNightTheme->toggle(
		!Window::Theme::IsNonDefaultUsed(),
		anim::type::normal);
}

void AdvancedWidget::onManageLocalStorage() {
	Ui::show(Box<LocalStorageBox>());
}

#ifndef TDESKTOP_DISABLE_NETWORK_PROXY
void AdvancedWidget::connectionTypeUpdated() {
	auto connection = [] {
		switch (Global::ConnectionType()) {
		case dbictHttpProxy:
		case dbictTcpProxy: {
			auto transport = MTP::dctransport();
			return transport.isEmpty() ? lang(lng_connection_proxy_connecting) : lng_connection_proxy(lt_transport, transport);
		} break;
		case dbictAuto:
		default: {
			auto transport = MTP::dctransport();
			return transport.isEmpty() ? lang(lng_connection_auto_connecting) : lng_connection_auto(lt_transport, transport);
		} break;
		}
	};
	_connectionType->link()->setText(connection());
	resizeToWidth(width());
}

void AdvancedWidget::onConnectionType() {
	Ui::show(Box<ConnectionBox>());
}
#endif // !TDESKTOP_DISABLE_NETWORK_PROXY

void AdvancedWidget::onUseDefaultTheme() {
	Window::Theme::ApplyDefault();
}

void AdvancedWidget::onToggleNightTheme() {
	Window::Theme::SwitchNightTheme(!Window::Theme::IsNightTheme());
}

void AdvancedWidget::onAskQuestion() {
	auto box = Box<ConfirmBox>(lang(lng_settings_ask_sure), lang(lng_settings_ask_ok), lang(lng_settings_faq_button), base::lambda_guarded(this, [this] {
		onAskQuestionSure();
	}), base::lambda_guarded(this, [this] {
		onTelegramFAQ();
	}));
	box->setStrictCancel(true);
	Ui::show(std::move(box));
}

void AdvancedWidget::onAskQuestionSure() {
	if (_supportGetRequest) return;
	_supportGetRequest = MTP::send(MTPhelp_GetSupport(), rpcDone(&AdvancedWidget::supportGot));
}

void AdvancedWidget::supportGot(const MTPhelp_Support &support) {
	if (!App::main()) return;

	if (support.type() == mtpc_help_support) {
		if (auto user = App::feedUsers(MTP_vector<MTPUser>(1, support.c_help_support().vuser))) {
			Ui::showPeerHistory(user, ShowAtUnreadMsgId);
		}
	}
}

QString AdvancedWidget::getNightThemeToggleText() const {
	return lang(Window::Theme::IsNightTheme() ? lng_settings_disable_night_theme : lng_settings_enable_night_theme);
}

void AdvancedWidget::onTelegramFAQ() {
	QDesktopServices::openUrl(telegramFaqLink());
}

void AdvancedWidget::onLogOut() {
	App::wnd()->onLogout();
}

} // namespace Settings
