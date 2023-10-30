/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/main_window.h"

#include "api/api_updates.h"
#include "storage/localstorage.h"
#include "platform/platform_specific.h"
#include "ui/platform/ui_platform_window.h"
#include "platform/platform_window_title.h"
#include "base/platform/base_platform_info.h"
#include "history/history.h"
#include "window/window_session_controller.h"
#include "window/window_lock_widgets.h"
#include "window/window_controller.h"
#include "main/main_account.h" // Account::sessionValue.
#include "main/main_domain.h"
#include "core/application.h"
#include "core/sandbox.h"
#include "core/shortcuts.h"
#include "lang/lang_keys.h"
#include "data/data_session.h"
#include "data/data_forum_topic.h"
#include "data/data_user.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "base/options.h"
#include "base/call_delayed.h"
#include "base/crc32hash.h"
#include "ui/toast/toast.h"
#include "ui/widgets/shadow.h"
#include "ui/controls/window_outdated_bar.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "apiwrap.h"
#include "mainwindow.h"
#include "mainwidget.h" // session->content()->windowShown().
#include "tray.h"
#include "styles/style_widgets.h"
#include "styles/style_window.h"
#include "styles/style_dialogs.h" // ChildSkip().x() for new child windows.

#include <QtCore/QMimeData>
#include <QtGui/QGuiApplication>
#include <QtGui/QWindow>
#include <QtGui/QScreen>
#include <QtGui/QDrag>

#include <kurlmimedata.h>

