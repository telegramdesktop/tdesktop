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
#include "export/export_manager.h"
#include "ui/platform/ui_platform_window.h"
#include "platform/platform_window_title.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "main/main_app_config.h"
#include "media/view/media_view_open_common.h"
#include "lang/lang_keys.h"
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
#include "ui/boxes/confirm_box.h"
#include "data/data_peer.h"
#include "mainwindow.h"
#include "apiwrap.h" // ApiWrap::acceptTerms.
#include "styles/style_layers.h"

#include <QtGui/QWindow>
#include <QtGui/QScreen>

namespace Window {
namespace {

class Show final : public Ui::Show {
public:
	explicit Show(not_null<Controller*> window);

	void showOrHideBoxOrLayer(
		std::variant<
			v::null_t,
			object_ptr<Ui::BoxContent>,
			std::unique_ptr<Ui::LayerWidget>> &&layer,
		Ui::LayerOptions options,
		anim::type animated) const override;
	[[nodiscard]] not_null<QWidget*> toastParent() const override;
	[[nodiscard]] bool valid() const override;
	operator bool() const override;

private:
	const base::weak_ptr<Controller> _window;

};

Show::Show(not_null<Controller*> window)
: _window(base::make_weak(window)) {
}

void Show::showOrHideBoxOrLayer(
		std::variant<
			v::null_t,
			object_ptr<Ui::BoxContent>,
			std::unique_ptr<Ui::LayerWidget>> &&layer,
		Ui::LayerOptions options,
		anim::type animated) const {
	if (const auto window = _window.get()) {
		window->widget()->showOrHideBoxOrLayer(
			std::move(layer),
			options,
			animated);
	}
}

not_null<QWidget*> Show::toastParent() const {
	const auto window = _window.get();
	Assert(window != nullptr);
	return window->widget()->bodyWidget();
}

bool Show::valid() const {
	return !_window.empty();
}

Show::operator bool() const {
	return valid();
}

} // namespace

Controller::Controller() : Controller(CreateArgs{}) {
}

Controller::Controller(not_null<Main::Account*> account)
: Controller(CreateArgs{}) {
	showAccount(account);
}

Controller::Controller(
	not_null<PeerData*> singlePeer,
	MsgId showAtMsgId)
: Controller(CreateArgs{ singlePeer.get() }) {
	showAccount(&singlePeer->account(), showAtMsgId);
}

Controller::Controller(CreateArgs &&args)
: _singlePeer(args.singlePeer)
, _isActiveTimer([=] { updateIsActive(); })
, _widget(this)
, _adaptive(std::make_unique<Adaptive>()) {
	_widget.init();
}

Controller::~Controller() {
	// We want to delete all widgets before the _sessionController.
	_widget.ui_hideSettingsAndLayer(anim::type::instant);
	_widget.clearWidgets();
	_accountLifetime.destroy();
	_sessionControllerValue = nullptr;
	_sessionController = nullptr;
}

void Controller::showAccount(not_null<Main::Account*> account) {
	showAccount(account, ShowAtUnreadMsgId);
}

void Controller::showAccount(
		not_null<Main::Account*> account,
		MsgId singlePeerShowAtMsgId) {
	Expects(isPrimary() || &_singlePeer->account() == account);

	const auto prevSessionUniqueId = (_account && _account->sessionExists())
		? _account->session().uniqueId()
		: 0;
	_accountLifetime.destroy();
	_account = account;
	Core::App().checkWindowAccount(this);

	const auto updateOnlineOfPrevSesssion = crl::guard(_account, [=] {
		if (!prevSessionUniqueId) {
			return;
		}
		for (auto &[index, account] : _account->domain().accounts()) {
			if (const auto anotherSession = account->maybeSession()) {
				if (anotherSession->uniqueId() == prevSessionUniqueId) {
					anotherSession->updates().updateOnline(crl::now());
					return;
				}
			}
		}
	});

	_account->sessionValue(
	) | rpl::start_with_next([=](Main::Session *session) {
		if (!isPrimary() && (&_singlePeer->session() != session)) {
			Core::App().closeWindow(this);
			return;
		}
		const auto was = base::take(_sessionController);
		_sessionController = session
			? std::make_unique<SessionController>(session, this)
			: nullptr;
		_sessionControllerValue = _sessionController.get();

		auto oldContentCache = _widget.grabForSlideAnimation();
		_widget.updateWindowIcon();
		if (session) {
			setupSideBar();
			setupMain(singlePeerShowAtMsgId, std::move(oldContentCache));

			session->updates().isIdleValue(
			) | rpl::filter([=](bool idle) {
				return !idle;
			}) | rpl::start_with_next([=] {
				widget()->checkActivation();
			}, _sessionController->lifetime());

			session->termsLockValue(
			) | rpl::start_with_next([=] {
				checkLockByTerms();
				_widget.updateGlobalMenu();
			}, _sessionController->lifetime());

			widget()->setInnerFocus();

			_sessionController->activeChatChanges(
			) | rpl::start_with_next([=] {
				_widget.updateTitle();
			}, _sessionController->lifetime());
			_widget.updateTitle();

			session->updates().updateOnline(crl::now());
		} else {
			sideBarChanged();
			setupIntro(std::move(oldContentCache));
			_widget.updateGlobalMenu();
		}

		crl::on_main(updateOnlineOfPrevSesssion);
	}, _accountLifetime);
}

PeerData *Controller::singlePeer() const {
	return _singlePeer;
}

void Controller::setupSideBar() {
	Expects(_sessionController != nullptr);

	if (!isPrimary()) {
		return;
	}
	_sessionController->filtersMenuChanged(
	) | rpl::start_with_next([=] {
		sideBarChanged();
	}, _sessionController->lifetime());

	if (_sessionController->session().settings().dialogsFiltersEnabled()) {
		_sessionController->toggleFiltersMenu(true);
	} else {
		sideBarChanged();
	}
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
	hideSettingsAndLayer(anim::type::instant);
	const auto box = show(Box<TermsBox>(
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
	const auto box = show(Box<Window::TermsBox>(
		TextWithEntities{ tr::lng_terms_update_sorry(tr::now) },
		tr::lng_terms_decline_and_delete(),
		tr::lng_terms_back(),
		true));

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
			hideLayer();
		}
	};
	show(Ui::MakeConfirmBox({
		.text = tr::lng_terms_delete_warning(),
		.confirmed = deleteByTerms,
		.confirmText = tr::lng_terms_delete_now(),
		.confirmStyle = &st::attentionBoxButton,
	}));
}

void Controller::finishFirstShow() {
	_widget.finishFirstShow();
	checkThemeEditor();
}

Main::Session *Controller::maybeSession() const {
	return _account ? _account->maybeSession() : nullptr;
}

auto Controller::sessionControllerValue() const
-> rpl::producer<SessionController*> {
	return _sessionControllerValue.value();
}

auto Controller::sessionControllerChanges() const
-> rpl::producer<SessionController*> {
	return _sessionControllerValue.changes();
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

void Controller::setupIntro(QPixmap oldContentCache) {
	const auto point = Core::App().domain().maybeLastOrSomeAuthedAccount()
		? Intro::EnterPoint::Qr
		: Intro::EnterPoint::Start;
	_widget.setupIntro(point, std::move(oldContentCache));
}

void Controller::setupMain(
		MsgId singlePeerShowAtMsgId,
		QPixmap oldContentCache) {
	Expects(_sessionController != nullptr);

	_widget.setupMain(singlePeerShowAtMsgId, std::move(oldContentCache));

	if (const auto id = Ui::Emoji::NeedToSwitchBackToId()) {
		Ui::Emoji::LoadAndSwitchTo(&_sessionController->session(), id);
	}
}

void Controller::showSettings() {
	_widget.showSettings();
}

int Controller::verticalShadowTop() const {
	return (Platform::NativeTitleRequiresShadow()
		&& Ui::Platform::NativeWindowFrameSupported()
		&& Core::App().settings().nativeWindowFrame())
		? st::lineWidth
		: 0;
}

void Controller::showToast(Ui::Toast::Config &&config) {
	Show(this).showToast(std::move(config));
}

void Controller::showToast(TextWithEntities &&text, crl::time duration) {
	Show(this).showToast(std::move(text), duration);
}

void Controller::showToast(const QString &text, crl::time duration) {
	Show(this).showToast(text, duration);
}

void Controller::showLayer(
		std::unique_ptr<Ui::LayerWidget> &&layer,
		Ui::LayerOptions options,
		anim::type animated) {
	_widget.showOrHideBoxOrLayer(std::move(layer), options, animated);
}

void Controller::showBox(
		object_ptr<Ui::BoxContent> content,
		Ui::LayerOptions options,
		anim::type animated) {
	_widget.showOrHideBoxOrLayer(std::move(content), options, animated);
}

void Controller::showRightColumn(object_ptr<TWidget> widget) {
	_widget.showRightColumn(std::move(widget));
}

void Controller::hideLayer(anim::type animated) {
	_widget.showOrHideBoxOrLayer(v::null, Ui::LayerOption::CloseOther, animated);
}

void Controller::hideSettingsAndLayer(anim::type animated) {
	_widget.ui_hideSettingsAndLayer(animated);
}

bool Controller::isLayerShown() const {
	return _widget.ui_isLayerShown();
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
	if (Core::App().settings().workMode()
			== Core::Settings::WorkMode::TrayOnly) {
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

void Controller::invokeForSessionController(
		not_null<Main::Account*> account,
		PeerData *singlePeer,
		Fn<void(not_null<SessionController*>)> &&callback) {
	const auto separateWindow = singlePeer
		? Core::App().separateWindowForPeer(singlePeer)
		: nullptr;
	const auto separateSession = separateWindow
		? separateWindow->sessionController()
		: nullptr;
	if (separateSession) {
		return callback(separateSession);
	}
	_account->domain().activate(std::move(account));
	if (_sessionController) {
		callback(_sessionController.get());
	}
}

QPoint Controller::getPointForCallPanelCenter() const {
	return _widget.isActive()
		? _widget.geometry().center()
		: _widget.screen()->geometry().center();
}

void Controller::showLogoutConfirmation() {
	const auto account = Core::App().passcodeLocked()
		? nullptr
		: sessionController()
		? &sessionController()->session().account()
		: nullptr;
	const auto weak = base::make_weak(account);
	const auto callback = [=](Fn<void()> close) {
		if (!account || weak) {
			Core::App().logoutWithChecks(account);
		}
		if (close) {
			close();
		}
	};
	show(Ui::MakeConfirmBox({
		.text = tr::lng_sure_logout(),
		.confirmed = callback,
		.confirmText = tr::lng_settings_logout(),
		.confirmStyle = &st::attentionBoxButton,
	}));
}

Window::Adaptive &Controller::adaptive() const {
	return *_adaptive;
}

void Controller::openInMediaView(Media::View::OpenRequest &&request) {
	_openInMediaViewRequests.fire(std::move(request));
}

auto Controller::openInMediaViewRequests() const
-> rpl::producer<Media::View::OpenRequest> {
	return _openInMediaViewRequests.events();
}

void Controller::setDefaultFloatPlayerDelegate(
		not_null<Media::Player::FloatDelegate*> delegate) {
	_defaultFloatPlayerDelegate = delegate;
	_replacementFloatPlayerDelegate = nullptr;
	_floatPlayerDelegate = delegate;
}

void Controller::replaceFloatPlayerDelegate(
		not_null<Media::Player::FloatDelegate*> replacement) {
	Expects(_defaultFloatPlayerDelegate != nullptr);

	_replacementFloatPlayerDelegate = replacement;
	_floatPlayerDelegate = replacement;
}

void Controller::restoreFloatPlayerDelegate(
		not_null<Media::Player::FloatDelegate*> replacement) {
	Expects(_defaultFloatPlayerDelegate != nullptr);

	if (_replacementFloatPlayerDelegate == replacement) {
		_replacementFloatPlayerDelegate = nullptr;
		_floatPlayerDelegate = _defaultFloatPlayerDelegate;
	}
}

auto Controller::floatPlayerDelegate() const -> FloatDelegate* {
	return _floatPlayerDelegate.current();
}

auto Controller::floatPlayerDelegateValue() const
-> rpl::producer<FloatDelegate*> {
	return _floatPlayerDelegate.value();
}

std::shared_ptr<Ui::Show> Controller::uiShow() {
	return std::make_shared<Show>(this);
}

rpl::lifetime &Controller::lifetime() {
	return _lifetime;
}

} // namespace Window
