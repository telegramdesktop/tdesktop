/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mainwindow.h"

#include "data/data_document.h"
#include "data/data_session.h"
#include "data/data_document_media.h"
#include "dialogs/ui/dialogs_layout.h"
#include "history/history.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/tooltip.h"
#include "ui/layers/layer_widget.h"
#include "ui/emoji_config.h"
#include "ui/ui_utility.h"
#include "lang/lang_cloud_manager.h"
#include "lang/lang_instance.h"
#include "lang/lang_keys.h"
#include "core/shortcuts.h"
#include "core/sandbox.h"
#include "core/application.h"
#include "export/export_manager.h"
#include "intro/intro_widget.h"
#include "main/main_session.h"
#include "main/main_account.h" // Account::sessionValue.
#include "main/main_domain.h"
#include "mainwidget.h"
#include "media/system_media_controls_manager.h"
#include "ui/boxes/confirm_box.h"
#include "boxes/connection_box.h"
#include "storage/storage_account.h"
#include "storage/localstorage.h"
#include "apiwrap.h"
#include "api/api_updates.h"
#include "settings/settings_intro.h"
#include "platform/platform_notifications_manager.h"
#include "base/platform/base_platform_info.h"
#include "base/variant.h"
#include "window/notifications_manager.h"
#include "window/themes/window_theme.h"
#include "window/themes/window_theme_warning.h"
#include "window/window_lock_widgets.h"
#include "window/window_main_menu.h"
#include "window/window_controller.h" // App::wnd.
#include "window/window_session_controller.h"
#include "window/window_media_preview.h"
#include "styles/style_dialogs.h"
#include "styles/style_layers.h"
#include "styles/style_window.h"

#include <QtGui/QWindow>
#include <QtCore/QCoreApplication>

namespace {

// Code for testing languages is F7-F6-F7-F8
void FeedLangTestingKey(int key) {
	static auto codeState = 0;
	if ((codeState == 0 && key == Qt::Key_F7)
		|| (codeState == 1 && key == Qt::Key_F6)
		|| (codeState == 2 && key == Qt::Key_F7)
		|| (codeState == 3 && key == Qt::Key_F8)) {
		++codeState;
	} else {
		codeState = 0;
	}
	if (codeState == 4) {
		codeState = 0;
		Lang::CurrentCloudManager().switchToTestLanguage();
	}
}

} // namespace

MainWindow::MainWindow(not_null<Window::Controller*> controller)
: Platform::MainWindow(controller) {
	resize(st::windowDefaultWidth, st::windowDefaultHeight);

	setLocale(QLocale(QLocale::English, QLocale::UnitedStates));

	using Window::Theme::BackgroundUpdate;
	Window::Theme::Background()->updates(
	) | rpl::start_with_next([=](const BackgroundUpdate &data) {
		themeUpdated(data);
	}, lifetime());

	Core::App().passcodeLockChanges(
	) | rpl::start_with_next([=] {
		updateGlobalMenu();
	}, lifetime());

	Ui::Emoji::Updated(
	) | rpl::start_with_next([=] {
		Ui::ForceFullRepaint(this);
	}, lifetime());

	setAttribute(Qt::WA_OpaquePaintEvent);
}

void MainWindow::initHook() {
	Platform::MainWindow::initHook();

	QCoreApplication::instance()->installEventFilter(this);

	// Non-queued activeChanged handlers must use QtSignalProducer.
	connect(
		windowHandle(),
		&QWindow::activeChanged,
		this,
		[=] { checkActivation(); },
		Qt::QueuedConnection);

	if (Media::SystemMediaControlsManager::Supported()) {
		using MediaManager = Media::SystemMediaControlsManager;
		_mediaControlsManager = std::make_unique<MediaManager>(&controller());
	}
}