namespace Window {
namespace {

constexpr auto kSaveWindowPositionTimeout = crl::time(1000);

using Core::WindowPosition;

[[nodiscard]] QPoint ChildSkip() {
	const auto skipx = st::defaultDialogRow.padding.left()
		+ st::defaultDialogRow.photoSize
		+ st::defaultDialogRow.padding.left();
	const auto skipy = st::windowTitleHeight;
	return { skipx, skipy };
}

[[nodiscard]] QImage &OverridenIcon() {
	static auto result = QImage();
	return result;
}

} // namespace

const QImage &Logo() {
	static const auto result = QImage(u":/gui/art/logo_256.png"_q);
	return result;
}

const QImage &LogoNoMargin() {
	static const auto result = QImage(u":/gui/art/logo_256_no_margin.png"_q);
	return result;
}

void ConvertIconToBlack(QImage &image) {
	if (image.format() != QImage::Format_ARGB32_Premultiplied) {
		image = std::move(image).convertToFormat(
			QImage::Format_ARGB32_Premultiplied);
	}
	//const auto gray = red * 0.299 + green * 0.587 + blue * 0.114;
	//const auto result = (gray - 100 < 0) ? 0 : (gray - 100) * 255 / 155;
	constexpr auto scale = 255 / 155.;
	constexpr auto red = 0.299;
	constexpr auto green = 0.587;
	constexpr auto blue = 0.114;
	static constexpr auto shift = (1 << 24);
	auto shifter = [](double value) {
		return uint32(value * shift);
	};
	constexpr auto iscale = shifter(scale);
	constexpr auto ired = shifter(red);
	constexpr auto igreen = shifter(green);
	constexpr auto iblue = shifter(blue);
	constexpr auto threshold = 100;

	const auto width = image.width();
	const auto height = image.height();
	const auto data = reinterpret_cast<uint32*>(image.bits());
	const auto intsPerLine = image.bytesPerLine() / 4;
	const auto intsPerLineAdded = intsPerLine - width;

	auto pixel = data;
	for (auto j = 0; j != height; ++j) {
		for (auto i = 0; i != width; ++i) {
			const auto value = *pixel;
			const auto gray = (((value >> 16) & 0xFF) * ired
				+ ((value >> 8) & 0xFF) * igreen
				+ (value & 0xFF) * iblue) >> 24;
			const auto small = gray - threshold;
			const auto test = ~small;
			const auto result = (test >> 31) * small * iscale;
			const auto component = (result >> 24) & 0xFF;
			*pixel++ = (value & 0xFF000000U)
				| (component << 16)
				| (component << 8)
				| component;
		}
		pixel += intsPerLineAdded;
	}
}

void OverrideApplicationIcon(QImage image) {
	OverridenIcon() = std::move(image);
}

QIcon CreateOfficialIcon(Main::Session *session) {
	const auto support = (session && session->supportMode());
	if (!support) {
		return QIcon();
	}
	auto overriden = OverridenIcon();
	auto image = overriden.isNull()
		? Platform::DefaultApplicationIcon()
		: overriden;
	ConvertIconToBlack(image);
	return QIcon(Ui::PixmapFromImage(std::move(image)));
}

QIcon CreateIcon(Main::Session *session, bool returnNullIfDefault) {
	const auto officialIcon = CreateOfficialIcon(session);
	if (!officialIcon.isNull() || returnNullIfDefault) {
		return officialIcon;
	}

	auto result = QIcon(Ui::PixmapFromImage(base::duplicate(Logo())));

	if constexpr (!Platform::IsLinux()) {
		return result;
	}

	const auto iconFromTheme = QIcon::fromTheme(
		base::IconName(),
		result);

	result = QIcon();

	static const auto iconSizes = {
		16,
		22,
		32,
		48,
		64,
		128,
		256,
	};

	// Qt's standard QIconLoaderEngine sets availableSizes
	// to XDG directories sizes, since svg icons are scalable,
	// they could be only in one XDG folder (like 48x48)
	// and Qt will set only a 48px icon to the window
	// even though the icon could be scaled to other sizes.
	// Thus, scale it manually to the most widespread sizes.
	for (const auto iconSize : iconSizes) {
		// We can't use QIcon::actualSize here
		// since it works incorrectly with svg icon themes
		const auto iconPixmap = iconFromTheme.pixmap(iconSize);

		const auto iconPixmapSize = iconPixmap.size()
			/ iconPixmap.devicePixelRatio();

		// Not a svg icon, don't scale it
		if (iconPixmapSize.width() != iconSize) {
			return iconFromTheme;
		}

		result.addPixmap(iconPixmap);
	}

	return result;
}

QImage GenerateCounterLayer(CounterLayerArgs &&args) {
	// platform/linux/main_window_linux depends on count used the same
	// way for all the same (count % 1000) values.
	const auto count = args.count.value();
	const auto text = (count < 1000)
		? QString::number(count)
		: u"..%1"_q.arg(count % 100, 2, 10, QChar('0'));
	const auto textSize = text.size();

	struct Dimensions {
		int size = 0;
		int font = 0;
		int delta = 0;
		int radius = 0;
	};
	const auto d = [&]() -> Dimensions {
		switch (args.size.value()) {
		case 16:
			return {
				.size = 16,
				.font = ((textSize < 2) ? 11 : (textSize < 3) ? 11 : 8),
				.delta = ((textSize < 2) ? 5 : (textSize < 3) ? 2 : 1),
				.radius = ((textSize < 2) ? 8 : (textSize < 3) ? 7 : 3),
			};
		case 20:
			return {
				.size = 20,
				.font = ((textSize < 2) ? 14 : (textSize < 3) ? 13 : 10),
				.delta = ((textSize < 2) ? 6 : (textSize < 3) ? 2 : 1),
				.radius = ((textSize < 2) ? 10 : (textSize < 3) ? 9 : 5),
			};
		case 24:
			return {
				.size = 24,
				.font = ((textSize < 2) ? 17 : (textSize < 3) ? 16 : 12),
				.delta = ((textSize < 2) ? 7 : (textSize < 3) ? 3 : 1),
				.radius = ((textSize < 2) ? 12 : (textSize < 3) ? 11 : 6),
			};
		default:
			return {
				.size = 32,
				.font = ((textSize < 2) ? 22 : (textSize < 3) ? 20 : 16),
				.delta = ((textSize < 2) ? 9 : (textSize < 3) ? 4 : 2),
				.radius = ((textSize < 2) ? 16 : (textSize < 3) ? 14 : 8),
			};
		}
	}();

	auto result = QImage(
		QSize(d.size, d.size) * args.devicePixelRatio,
		QImage::Format_ARGB32);
	result.setDevicePixelRatio(args.devicePixelRatio);
	result.fill(Qt::transparent);

	auto p = QPainter(&result);
	auto hq = PainterHighQualityEnabler(p);
	const auto f = style::font{ d.font, 0, 0 };
	const auto w = f->width(text);

	p.setBrush(args.bg.value());
	p.setPen(Qt::NoPen);
	p.drawRoundedRect(
		QRect(
			d.size - w - d.delta * 2,
			d.size - f->height,
			w + d.delta * 2,
			f->height),
		d.radius,
		d.radius);

	p.setFont(f);
	p.setPen(args.fg.value());
	p.drawText(d.size - w - d.delta, d.size - f->height + f->ascent, text);
	p.end();

	return result;
}

QImage WithSmallCounter(QImage image, CounterLayerArgs &&args) {
	const auto count = args.count.value();
	const auto text = (count < 100)
		? QString::number(count)
		: QString("..%1").arg(count % 10, 1, 10, QChar('0'));
	const auto textSize = text.size();

	struct Dimensions {
		int size = 0;
		int font = 0;
		int delta = 0;
		int radius = 0;
	};
	const auto d = Dimensions{
		.size = args.size.value(),
		.font = args.size.value() / 2,
		.delta = args.size.value() / ((textSize < 2) ? 8 : 16),
		.radius = args.size.value() / ((textSize < 2) ? 4 : 5),
	};

	auto p = QPainter(&image);
	auto hq = PainterHighQualityEnabler(p);
	const auto f = style::font{ d.font, 0, 0 };
	const auto w = f->width(text);

	p.setBrush(args.bg.value());
	p.setPen(Qt::NoPen);
	p.drawRoundedRect(
		QRect(
			d.size - w - d.delta * 2,
			d.size - f->height,
			w + d.delta * 2,
			f->height),
		d.radius,
		d.radius);

	p.setFont(f);
	p.setPen(args.fg.value());
	p.drawText(d.size - w - d.delta, d.size - f->height + f->ascent, text);
	p.end();

	return image;
}

MainWindow::MainWindow(not_null<Controller*> controller)
: _controller(controller)
, _positionUpdatedTimer([=] { savePosition(); })
, _outdated(Ui::CreateOutdatedBar(body(), cWorkingDir()))
, _body(body()) {
	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		updatePalette();
	}, lifetime());

	Core::App().unreadBadgeChanges(
	) | rpl::start_with_next([=] {
		updateTitle();
		unreadCounterChangedHook();
		Core::App().tray().updateIconCounters();
	}, lifetime());

	Core::App().settings().workModeChanges(
	) | rpl::start_with_next([=](Core::Settings::WorkMode mode) {
		workmodeUpdated(mode);
	}, lifetime());

	if (isPrimary()) {
		Ui::Toast::SetDefaultParent(_body.data());
	}

	body()->sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		updateControlsGeometry();
	}, lifetime());

	if (_outdated) {
		_outdated->heightValue(
		) | rpl::start_with_next([=](int height) {
			if (!height) {
				crl::on_main(this, [=] { _outdated.destroy(); });
			}
			updateControlsGeometry();
		}, _outdated->lifetime());
	}

	Shortcuts::Listen(this);
}

