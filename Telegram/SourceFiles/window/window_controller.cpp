/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_controller.h"

#include "api/api_updates.h"
#include "core/application.h"
#include "core/click_handler_types.h"
#include "platform/platform_window_title.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "main/main_app_config.h"
#include "intro/intro_widget.h"
#include "mtproto/mtproto_config.h"
#include "ui/layers/box_content.h"
#include "ui/layers/layer_widget.h"
#include "ui/toast/toast.h"
#include "ui/emoji_config.h"
#include "chat_helpers/emoji_sets_manager.h"
#include "window/window_session_controller.h"
#include "window/themes/window_theme.h"
#include "window/themes/window_theme_editor.h"
#include "boxes/confirm_box.h"
#include "mainwindow.h"
#include "apiwrap.h" // ApiWrap::acceptTerms.
#include "facades.h"
#include "app.h"
#include "styles/style_layers.h"

#include <QtGui/QWindow>
#include <QtGui/QScreen>

namespace Window {

Controller::Controller()
: _widget(this)
, _isActiveTimer([=] { updateIsActive(); }) {
	_widget.init();
}

Controller::~Controller() {
	// We want to delete all widgets before the _sessionController.
	_widget.ui_hideSettingsAndLayer(anim::type::instant);
	_widget.clearWidgets();
}

void Controller::showAccount(not_null<Main::Account*> account) {
	const auto prevSessionUniqueId = (_account && _account->sessionExists())
		? _account->session().uniqueId()
		: 0;
	_accountLifetime.destroy();
	_account = account;

	const auto updateOnlineOfPrevSesssion = crl::guard(_account, [=] {
		if (!prevSessionUniqueId) {
			return;
		}
		for (auto &[index, account] : _account->domain().accounts()) {
			if (const auto anotherSession = account->maybeSession()) {
				if (anotherSession->uniqueId() == prevSessionUniqueId) {
					anotherSession->updates().updateOnline();
					return;
				}
			}
		}
	});

	_account->sessionValue(
	) | rpl::start_with_next([=](Main::Session *session) {
		const auto was = base::take(_sessionController);
		_sessionController = session
			? std::make_unique<SessionController>(session, this)
			: nullptr;
		if (_sessionController) {
			_sessionController->filtersMenuChanged(
			) | rpl::start_with_next([=] {
				sideBarChanged();
			}, session->lifetime());
		}
		if (session && session->settings().dialogsFiltersEnabled()) {
			_sessionController->toggleFiltersMenu(true);
		} else {
			sideBarChanged();
		}
		_widget.updateWindowIcon();
		if (session) {
			setupMain();

			session->termsLockValue(
			) | rpl::start_with_next([=] {
				checkLockByTerms();
				_widget.updateGlobalMenu();
			}, _lifetime);
		} else {
			setupIntro();
			_widget.updateGlobalMenu();
		}

		crl::on_main(updateOnlineOfPrevSesssion);
	}, _accountLifetime);
}

void Controller::checkLockByTerms() {
	const auto data = account().sessionExists()
		? account().session().termsLocked()
		: std::nullopt;
	if (!data) {
		if (_termsBox) {
			_termsBox->closeBox();
		}
		return;
	}
	Ui::hideSettingsAndLayer(anim::type::instant);
	const auto box = Ui::show(Box<TermsBox>(
		*data,
		tr::lng_terms_agree(),
		tr::lng_terms_decline()));

	box->setCloseByEscape(false);
	box->setCloseByOutsideClick(false);

	const auto id = data->id;
	box->agreeClicks(
	) | rpl::start_with_next([=] {
		const auto mention = box ? box->lastClickedMention() : QString();
		box->closeBox();
		if (const auto session = account().maybeSession()) {
			session->api().acceptTerms(id);
			session->unlockTerms();
			if (!mention.isEmpty()) {
				MentionClickHandler(mention).onClick({});
			}
		}
	}, box->lifetime());

	box->cancelClicks(
	) | rpl::start_with_next([=] {
		showTermsDecline();
	}, box->lifetime());

	QObject::connect(box, &QObject::destroyed, [=] {
		crl::on_main(widget(), [=] { checkLockByTerms(); });
	});

	_termsBox = box;
}

void Controller::showTermsDecline() {
	const auto box = Ui::show(
		Box<Window::TermsBox>(
			TextWithEntities{ tr::lng_terms_update_sorry(tr::now) },
			tr::lng_terms_decline_and_delete(),
			tr::lng_terms_back(),
			true),
		Ui::LayerOption::KeepOther);

	box->agreeClicks(
	) | rpl::start_with_next([=] {
		if (box) {
			box->closeBox();
		}
		showTermsDelete();
	}, box->lifetime());

	box->cancelClicks(
	) | rpl::start_with_next([=] {
		if (box) {
			box->closeBox();
		}
	}, box->lifetime());
}

void Controller::showTermsDelete() {
	const auto deleteByTerms = [=] {
		if (const auto session = account().maybeSession()) {
			session->termsDeleteNow();
		} else {
			Ui::hideLayer();
		}
	};
	Ui::show(
		Box<ConfirmBox>(
			tr::lng_terms_delete_warning(tr::now),
			tr::lng_terms_delete_now(tr::now),
			st::attentionBoxButton,
			deleteByTerms),
		Ui::LayerOption::KeepOther);
}

void Controller::finishFirstShow() {
	_widget.finishFirstShow();
	checkThemeEditor();
}

bool Controller::locked() const {
	if (Core::App().passcodeLocked()) {
		return true;
	} else if (const auto controller = sessionController()) {
		return controller->session().termsLocked().has_value();
	}
	return false;
}

void Controller::checkThemeEditor() {
	using namespace Window::Theme;

	if (const auto editing = Background()->editingTheme()) {
		showRightColumn(Box<Editor>(this, *editing));
	}
}

void Controller::setupPasscodeLock() {
	_widget.setupPasscodeLock();
}

void Controller::clearPasscodeLock() {
	if (!_account) {
		showAccount(&Core::App().activeAccount());
	} else {
		_widget.clearPasscodeLock();
	}
}

void Controller::setupIntro() {
	const auto parent = Core::App().domain().maybeLastOrSomeAuthedAccount();
	if (!parent) {
		_widget.setupIntro(Intro::EnterPoint::Start);
		return;
	}
	const auto qrLogin = parent->appConfig().get<QString>(
		"qr_login_code",
		"[not-set]");
	DEBUG_LOG(("qr_login_code in setup: %1").arg(qrLogin));
	const auto qr = (qrLogin == "primary");
	_widget.setupIntro(qr ? Intro::EnterPoint::Qr : Intro::EnterPoint::Phone);
}

void Controller::setupMain() {
	Expects(_sessionController != nullptr);

	_widget.setupMain();

	if (const auto id = Ui::Emoji::NeedToSwitchBackToId()) {
		Ui::Emoji::LoadAndSwitchTo(&_sessionController->session(), id);
	}
}

void Controller::showSettings() {
	_widget.showSettings();
}

int Controller::verticalShadowTop() const {
	return (Platform::NativeTitleRequiresShadow()
		&& Core::App().settings().nativeWindowFrame())
		? st::lineWidth
		: 0;
}

void Controller::showToast(const QString &text) {
	Ui::Toast::Show(_widget.bodyWidget(), text);
}

void Controller::showBox(
		object_ptr<Ui::BoxContent> content,
		Ui::LayerOptions options,
		anim::type animated) {
	_widget.ui_showBox(std::move(content), options, animated);
}

void Controller::showRightColumn(object_ptr<TWidget> widget) {
	_widget.showRightColumn(std::move(widget));
}

void Controller::sideBarChanged() {
	_widget.recountGeometryConstraints();
}

void Controller::activate() {
	_widget.activate();
}

void Controller::reActivate() {
	_widget.reActivateWindow();
}

void Controller::updateIsActiveFocus() {
	_isActiveTimer.callOnce(sessionController()
		? sessionController()->session().serverConfig().onlineFocusTimeout
		: crl::time(1000));
}

void Controller::updateIsActiveBlur() {
	_isActiveTimer.callOnce(sessionController()
		? sessionController()->session().serverConfig().offlineBlurTimeout
		: crl::time(1000));
}

void Controller::updateIsActive() {
	_widget.updateIsActive();
}

void Controller::minimize() {
	if (Global::WorkMode().value() == dbiwmTrayOnly) {
		_widget.minimizeToTray();
	} else {
		_widget.setWindowState(_widget.windowState() | Qt::WindowMinimized);
	}
}

void Controller::close() {
	_widget.close();
}

void Controller::preventOrInvoke(Fn<void()> &&callback) {
	_widget.preventOrInvoke(std::move(callback));
}

QPoint Controller::getPointForCallPanelCenter() const {
	Expects(_widget.windowHandle() != nullptr);

	return _widget.isActive()
		? _widget.geometry().center()
		: _widget.windowHandle()->screen()->geometry().center();
}

} // namespace Window