void MainWindow::applyInitialWorkMode() {
	const auto workMode = Core::App().settings().workMode();
	workmodeUpdated(workMode);

	if (controller().isPrimary()) {
		if (Core::App().settings().windowPosition().maximized) {
			DEBUG_LOG(("Window Pos: First show, setting maximized."));
			setWindowState(Qt::WindowMaximized);
		}
		if (cStartInTray()
			|| (cLaunchMode() == LaunchModeAutoStart
				&& cStartMinimized()
				&& !Core::App().passcodeLocked())) {
			DEBUG_LOG(("Window Pos: First show, setting minimized after."));
			if (workMode == Core::Settings::WorkMode::TrayOnly
				|| workMode == Core::Settings::WorkMode::WindowAndTray) {
				hide();
			} else {
				setWindowState(windowState() | Qt::WindowMinimized);
			}
		}
	}
	setPositionInited();
}

void MainWindow::finishFirstShow() {
	applyInitialWorkMode();
	createGlobalMenu();

	windowDeactivateEvents(
	) | rpl::start_with_next([=] {
		Ui::Tooltip::Hide();
	}, lifetime());

	setAttribute(Qt::WA_NoSystemBackground);

	if (!_passcodeLock && _main) {
		_main->activate();
	}
}

void MainWindow::clearWidgetsHook() {
	_mediaPreview.destroy();
	_main.destroy();
	_intro.destroy();
	if (!Core::App().passcodeLocked()) {
		_passcodeLock.destroy();
	}
}

QPixmap MainWindow::grabForSlideAnimation() {
	return Ui::GrabWidget(bodyWidget());
}

void MainWindow::preventOrInvoke(Fn<void()> callback) {
	if (_main && _main->preventsCloseSection(callback)) {
		return;
	}
	callback();
}

void MainWindow::setupPasscodeLock() {
	auto animated = (_main || _intro);
	auto oldContentCache = animated ? grabForSlideAnimation() : QPixmap();
	_passcodeLock.create(bodyWidget(), &controller());
	updateControlsGeometry();

	Core::App().hideMediaView();
	ui_hideSettingsAndLayer(anim::type::instant);
	if (_main) {
		_main->hide();
	}
	if (_intro) {
		_intro->hide();
	}
	if (animated) {
		_passcodeLock->showAnimated(std::move(oldContentCache));
	} else {
		_passcodeLock->showFinished();
		setInnerFocus();
	}
}

void MainWindow::clearPasscodeLock() {
	Expects(_intro || _main);

	if (!_passcodeLock) {
		return;
	}

	auto oldContentCache = grabForSlideAnimation();
	_passcodeLock.destroy();
	if (_intro) {
		_intro->show();
		updateControlsGeometry();
		_intro->showAnimated(std::move(oldContentCache), true);
	} else if (_main) {
		_main->show();
		updateControlsGeometry();
		_main->showAnimated(std::move(oldContentCache), true);
		Core::App().checkStartUrl();
	}
}

void MainWindow::setupIntro(
		Intro::EnterPoint point,
		QPixmap oldContentCache) {
	auto animated = (_main || _passcodeLock);

	destroyLayer();
	auto created = object_ptr<Intro::Widget>(
		bodyWidget(),
		&controller(),
		&account(),
		point);
	created->showSettingsRequested(
	) | rpl::start_with_next([=] {
		showSettings();
	}, created->lifetime());

	clearWidgets();
	_intro = std::move(created);
	if (_passcodeLock) {
		_intro->hide();
	} else {
		_intro->show();
		updateControlsGeometry();
		if (animated) {
			_intro->showAnimated(std::move(oldContentCache));
		} else {
			setInnerFocus();
		}
	}
	fixOrder();
}

void MainWindow::setupMain(
		MsgId singlePeerShowAtMsgId,
		QPixmap oldContentCache) {
	Expects(account().sessionExists());

	const auto animated = _intro
		|| (_passcodeLock && !Core::App().passcodeLocked());
	const auto weakAnimatedLayer = (_main && _layer && !_passcodeLock)
		? Ui::MakeWeak(_layer.get())
		: nullptr;
	if (weakAnimatedLayer) {
		Assert(!animated);
		_layer->hideAllAnimatedPrepare();
	} else {
		destroyLayer();
	}
	auto created = object_ptr<MainWidget>(bodyWidget(), sessionController());
	clearWidgets();
	_main = std::move(created);
	if (const auto peer = singlePeer()) {
		updateControlsGeometry();
		_main->controller()->showPeerHistory(
			peer,
			Window::SectionShow::Way::ClearStack,
			singlePeerShowAtMsgId);
	}
	if (_passcodeLock) {
		_main->hide();
	} else {
		_main->show();
		updateControlsGeometry();
		if (animated) {
			_main->showAnimated(std::move(oldContentCache));
		} else {
			_main->activate();
		}
		Core::App().checkStartUrl();
	}
	fixOrder();
	if (const auto strong = weakAnimatedLayer.data()) {
		strong->hideAllAnimatedRun();
	}
}