Main::Account &MainWindow::account() const {
	return _controller->account();
}

PeerData *MainWindow::singlePeer() const {
	return _controller->singlePeer();
}

bool MainWindow::isPrimary() const {
	return _controller->isPrimary();
}

Window::SessionController *MainWindow::sessionController() const {
	return _controller->sessionController();
}

bool MainWindow::hideNoQuit() {
	if (Core::Quitting()) {
		return false;
	}
	const auto workMode = Core::App().settings().workMode();
	if (workMode == Core::Settings::WorkMode::TrayOnly
		|| workMode == Core::Settings::WorkMode::WindowAndTray) {
		if (minimizeToTray()) {
			if (const auto controller = sessionController()) {
				controller->clearSectionStack();
			}
			return true;
		}
	}
	if (Platform::RunInBackground() || Core::App().settings().closeToTaskbar()) {
		if (Platform::RunInBackground()) {
			closeWithoutDestroy();
		} else {
			setWindowState(window()->windowState() | Qt::WindowMinimized);
		}
		controller().updateIsActiveBlur();
		updateGlobalMenu();
		if (const auto controller = sessionController()) {
			controller->clearSectionStack();
		}
		return true;
	}
	return false;
}

void MainWindow::clearWidgets() {
	clearWidgetsHook();
	updateGlobalMenu();
}

void MainWindow::updateIsActive() {
	const auto isActive = computeIsActive();
	if (_isActive != isActive) {
		_isActive = isActive;
	}
}

bool MainWindow::computeIsActive() const {
	return isActiveWindow() && isVisible() && !(windowState() & Qt::WindowMinimized);
}