void MainWindow::showSettings() {
	if (_passcodeLock) {
		return;
	}

	if (const auto session = sessionController()) {
		session->showSettings();
	} else {
		showSpecialLayer(
			Box<Settings::LayerWidget>(&controller()),
			anim::type::normal);
	}
}

void MainWindow::showSpecialLayer(
		object_ptr<Ui::LayerWidget> layer,
		anim::type animated) {
	if (_passcodeLock) {
		return;
	}

	if (layer) {
		ensureLayerCreated();
		_layer->showSpecialLayer(std::move(layer), animated);
	} else if (_layer) {
		_layer->hideSpecialLayer(animated);
	}
}

bool MainWindow::showSectionInExistingLayer(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) {
	if (_layer) {
		return _layer->showSectionInternal(memento, params);
	}
	return false;
}

void MainWindow::showMainMenu() {
	if (_passcodeLock) return;

	if (isHidden()) showFromTray();

	ensureLayerCreated();
	_layer->showMainMenu(
		object_ptr<Window::MainMenu>(body(), sessionController()),
		anim::type::normal);
}

void MainWindow::ensureLayerCreated() {
	if (_layer) {
		return;
	}
	_layer = base::make_unique_q<Ui::LayerStackWidget>(
		bodyWidget());

	_layer->hideFinishEvents(
	) | rpl::filter([=] {
		return _layer != nullptr; // Last hide finish is sent from destructor.
	}) | rpl::start_with_next([=] {
		destroyLayer();
	}, _layer->lifetime());

	if (const auto controller = sessionController()) {
		controller->enableGifPauseReason(Window::GifPauseReason::Layer);
	}
}

void MainWindow::destroyLayer() {
	if (!_layer) {
		return;
	}

	auto layer = base::take(_layer);
	const auto resetFocus = Ui::InFocusChain(layer);
	if (resetFocus) {
		setFocus();
	}
	layer = nullptr;

	if (const auto controller = sessionController()) {
		controller->disableGifPauseReason(Window::GifPauseReason::Layer);
	}
	if (resetFocus) {
		setInnerFocus();
	}
	InvokeQueued(this, [=] {
		checkActivation();
	});
}

void MainWindow::ui_hideSettingsAndLayer(anim::type animated) {
	if (animated == anim::type::instant) {
		destroyLayer();
	} else if (_layer) {
		_layer->hideAll(animated);
	}
}

void MainWindow::ui_removeLayerBlackout() {
	if (_layer) {
		_layer->removeBodyCache();
	}
}

MainWidget *MainWindow::sessionContent() const {
	return _main.data();
}

void MainWindow::showBoxOrLayer(
		std::variant<
			v::null_t,
			object_ptr<Ui::BoxContent>,
			std::unique_ptr<Ui::LayerWidget>> &&layer,
		Ui::LayerOptions options,
		anim::type animated) {
	using UniqueLayer = std::unique_ptr<Ui::LayerWidget>;
	using ObjectBox = object_ptr<Ui::BoxContent>;
	if (auto layerWidget = std::get_if<UniqueLayer>(&layer)) {
		ensureLayerCreated();
		_layer->showLayer(std::move(*layerWidget), options, animated);
	} else if (auto box = std::get_if<ObjectBox>(&layer); *box != nullptr) {
		ensureLayerCreated();
		_layer->showBox(std::move(*box), options, animated);
	} else {
		if (_layer) {
			_layer->hideTopLayer(animated);
			if ((animated == anim::type::instant)
				&& _layer
				&& !_layer->layerShown()) {
				destroyLayer();
			}
		}
		Core::App().hideMediaView();
	}
}