QRect MainWindow::desktopRect() const {
	const auto now = crl::now();
	if (!_monitorLastGot || now >= _monitorLastGot + crl::time(1000)) {
		_monitorLastGot = now;
		_monitorRect = computeDesktopRect();
	}
	return _monitorRect;
}

void MainWindow::init() {
	createWinId();

	initHook();

	// Non-queued activeChanged handlers must use QtSignalProducer.
	connect(
		windowHandle(),
		&QWindow::activeChanged,
		this,
		[=] { handleActiveChanged(); },
		Qt::QueuedConnection);
	connect(
		windowHandle(),
		&QWindow::windowStateChanged,
		this,
		[=](Qt::WindowState state) { handleStateChanged(state); });
	connect(
		windowHandle(),
		&QWindow::visibleChanged,
		this,
		[=](bool visible) { handleVisibleChanged(visible); });

	updatePalette();

	if (Ui::Platform::NativeWindowFrameSupported()) {
		Core::App().settings().nativeWindowFrameChanges(
		) | rpl::start_with_next([=](bool native) {
			refreshTitleWidget();
			recountGeometryConstraints();
		}, lifetime());
	}
	refreshTitleWidget();

	initGeometry();
	updateTitle();
	updateWindowIcon();
}

void MainWindow::handleStateChanged(Qt::WindowState state) {
	stateChangedHook(state);
	updateControlsGeometry();
	if (state == Qt::WindowMinimized) {
		controller().updateIsActiveBlur();
	} else {
		controller().updateIsActiveFocus();
	}
	Core::App().updateNonIdle();
	using WorkMode = Core::Settings::WorkMode;
	if (state == Qt::WindowMinimized
		&& (Core::App().settings().workMode() == WorkMode::TrayOnly)) {
		minimizeToTray();
	}
	savePosition(state);
}

void MainWindow::handleActiveChanged() {
	checkActivation();
	if (isActiveWindow()) {
		Core::App().windowActivated(&controller());
	}
	if (const auto controller = sessionController()) {
		controller->session().updates().updateOnline();
	}
}

void MainWindow::handleVisibleChanged(bool visible) {
	if (visible) {
		if (_maximizedBeforeHide) {
			DEBUG_LOG(("Window Pos: Window was maximized before hidding, setting maximized."));
			setWindowState(Qt::WindowMaximized);
		}
	} else {
		_maximizedBeforeHide = Core::App().settings().windowPosition().maximized;
	}

	handleVisibleChangedHook(visible);
}

void MainWindow::showFromTray() {
	InvokeQueued(this, [=] {
		updateGlobalMenu();
	});
	activate();
	unreadCounterChangedHook();
	Core::App().tray().updateIconCounters();
}

void MainWindow::quitFromTray() {
	Core::Quit();
}

void MainWindow::activate() {
	bool wasHidden = !isVisible();
	setWindowState(windowState() & ~Qt::WindowMinimized);
	setVisible(true);
	Platform::ActivateThisProcess();
	raise();
	activateWindow();
	controller().updateIsActiveFocus();
	if (wasHidden) {
		if (const auto session = sessionController()) {
			session->content()->windowShown();
		}
	}
}

void MainWindow::updatePalette() {
	Ui::ForceFullRepaint(this);

	auto p = palette();
	p.setColor(QPalette::Window, st::windowBg->c);
	setPalette(p);
}

int MainWindow::computeMinWidth() const {
	auto result = st::windowMinWidth;
	if (const auto session = _controller->sessionController()) {
		if (const auto add = session->filtersWidth()) {
			result += add;
		}
	}
	if (_rightColumn) {
		result += _rightColumn->width();
	}
	return result;
}

int MainWindow::computeMinHeight() const {
	const auto outdated = [&] {
		if (!_outdated) {
			return 0;
		}
		_outdated->resizeToWidth(st::windowMinWidth);
		return _outdated->height();
	}();
	return outdated + st::windowMinHeight;
}

void MainWindow::refreshTitleWidget() {
	if (Ui::Platform::NativeWindowFrameSupported()
		&& Core::App().settings().nativeWindowFrame()) {
		setNativeFrame(true);
		if (Platform::NativeTitleRequiresShadow()) {
			_titleShadow.create(this);
			_titleShadow->show();
		}
	} else {
		setNativeFrame(false);
		_titleShadow.destroy();
	}
}

void MainWindow::updateMinimumSize() {
	setMinimumSize(QSize(computeMinWidth(), computeMinHeight()));
}

void MainWindow::recountGeometryConstraints() {
	updateMinimumSize();
	updateControlsGeometry();
	fixOrder();
}

WindowPosition MainWindow::initialPosition() const {
	const auto active = Core::App().activeWindow();
	return (!active || active == &controller())
		? Core::AdjustToScale(
			Core::App().settings().windowPosition(),
			u"Window"_q)
		: active->widget()->nextInitialChildPosition(isPrimary());
}

WindowPosition MainWindow::nextInitialChildPosition(bool primary) {
	const auto rect = geometry().marginsRemoved(frameMargins());
	const auto position = rect.topLeft();
	const auto adjust = [&](int value) {
		return primary ? value : (value * 3 / 4);
	};
	const auto width = adjust(st::windowDefaultWidth);
	const auto height = adjust(st::windowDefaultHeight);
	const auto skip = ChildSkip();
	const auto delta = _lastChildIndex
		? (_lastMyChildCreatePosition - position)
		: skip;
	if (qAbs(delta.x()) >= skip.x() || qAbs(delta.y()) >= skip.y()) {
		_lastChildIndex = 1;
	} else {
		++_lastChildIndex;
	}

	_lastMyChildCreatePosition = position;
	const auto use = position + (skip * _lastChildIndex);
	return withScreenInPosition({
		.scale = cScale(),
		.x = use.x(),
		.y = use.y(),
		.w = width,
		.h = height,
	});
}

QRect MainWindow::countInitialGeometry(WindowPosition position) {
	const auto primaryScreen = QGuiApplication::primaryScreen();
	const auto primaryAvailable = primaryScreen
		? primaryScreen->availableGeometry()
		: QRect(0, 0, st::windowDefaultWidth, st::windowDefaultHeight);
	const auto initialWidth = Core::Settings::ThirdColumnByDefault()
		? st::windowBigDefaultWidth
		: st::windowDefaultWidth;
	const auto initialHeight = Core::Settings::ThirdColumnByDefault()
		? st::windowBigDefaultHeight
		: st::windowDefaultHeight;
	const auto initial = WindowPosition{
		.x = (primaryAvailable.x()
			+ std::max((primaryAvailable.width() - initialWidth) / 2, 0)),
		.y = (primaryAvailable.y()
			+ std::max((primaryAvailable.height() - initialHeight) / 2, 0)),
		.w = initialWidth,
		.h = initialHeight,
	};
	return countInitialGeometry(
		position,
		initial,
		{ st::windowMinWidth, st::windowMinHeight });
}

QRect MainWindow::countInitialGeometry(
		WindowPosition position,
		WindowPosition initial,
		QSize minSize) const {
	if (!position.w || !position.h) {
		return initial.rect();
	}
	const auto screen = [&]() -> QScreen* {
		for (const auto screen : QGuiApplication::screens()) {
			const auto sum = Platform::ScreenNameChecksum(screen->name());
			if (position.moncrc == sum) {
				return screen;
			}
		}
		return nullptr;
	}();
	if (!screen) {
		return initial.rect();
	}
	const auto frame = frameMargins();
	const auto screenGeometry = screen->geometry();
	const auto availableGeometry = screen->availableGeometry();
	const auto spaceForInner = availableGeometry.marginsRemoved(frame);
	DEBUG_LOG(("Window Pos: "
		"Screen found, screen geometry: %1, %2, %3, %4, "
		"available: %5, %6, %7, %8"
		).arg(screenGeometry.x()
		).arg(screenGeometry.y()
		).arg(screenGeometry.width()
		).arg(screenGeometry.height()
		).arg(availableGeometry.x()
		).arg(availableGeometry.y()
		).arg(availableGeometry.width()
		).arg(availableGeometry.height()));
	DEBUG_LOG(("Window Pos: "
		"Window frame margins: %1, %2, %3, %4, "
		"available space for inner geometry: %5, %6, %7, %8"
		).arg(frame.left()
		).arg(frame.top()
		).arg(frame.right()
		).arg(frame.bottom()
		).arg(spaceForInner.x()
		).arg(spaceForInner.y()
		).arg(spaceForInner.width()
		).arg(spaceForInner.height()));

	const auto x = spaceForInner.x() - screenGeometry.x();
	const auto y = spaceForInner.y() - screenGeometry.y();
	const auto w = spaceForInner.width();
	const auto h = spaceForInner.height();
	if (w < st::windowMinWidth || h < st::windowMinHeight) {
		return initial.rect();
	}
	if (position.x < x) position.x = x;
	if (position.y < y) position.y = y;
	if (position.w > w) position.w = w;
	if (position.h > h) position.h = h;
	const auto rightPoint = position.x + position.w;
	const auto screenRightPoint = x + w;
	if (rightPoint > screenRightPoint) {
		const auto distance = rightPoint - screenRightPoint;
		const auto newXPos = position.x - distance;
		if (newXPos >= x) {
			position.x = newXPos;
		} else {
			position.x = x;
			const auto newRightPoint = position.x + position.w;
			const auto newDistance = newRightPoint - screenRightPoint;
			position.w -= newDistance;
		}
	}
	const auto bottomPoint = position.y + position.h;
	const auto screenBottomPoint = y + h;
	if (bottomPoint > screenBottomPoint) {
		const auto distance = bottomPoint - screenBottomPoint;
		const auto newYPos = position.y - distance;
		if (newYPos >= y) {
			position.y = newYPos;
		} else {
			position.y = y;
			const auto newBottomPoint = position.y + position.h;
			const auto newDistance = newBottomPoint - screenBottomPoint;
			position.h -= newDistance;
		}
	}
	position.x += screenGeometry.x();
	position.y += screenGeometry.y();
	if ((position.x + st::windowMinWidth
		> screenGeometry.x() + screenGeometry.width())
		|| (position.y + st::windowMinHeight
			> screenGeometry.y() + screenGeometry.height())) {
		return initial.rect();
	}
	DEBUG_LOG(("Window Pos: Resulting geometry is %1, %2, %3, %4"
		).arg(position.x
		).arg(position.y
		).arg(position.w
		).arg(position.h));
	return position.rect();
}