void MainWindow::ui_showBox(
		object_ptr<Ui::BoxContent> box,
		Ui::LayerOptions options,
		anim::type animated) {
	showBoxOrLayer(std::move(box), options, animated);
}

void MainWindow::showLayer(
		std::unique_ptr<Ui::LayerWidget> &&layer,
		Ui::LayerOptions options,
		anim::type animated) {
	showBoxOrLayer(std::move(layer), options, animated);
}

bool MainWindow::ui_isLayerShown() const {
	return _layer != nullptr;
}

bool MainWindow::showMediaPreview(
		Data::FileOrigin origin,
		not_null<DocumentData*> document) {
	const auto media = document->activeMediaView();
	const auto preview = Data::VideoPreviewState(media.get());
	if (!document->sticker()
		&& (!document->isAnimation() || !preview.loaded())) {
		return false;
	}
	if (!_mediaPreview) {
		_mediaPreview.create(bodyWidget(), sessionController());
		updateControlsGeometry();
	}
	if (_mediaPreview->isHidden()) {
		fixOrder();
	}
	_mediaPreview->showPreview(origin, document);
	return true;
}

bool MainWindow::showMediaPreview(
		Data::FileOrigin origin,
		not_null<PhotoData*> photo) {
	if (!_mediaPreview) {
		_mediaPreview.create(bodyWidget(), sessionController());
		updateControlsGeometry();
	}
	if (_mediaPreview->isHidden()) {
		fixOrder();
	}
	_mediaPreview->showPreview(origin, photo);
	return true;
}

void MainWindow::hideMediaPreview() {
	if (!_mediaPreview) {
		return;
	}
	_mediaPreview->hidePreview();
}

void MainWindow::themeUpdated(const Window::Theme::BackgroundUpdate &data) {
	using Type = Window::Theme::BackgroundUpdate::Type;

	// We delay animating theme warning because we want all other
	// subscribers to receive palette changed notification before any
	// animations (that include pixmap caches with old palette values).
	if (data.type == Type::TestingTheme) {
		if (!_testingThemeWarning) {
			_testingThemeWarning.create(bodyWidget());
			_testingThemeWarning->hide();
			_testingThemeWarning->setGeometry(rect());
			_testingThemeWarning->setHiddenCallback([this] { _testingThemeWarning.destroyDelayed(); });
		}
		crl::on_main(this, [=] {
			if (_testingThemeWarning) {
				_testingThemeWarning->showAnimated();
			}
		});
	} else if (data.type == Type::RevertingTheme || data.type == Type::ApplyingTheme) {
		if (_testingThemeWarning) {
			if (_testingThemeWarning->isHidden()) {
				_testingThemeWarning.destroy();
			} else {
				crl::on_main(this, [=] {
					if (_testingThemeWarning) {
						_testingThemeWarning->hideAnimated();
						_testingThemeWarning = nullptr;
					}
					setInnerFocus();
				});
			}
		}
	}
}

bool MainWindow::markingAsRead() const {
	return _main
		&& !_main->isHidden()
		&& !_main->animatingShow()
		&& !_layer
		&& isActive()
		&& !_main->session().updates().isIdle();
}

void MainWindow::checkActivation() {
	updateIsActive();
	if (_main) {
		_main->checkActivation();
	}
}

bool MainWindow::contentOverlapped(const QRect &globalRect) {
	return (_main && _main->contentOverlapped(globalRect))
		|| (_layer && _layer->contentOverlapped(globalRect));
}

void MainWindow::setInnerFocus() {
	if (_testingThemeWarning) {
		_testingThemeWarning->setFocus();
	} else if (_layer && _layer->canSetFocus()) {
		_layer->setInnerFocus();
	} else if (_passcodeLock) {
		_passcodeLock->setInnerFocus();
	} else if (_main) {
		_main->setInnerFocus();
	} else if (_intro) {
		_intro->setInnerFocus();
	}
}