void MainWindow::initGeometry() {
	updateMinimumSize();
	if (initGeometryFromSystem()) {
		return;
	}
	const auto geometry = countInitialGeometry(initialPosition());
	DEBUG_LOG(("Window Pos: Setting first %1, %2, %3, %4"
		).arg(geometry.x()
		).arg(geometry.y()
		).arg(geometry.width()
		).arg(geometry.height()));
	setGeometry(geometry);
}

void MainWindow::positionUpdated() {
	_positionUpdatedTimer.callOnce(kSaveWindowPositionTimeout);
}

void MainWindow::setPositionInited() {
	_positionInited = true;
}

rpl::producer<> MainWindow::leaveEvents() const {
	return _leaveEvents.events();
}

void MainWindow::leaveEventHook(QEvent *e) {
	_leaveEvents.fire({});
}

void MainWindow::updateControlsGeometry() {
	const auto inner = body()->rect();
	auto bodyLeft = inner.x();
	auto bodyTop = inner.y();
	auto bodyWidth = inner.width();
	if (_titleShadow) {
		_titleShadow->setGeometry(inner.x(), bodyTop, inner.width(), st::lineWidth);
	}
	if (_outdated) {
		Ui::SendPendingMoveResizeEvents(_outdated.data());
		_outdated->resizeToWidth(inner.width());
		_outdated->moveToLeft(inner.x(), bodyTop);
		bodyTop += _outdated->height();
	}
	if (_rightColumn) {
		bodyWidth -= _rightColumn->width();
		_rightColumn->setGeometry(bodyWidth, bodyTop, inner.width() - bodyWidth, inner.height() - (bodyTop - inner.y()));
	}
	_body->setGeometry(bodyLeft, bodyTop, bodyWidth, inner.height() - (bodyTop - inner.y()));
}