bool MainWindow::eventFilter(QObject *object, QEvent *e) {
	switch (e->type()) {
	case QEvent::KeyPress: {
		if (Logs::DebugEnabled()
			&& object == windowHandle()) {
			const auto key = static_cast<QKeyEvent*>(e)->key();
			FeedLangTestingKey(key);
		}
#ifdef _DEBUG
		if (static_cast<QKeyEvent*>(e)->modifiers().testFlag(
				Qt::ControlModifier)) {
			switch (static_cast<QKeyEvent*>(e)->key()) {
			case Qt::Key_F11:
				anim::SetSlowMultiplier((anim::SlowMultiplier() == 10)
					? 1
					: 10);
				return true;
			case Qt::Key_F12:
				anim::SetSlowMultiplier((anim::SlowMultiplier() == 50)
					? 1
					: 50);
				return true;
			}
		}
#endif
	} break;

	case QEvent::MouseMove: {
		const auto position = static_cast<QMouseEvent*>(e)->globalPos();
		if (_lastMousePosition != position) {
			if (const auto controller = sessionController()) {
				if (controller->session().updates().isIdle()) {
					Core::App().updateNonIdle();
				}
			}
		}
		_lastMousePosition = position;
	} break;

	case QEvent::MouseButtonRelease: {
		hideMediaPreview();
	} break;

	case QEvent::ApplicationActivate: {
		if (object == QCoreApplication::instance()) {
			InvokeQueued(this, [=] {
				handleActiveChanged();
			});
		}
	} break;

	case QEvent::WindowStateChange: {
		if (object == this) {
			auto state = (windowState() & Qt::WindowMinimized) ? Qt::WindowMinimized :
				((windowState() & Qt::WindowMaximized) ? Qt::WindowMaximized :
				((windowState() & Qt::WindowFullScreen) ? Qt::WindowFullScreen : Qt::WindowNoState));
			handleStateChanged(state);
		}
	} break;

	case QEvent::Move:
	case QEvent::Resize: {
		if (object == this) {
			positionUpdated();
		}
	} break;
	}

	return Platform::MainWindow::eventFilter(object, e);
}

bool MainWindow::takeThirdSectionFromLayer() {
	return _layer ? _layer->takeToThirdSection() : false;
}

void MainWindow::fixOrder() {
	if (_passcodeLock) _passcodeLock->raise();
	if (_layer) _layer->raise();
	if (_mediaPreview) _mediaPreview->raise();
	if (_testingThemeWarning) _testingThemeWarning->raise();
}

void MainWindow::closeEvent(QCloseEvent *e) {
	if (Core::Sandbox::Instance().isSavingSession() || Core::Quitting()) {
		e->accept();
		Core::Quit();
		return;
	} else if (!isPrimary()) {
		e->accept();
		crl::on_main(this, [=] {
			Core::App().closeWindow(&controller());
		});
		return;
	}
	e->ignore();
	const auto hasAuth = [&] {
		if (!Core::App().domain().started()) {
			return false;
		}
		for (const auto &[_, account] : Core::App().domain().accounts()) {
			if (account->sessionExists()) {
				return true;
			}
		}
		return false;
	}();
	if (!hasAuth || !hideNoQuit()) {
		Core::Quit();
	}
}

void MainWindow::updateControlsGeometry() {
	Platform::MainWindow::updateControlsGeometry();

	auto body = bodyWidget()->rect();
	if (_passcodeLock) _passcodeLock->setGeometry(body);
	auto mainLeft = 0;
	auto mainWidth = body.width();
	if (const auto session = sessionController()) {
		if (const auto skip = session->filtersWidth()) {
			mainLeft += skip;
			mainWidth -= skip;
		}
	}
	if (_main) {
		_main->setGeometry({
			body.x() + mainLeft,
			body.y(),
			mainWidth,
			body.height() });
	}
	if (_intro) _intro->setGeometry(body);
	if (_layer) _layer->setGeometry(body);
	if (_mediaPreview) _mediaPreview->setGeometry(body);
	if (_testingThemeWarning) _testingThemeWarning->setGeometry(body);

	if (_main) _main->checkMainSectionToLayer();
}

void MainWindow::sendPaths() {
	if (controller().locked()) {
		return;
	}
	Core::App().hideMediaView();
	ui_hideSettingsAndLayer(anim::type::instant);
	if (_main) {
		_main->activate();
	}
}

MainWindow::~MainWindow() = default;