void MainWindow::updateTitle() {
	if (Core::Quitting()) {
		return;
	}

	const auto settings = Core::App().settings().windowTitleContent();
	const auto locked = Core::App().passcodeLocked();
	const auto counter = settings.hideTotalUnread
		? 0
		: Core::App().unreadBadge();
	const auto added = (counter > 0) ? u" (%1)"_q.arg(counter) : QString();
	const auto session = locked ? nullptr : _controller->sessionController();
	const auto user = (session
		&& !settings.hideAccountName
		&& Core::App().domain().accountsAuthedCount() > 1)
		? session->authedName()
		: QString();
	const auto key = (session && !settings.hideChatName)
		? session->activeChatCurrent()
		: Dialogs::Key();
	const auto thread = key ? key.thread() : nullptr;
	if (!thread) {
		setTitle((user.isEmpty() ? u"Telegram"_q : user) + added);
		return;
	}
	const auto history = thread->owningHistory();
	const auto topic = thread->asTopic();
	const auto name = topic
		? topic->title()
		: history->peer->isSelf()
		? tr::lng_saved_messages(tr::now)
		: history->peer->name();
	const auto threadCounter = thread->chatListBadgesState().unreadCounter;
	const auto primary = (threadCounter > 0)
		? u"(%1) %2"_q.arg(threadCounter).arg(name)
		: name;
	const auto middle = !user.isEmpty()
		? (u" @ "_q + user)
		: !added.isEmpty()
		? u" \u2013"_q
		: QString();
	setTitle(primary + middle + added);
}

QRect MainWindow::computeDesktopRect() const {
	return screen()->availableGeometry();
}

void MainWindow::savePosition(Qt::WindowState state) {
	if (state == Qt::WindowActive) {
		state = windowHandle()->windowState();
	}

	if (state == Qt::WindowMinimized
		|| !isVisible()
		|| !Core::App().savingPositionFor(&controller())
		|| !positionInited()) {
		return;
	}

	const auto &savedPosition = Core::App().settings().windowPosition();
	auto realPosition = savedPosition;

	if (state == Qt::WindowMaximized) {
		realPosition.maximized = 1;
		DEBUG_LOG(("Window Pos: Saving maximized position."));
	} else {
		auto r = body()->mapToGlobal(body()->rect());
		realPosition.x = r.x();
		realPosition.y = r.y();
		realPosition.w = r.width() - (_rightColumn ? _rightColumn->width() : 0);
		realPosition.h = r.height();
		realPosition.scale = cScale();
		realPosition.maximized = 0;
		realPosition.moncrc = 0;

		DEBUG_LOG(("Window Pos: Saving non-maximized position: %1, %2, %3, %4").arg(realPosition.x).arg(realPosition.y).arg(realPosition.w).arg(realPosition.h));
		realPosition = withScreenInPosition(realPosition);
	}
	if (realPosition.w >= st::windowMinWidth && realPosition.h >= st::windowMinHeight) {
		if (realPosition.x != savedPosition.x
			|| realPosition.y != savedPosition.y
			|| realPosition.w != savedPosition.w
			|| realPosition.h != savedPosition.h
			|| realPosition.scale != savedPosition.scale
			|| realPosition.moncrc != savedPosition.moncrc
			|| realPosition.maximized != savedPosition.maximized) {
			DEBUG_LOG(("Window Pos: Writing: %1, %2, %3, %4 (scale %5%, maximized %6)")
				.arg(realPosition.x)
				.arg(realPosition.y)
				.arg(realPosition.w)
				.arg(realPosition.h)
				.arg(realPosition.scale)
				.arg(Logs::b(realPosition.maximized)));
			Core::App().settings().setWindowPosition(realPosition);
			Core::App().saveSettingsDelayed();
		}
	}
}

WindowPosition MainWindow::withScreenInPosition(
		WindowPosition position) const {
	return PositionWithScreen(
		position,
		this,
		{ st::windowMinWidth, st::windowMinHeight });
}

bool MainWindow::minimizeToTray() {
	if (Core::Quitting() || !Core::App().tray().has()) {
		return false;
	}

	closeWithoutDestroy();
	controller().updateIsActiveBlur();
	updateGlobalMenu();
	return true;
}

void MainWindow::reActivateWindow() {
	// X11 is the only platform with unreliable activate requests
	if (!Platform::IsX11()) {
		return;
	}
	const auto weak = Ui::MakeWeak(this);
	const auto reActivate = [=] {
		if (const auto w = weak.data()) {
			if (auto f = QApplication::focusWidget()) {
				f->clearFocus();
			}
			w->activate();
			if (auto f = QApplication::focusWidget()) {
				f->clearFocus();
			}
			w->setInnerFocus();
		}
	};
	crl::on_main(this, reActivate);
	base::call_delayed(200, this, reActivate);
}

void MainWindow::showRightColumn(object_ptr<TWidget> widget) {
	const auto wasWidth = width();
	const auto wasRightWidth = _rightColumn ? _rightColumn->width() : 0;
	_rightColumn = std::move(widget);
	if (_rightColumn) {
		_rightColumn->setParent(body());
		_rightColumn->show();
		_rightColumn->setFocus();
	} else {
		setInnerFocus();
	}
	const auto nowRightWidth = _rightColumn ? _rightColumn->width() : 0;
	const auto wasMinimumWidth = minimumWidth();
	const auto nowMinimumWidth = computeMinWidth();
	const auto firstResize = (nowMinimumWidth < wasMinimumWidth);
	if (firstResize) {
		updateMinimumSize();
	}
	if (!isMaximized()) {
		tryToExtendWidthBy(wasWidth + nowRightWidth - wasRightWidth - width());
	} else {
		updateControlsGeometry();
	}
	if (!firstResize) {
		updateMinimumSize();
	}
}

int MainWindow::maximalExtendBy() const {
	auto desktop = screen()->availableGeometry();
	return std::max(desktop.width() - body()->width(), 0);
}

bool MainWindow::canExtendNoMove(int extendBy) const {
	auto desktop = screen()->availableGeometry();
	auto inner = body()->mapToGlobal(body()->rect());
	auto innerRight = (inner.x() + inner.width() + extendBy);
	auto desktopRight = (desktop.x() + desktop.width());
	return innerRight <= desktopRight;
}

int MainWindow::tryToExtendWidthBy(int addToWidth) {
	auto desktop = screen()->availableGeometry();
	auto inner = body()->mapToGlobal(body()->rect());
	accumulate_min(
		addToWidth,
		std::max(desktop.width() - inner.width(), 0));
	auto newWidth = inner.width() + addToWidth;
	auto newLeft = std::min(
		inner.x(),
		desktop.x() + desktop.width() - newWidth);
	if (inner.x() != newLeft || inner.width() != newWidth) {
		setGeometry(QRect(newLeft, inner.y(), newWidth, inner.height()));
	} else {
		updateControlsGeometry();
	}
	return addToWidth;
}

void MainWindow::launchDrag(
		std::unique_ptr<QMimeData> data,
		Fn<void()> &&callback) {
	// Qt destroys this QDrag automatically after the drag is finished
	// We must not delete this at the end of this function, as this breaks DnD on Linux
	auto drag = new QDrag(this);
	KUrlMimeData::exportUrlsToPortal(data.get());
	drag->setMimeData(data.release());
	drag->exec(Qt::CopyAction);

	// We don't receive mouseReleaseEvent when drag is finished.
	ClickHandler::unpressed();
	callback();
}

MainWindow::~MainWindow() {
	// Otherwise:
	// ~QWidget
	// QWidgetPrivate::close_helper
	// QWidgetPrivate::setVisible
	// QWidgetPrivate::hide_helper
	// QWidgetPrivate::hide_sys
	// QWindowPrivate::setVisible
	// QMetaObject::activate
	// Window::MainWindow::handleVisibleChanged on a destroyed MainWindow.
	hide();
}

int32 DefaultScreenNameChecksum(const QString &name) {
	const auto bytes = name.toUtf8();
	return base::crc32(bytes.constData(), bytes.size());
}

WindowPosition PositionWithScreen(
		WindowPosition position,
		const QScreen *chosen,
		QSize minimal) {
	if (!chosen) {
		return position;
	}
	const auto available = chosen->availableGeometry();
	if (available.width() < minimal.width()
		|| available.height() < minimal.height()) {
		return position;
	}
	accumulate_min(position.w, available.width());
	accumulate_min(position.h, available.height());
	if (position.x + position.w > available.x() + available.width()) {
		position.x = available.x() + available.width() - position.w;
	}
	if (position.y + position.h > available.y() + available.height()) {
		position.y = available.y() + available.height() - position.h;
	}
	const auto geometry = chosen->geometry();
	DEBUG_LOG(("Window Pos: Screen found, geometry: %1, %2, %3, %4"
		).arg(geometry.x()
		).arg(geometry.y()
		).arg(geometry.width()
		).arg(geometry.height()));
	position.x -= geometry.x();
	position.y -= geometry.y();
	position.moncrc = Platform::ScreenNameChecksum(chosen->name());
	return position;
}

WindowPosition PositionWithScreen(
		WindowPosition position,
		not_null<const QWidget*> widget,
		QSize minimal) {
	const auto screen = widget->screen();
	return PositionWithScreen(
		position,
		screen ? screen : QGuiApplication::primaryScreen(),
		minimal);
}

} // namespace Window
