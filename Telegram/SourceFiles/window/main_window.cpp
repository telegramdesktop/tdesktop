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
#include "ui/platform/ui_platform_window_title.h"
#include "platform/platform_window_title.h"
#include "history/history.h"
#include "info/media/info_media_widget.h" // SharedMediaTitle.
#include "window/window_saved_windows.h"
#include "window/window_separate_id.h"
#include "window/window_session_controller.h"
#include "window/window_lock_widgets.h"
#include "window/window_controller.h"
#include "main/main_account.h" // Account::sessionValue.
#include "main/main_domain.h"
#include "core/application.h"
#include "core/sandbox.h"
#include "core/shortcuts.h"
#include "core/update_channel.h"
#include "lang/lang_keys.h"
#include "data/data_session.h"
#include "data/data_forum_topic.h"
#include "data/data_saved_sublist.h"
#include "data/data_user.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "base/options.h"
#include "base/crc32hash.h"
#include "ui/boxes/confirm_box.h"
#include "ui/toast/toast.h"
#include "ui/widgets/shadow.h"
#include "ui/controls/title_sub_widget.h"
#include "ui/controls/window_outdated_bar.h"
#include "ui/controls/window_screen_reader_bar.h"
#include "ui/painter.h"
#include "ui/screen_reader_mode.h"
#include "ui/ui_utility.h"
#include "ui/unread_counter_format.h"
#include "apiwrap.h"
#include "mainwidget.h" // session->content()->windowShown().
#include "tray.h"
#include "styles/style_window.h"
#include "styles/style_dialogs.h" // ChildSkip().x() for new child windows.

#ifdef Q_OS_MAC
#include "platform/mac/global_menu_mac.h"
#endif // Q_OS_MAC

#include <QtCore/QMimeData>
#include <QtGui/QWindow>
#include <QtGui/QScreen>
#include <QtGui/QDrag>

#include <kurlmimedata.h>

namespace Window {
namespace {

constexpr auto kSaveWindowPositionTimeout = crl::time(1000);

using Core::WindowPosition;

[[nodiscard]] QRect ScreenAvailableGeometry(not_null<const QWidget*> widget) {
	// When the last monitor is removed Qt keeps delivering resize events
	// while QGuiApplication has no screens at all, so screen() is nullptr.
	const auto screen = widget->screen();
	return screen ? Platform::ScreenAvailableGeometry(screen) : QRect();
}

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

base::options::toggle OptionNewWindowsSizeAsFirst({
	.id = kOptionNewWindowsSizeAsFirst,
	.name = "Adjust size of new chat windows",
	.description = "Open new windows with a size of the main window.",
});

base::options::toggle OptionDisableTouchbar({
	.id = kOptionDisableTouchbar,
	.name = "Disable Touch Bar (macOS only).",
#ifdef Q_OS_MAC
	.defaultValue = !Platform::HasTouchBar(),
#endif // Q_OS_MAC
	.scope = [] {
#ifdef Q_OS_MAC
		return true;
#else // !Q_OS_MAC
		return false;
#endif // !Q_OS_MAC
	},
	.restartRequired = true,
});

[[nodiscard]] QString TitleFromSeparateSharedMedia(
		const Core::WindowTitleContent &settings,
		const SeparateId &id) {
	if (id.type != SeparateType::SharedMedia) {
		return QString();
	}
	const auto type = id.sharedMediaType;
	const auto result = Info::Media::SharedMediaTitle(type)(tr::now);
	if (settings.hideChatName) {
		return result;
	}
	const auto thread = id.thread;
	const auto topic = thread->asTopic();
	const auto sublist = thread->asSublist();
	const auto peer = sublist ? sublist->sublistPeer() : thread->peer();
	const auto name = topic
		? topic->title()
		: peer->isSelf()
		? tr::lng_saved_messages(tr::now)
		: peer->name();
	const auto wrapped = st::wrap_rtl(name);
	return name + u" @ "_q + result;
}

} // namespace

const char kOptionNewWindowsSizeAsFirst[] = "new-windows-size-as-first";
const char kOptionDisableTouchbar[] = "touchbar-disabled";

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

QIcon CreateSupportIcon(Main::Session *session) {
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
	const auto supportIcon = CreateSupportIcon(session);
	if (!supportIcon.isNull() || returnNullIfDefault) {
		return supportIcon;
	}

	const auto officialIcon = QIcon(
		Ui::PixmapFromImage(base::duplicate(Logo())));

	if constexpr (!Platform::IsLinux()) {
		return officialIcon;
	}

	const auto iconFromTheme = QIcon::fromTheme(
		Platform::ApplicationIconName(),
		officialIcon);

	if (!Platform::IsX11()) {
		return iconFromTheme;
	}

	QIcon result;

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
	const auto text = FormatUnreadCounterShort(args.count.value());
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
	auto font = d.font;
	auto f = style::font{ font, 0, 0 };
	const auto height = f->height;
	const auto skip = std::max(d.delta - 1, 1);
	while (font > 1 && f->width(text) + d.delta + skip > d.size) {
		f = style::font{ --font, 0, 0 };
	}
	const auto w = f->width(text);
	const auto left = std::max(d.size - w - d.delta * 2, 0);
	const auto radius = std::min(
		d.radius * f->height / float64(height),
		f->height / 2.);

	p.setBrush(args.bg.value());
	p.setPen(Qt::NoPen);
	p.drawRoundedRect(
		QRect(
			left,
			d.size - f->height,
			d.size - left,
			f->height),
		radius,
		radius);

	p.setFont(f);
	p.setPen(args.fg.value());
	p.drawText(d.size - w - d.delta, d.size - f->height + f->ascent, text);
	p.end();

	return result;
}

QImage WithSmallCounter(QImage image, CounterLayerArgs &&args) {
	// platform/linux/tray_linux depends on count used the same
	// way for all the same (count % 100) values.
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
, _screenReaderBar(Ui::CreateScreenReaderBar(body(), [=] {
	controller->show(Ui::MakeConfirmBox({
		.text = tr::lng_screen_reader_confirm_text(tr::now),
		.confirmed = [=](Fn<void()> close) {
			Core::App().settings().writePref<bool>(
				Core::kScreenReaderModeDisabledKey,
				true);
			Core::App().saveSettingsDelayed();
			Ui::SetScreenReaderModeDisabled(true);
			close();
		},
		.confirmText = tr::lng_screen_reader_confirm_disable(),
	}));
}))
, _body(body()) {
	style::PaletteChanged(
	) | rpl::on_next([=] {
		updatePalette();
	}, lifetime());

	Core::App().unreadBadgeChanges(
	) | rpl::on_next([=] {
		updateTitle();
		unreadCounterChangedHook();
		Core::App().tray().updateIconCounters();
	}, lifetime());

	Core::App().settings().workModeChanges(
	) | rpl::on_next([=](Core::Settings::WorkMode mode) {
		workmodeUpdated(mode);
	}, lifetime());

	if (isPrimary()) {
		Ui::Toast::SetDefaultParent(_body.data());
	}

	windowActiveValue(
	) | rpl::skip(1) | rpl::on_next([=](bool active) {
		InvokeQueued(this, [=] {
			handleActiveChanged(active);
		});
	}, lifetime());

	shownValue(
	) | rpl::skip(1) | rpl::on_next([=](bool visible) {
		InvokeQueued(this, [=] {
			handleVisibleChanged(visible);
		});
	}, lifetime());

	body()->sizeValue(
	) | rpl::on_next([=](QSize size) {
		updateControlsGeometry();
	}, lifetime());

	if (_outdated) {
		_outdated->heightValue(
		) | rpl::on_next([=](int height) {
			if (!height) {
				crl::on_main(this, [=] { _outdated.destroy(); });
			}
			updateControlsGeometry();
		}, _outdated->lifetime());
	}

	if (_screenReaderBar) {
		_screenReaderBar->heightValue(
		) | rpl::on_next([=](int height) {
			updateControlsGeometry();
		}, _screenReaderBar->lifetime());
	}

	Shortcuts::Listen(this);
}

Main::Account &MainWindow::account() const {
	return _controller->account();
}

Window::SeparateId MainWindow::id() const {
	return _controller->id();
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
	using Mode = Core::Settings::WorkMode;
	if (workMode == Mode::TrayOnly || workMode == Mode::WindowAndTray) {
		if (minimizeToTray()) {
			if (const auto controller = sessionController()) {
				controller->clearSectionStack();
			}
			return true;
		}
	}
	using Behavior = Core::Settings::CloseBehavior;
	const auto behavior = Platform::IsMac()
		? Behavior::RunInBackground
		: Core::App().settings().closeBehavior();
	if (behavior == Behavior::RunInBackground) {
		closeWithoutDestroy();
	} else if (behavior == Behavior::CloseToTaskbar) {
		setWindowState(window()->windowState() | Qt::WindowMinimized);
	} else {
		return false;
	}
	controller().updateIsActiveBlur();
	updateGlobalMenu();
	if (const auto controller = sessionController()) {
		controller->clearSectionStack();
	}
	return true;
}

void MainWindow::clearWidgets() {
	clearWidgetsHook();
	updateGlobalMenu();
}

void MainWindow::updateGlobalMenu() {
#ifdef Q_OS_MAC
	Platform::RequestUpdateGlobalMenu();
#else // Q_OS_MAC
	updateGlobalMenuHook();
#endif // Q_OS_MAC
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
		const auto rect = computeDesktopRect();
		if (!rect.isEmpty()) {
			_monitorLastGot = now;
			_monitorRect = rect;
		}
	}
	return _monitorRect;
}

void MainWindow::init() {
	initHook();

	updatePalette();

	if (Ui::Platform::NativeWindowFrameSupported()) {
		Core::App().settings().nativeWindowFrameChanges(
		) | rpl::on_next([=](bool native) {
			refreshTitleWidget();
			recountGeometryConstraints();
		}, lifetime());
	}
	refreshTitleWidget();
	if constexpr (Core::BuildIsCanary) {
		setupCanaryTitleLabel();
	}

	updateTitle();
	updateWindowIcon();
}

void MainWindow::handleStateChanged(Qt::WindowState state) {
	if (isHidden()) {
		// A withdrawn window can flip to no state on X11, keep what it had.
		if (state == Qt::WindowMaximized) {
			_ownPosition.maximized = 1;
			_maximizedBeforeHide = true;
		}
	} else if (state != Qt::WindowMinimized) {
		_ownPosition.maximized = (state == Qt::WindowMaximized) ? 1 : 0;
	}
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

void MainWindow::handleActiveChanged(bool active) {
	checkActivation();
	if (active) {
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
		_maximizedBeforeHide = positionMaximized();
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
	const auto screenReader = [&] {
		if (!_screenReaderBar) {
			return 0;
		}
		_screenReaderBar->resizeToWidth(st::windowMinWidth);
		return _screenReaderBar->height();
	}();
	return outdated + screenReader + st::windowMinHeight;
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

void MainWindow::setupCanaryTitleLabel() {
	const auto title = titleWidget();
	if (!title) {
		return;
	}
	const auto layout = Ui::Platform::TitleControlsLayout::Instance();
	const auto label = Ui::CreateTitleSubWidget(
		title,
		st::titleSubWidgetStyle,
		rpl::single(Core::CanaryTitleLabel()),
		layout->value(
		) | rpl::map([](const Ui::Platform::TitleLayout &layout) {
			return layout.onLeft() ? style::al_right : style::al_left;
		}),
		additionalContentPaddingValue());
	label->lifetime().add([layout] {});

	title->shownValue(
	) | rpl::skip(1) | rpl::on_next([=] {
		updateTitle();
	}, lifetime());
}

QString MainWindow::nativeTitleSuffix() const {
	if constexpr (Core::BuildIsCanary) {
		const auto title = titleWidget();
		if (!title || title->isHidden()) {
			return u" \u2022 "_q + Core::CanaryTitleLabel();
		}
	}
	return QString();
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
	if (const auto saved = Core::App().savedWindows()) {
		if (const auto restored = saved->restorePositionFor(id())) {
			return Core::AdjustToScale(*restored, u"Window"_q);
		}
	}
	const auto active = Core::App().activeWindow();
	return (!active || active == &controller())
		? Core::AdjustToScale(
			Core::App().settings().windowPosition(),
			u"Window"_q)
		: active->widget()->nextInitialChildPosition(id());
}

WindowPosition MainWindow::nextInitialChildPosition(SeparateId childId) {
	const auto rect = geometry().marginsRemoved(frameMargins());
	const auto position = rect.topLeft();
	const auto adjust = [&](int value) {
		return (value * 3 / 4);
	};
	const auto width = OptionNewWindowsSizeAsFirst.value()
		? Core::App().settings().windowPosition().w
		: childId.primary()
		? st::windowDefaultWidth
		: childId.hasChatsList()
		? (st::columnMinimalWidthLeft + adjust(st::windowDefaultWidth))
		: adjust(st::windowDefaultWidth);
	const auto height = OptionNewWindowsSizeAsFirst.value()
		? Core::App().settings().windowPosition().h
		: childId.primary()
		? st::windowDefaultHeight
		: adjust(st::windowDefaultHeight);
	const auto skip = ChildSkip();
	const auto delta = _lastChildIndex
		? (_lastMyChildCreatePosition - position)
		: skip;
	if (std::abs(delta.x()) >= skip.x() || std::abs(delta.y()) >= skip.y()) {
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
	return CountInitialGeometry(
		this,
		position,
		initial,
		{ st::windowMinWidth, st::windowMinHeight },
		u"Window"_q);
}

void MainWindow::firstShow() {
	updateMinimumSize();
	const auto position = initialPosition();
	_ownPosition.maximized = position.maximized ? 1 : 0;
	if (initGeometryFromSystem()) {
		show();
		const auto inner = body()->mapToGlobal(body()->rect());
		rememberOwnGeometry(inner);
		rememberOwnBodyMargins(inner);
		return;
	}
	const auto geometry = countInitialGeometry(position);
	DEBUG_LOG(("Window Pos: Setting first %1, %2, %3, %4"
		).arg(geometry.x()
		).arg(geometry.y()
		).arg(geometry.width()
		).arg(geometry.height()));
	rememberOwnGeometry(geometry);
	setGeometry(geometry);
	rememberOwnBodyMargins(geometry);
	show();
}

void MainWindow::positionUpdated(bool spontaneous) {
	_positionUpdatedTimer.callOnce(kSaveWindowPositionTimeout);
	if (Platform::IsWindows()) {
		InvokeQueued(this, [=] { recoverOwnGeometry(); });
	} else if (Platform::IsX11() && !spontaneous) {
		rememberOwnRequestedGeometry();
	}
}

void MainWindow::setPositionInited() {
	_positionInited = true;
}

void MainWindow::imeCompositionStartReceived() {
	_imeCompositionStartReceived.fire({});
}

rpl::producer<> MainWindow::imeCompositionStarts() const {
	return _imeCompositionStartReceived.events();
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
	if (_screenReaderBar) {
		Ui::SendPendingMoveResizeEvents(_screenReaderBar.data());
		_screenReaderBar->resizeToWidth(inner.width());
		_screenReaderBar->moveToLeft(inner.x(), bodyTop);
		bodyTop += _screenReaderBar->height();
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

	const auto suffix = nativeTitleSuffix();
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
		? st::wrap_rtl(session->authedName())
		: QString();
	const auto separateSharedMediaTitle = session
		? TitleFromSeparateSharedMedia(settings, session->windowId())
		: QString();
	if (!separateSharedMediaTitle.isEmpty()) {
		setTitle(separateSharedMediaTitle + suffix);
		return;
	}
	const auto key = (session && !settings.hideChatName)
		? session->activeChatCurrent()
		: Dialogs::Key();
	const auto thread = key ? key.thread() : nullptr;
	if (!thread) {
		setTitle((user.isEmpty() ? u"Telegram"_q : user) + added + suffix);
		return;
	}
	const auto history = thread->owningHistory();
	const auto topic = thread->asTopic();
	const auto name = topic
		? topic->title()
		: history->peer->isSelf()
		? tr::lng_saved_messages(tr::now)
		: history->peer->name();
	const auto wrapped = st::wrap_rtl(name);
	const auto threadCounter = thread->chatListBadgesState().unreadCounter;
	const auto primary = (threadCounter > 0)
		? u"(%1) %2"_q.arg(threadCounter).arg(wrapped)
		: wrapped;
	const auto middle = !user.isEmpty()
		? (u" @ "_q + user)
		: !added.isEmpty()
		? u" \u2013"_q
		: QString();
	setTitle(primary + middle + added + suffix);
}

QRect MainWindow::computeDesktopRect() const {
	return ScreenAvailableGeometry(this);
}

void MainWindow::savePosition(Qt::WindowState state) {
	if (!isVisible() || !positionInited()) {
		return;
	}

	const auto settled = (state == Qt::WindowActive);
	if (settled) {
		state = windowHandle()->windowState();
	}
	if (state == Qt::WindowMinimized) {
		return;
	}
	if (const auto saved = Core::App().savedWindows()) {
		saved->scheduleSave();
	}

	if (state == Qt::WindowMaximized) {
		_ownPosition.maximized = 1;
		DEBUG_LOG(("Window Pos: Saving maximized position."));
	} else {
		const auto r = body()->mapToGlobal(body()->rect());
		if (settled) {
			rememberOwnBodyMargins(r);
			rememberOwnRequestedGeometry();
		}
		_ownPosition.maximized = 0;
		rememberOwnGeometry(QRect(
			r.x(),
			r.y(),
			r.width() - (_rightColumn ? _rightColumn->width() : 0),
			r.height()));

		DEBUG_LOG(("Window Pos: Saving non-maximized position: %1, %2, %3, %4"
			).arg(_ownPosition.x
			).arg(_ownPosition.y
			).arg(_ownPosition.w
			).arg(_ownPosition.h));
		_ownPosition = withScreenInPosition(_ownPosition);
	}
	savePositionToSettings();
}

void MainWindow::savePositionToSettings() {
	if (isMaximized() || isHidden() || isMinimized()) {
		recoverOwnGeometry();
	}
	if (!Core::App().isWindowInSettings(&controller())
		|| _ownPosition.w < st::windowMinWidth
		|| _ownPosition.h < st::windowMinHeight
		|| _ownPosition == Core::App().settings().windowPosition()) {
		return;
	}
	DEBUG_LOG(("Window Pos: Writing: %1, %2, %3, %4 (scale %5%, maximized %6)")
		.arg(_ownPosition.x)
		.arg(_ownPosition.y)
		.arg(_ownPosition.w)
		.arg(_ownPosition.h)
		.arg(_ownPosition.scale)
		.arg(Logs::b(_ownPosition.maximized)));
	Core::App().settings().setWindowPosition(_ownPosition);
	Core::App().saveSettingsDelayed();
}

void MainWindow::rememberOwnGeometry(QRect geometry) {
	_ownPosition.moncrc = 0;
	_ownPosition.scale = cScale();
	_ownPosition.x = geometry.x();
	_ownPosition.y = geometry.y();
	_ownPosition.w = geometry.width();
	_ownPosition.h = geometry.height();
}

void MainWindow::rememberOwnBodyMargins(QRect inner) {
	if (windowState() & ~Qt::WindowActive) {
		return;
	}
	const auto outer = geometry();
	_ownBodyMargins = QMargins(
		inner.x() - outer.x(),
		inner.y() - outer.y(),
		(outer.x() + outer.width()) - (inner.x() + inner.width()),
		(outer.y() + outer.height()) - (inner.y() + inner.height()));
}

void MainWindow::rememberOwnRequestedGeometry() {
	if (isHidden() || (windowState() & ~Qt::WindowActive)) {
		return;
	}
	_ownRequestedGeometry = geometry();
}

void MainWindow::recoverOwnGeometry() {
	const auto requested = base::take(_ownRequestedGeometry);
	const auto full = isMaximized() || isFullScreen();
	auto normal = normalGeometry();
	auto unreliable = positionMaximized();
	if (full && Platform::IsX11()) {
		// The window manager can move or resize it before the state arrives.
		const auto current = geometry();
		const auto moved = (normal.topLeft() == current.topLeft())
			|| (normal.size() == current.size());
		if (moved) {
			normal = requested;
		}
		unreliable = false;
	} else if (full) {
		// Windows shifts it by the maximized frame.
		unreliable = ((normal == geometry()) != Platform::IsWindows());
	}
	if (!normal.isValid() || unreliable) {
		return;
	}
	const auto inner = normal.marginsRemoved(_ownBodyMargins);
	rememberOwnGeometry(QRect(
		inner.x(),
		inner.y(),
		inner.width() - (_rightColumn ? _rightColumn->width() : 0),
		inner.height()));
	_ownPosition = withScreenInPosition(_ownPosition);
}

WindowPosition MainWindow::withScreenInPosition(
		WindowPosition position) const {
	return PositionWithScreen(
		position,
		this,
		{ st::windowMinWidth, st::windowMinHeight },
		u"Window"_q);
}

WindowPosition MainWindow::countPositionForSave() {
	auto result = WindowPosition{ .scale = cScale() };
	const auto handle = windowHandle();
	const auto state = handle ? handle->windowState() : Qt::WindowNoState;
	const auto windowScreen = screen();
	const auto screenGeometry = windowScreen
		? windowScreen->geometry()
		: QRect();
	if (windowScreen) {
		result.moncrc = Platform::ScreenNameChecksum(windowScreen->name());
	}
	const auto rememberedMaximized = positionMaximized()
		&& (isHidden() || state == Qt::WindowMinimized);
	if (state == Qt::WindowMaximized
		|| state == Qt::WindowFullScreen
		|| rememberedMaximized) {
		result.maximized = 1;
		const auto normal = normalGeometry();
		result.x = normal.x() - screenGeometry.x();
		result.y = normal.y() - screenGeometry.y();
		result.w = normal.width();
		result.h = normal.height();
		return result;
	}
	const auto rect = body()->mapToGlobal(body()->rect());
	result.x = rect.x();
	result.y = rect.y();
	result.w = rect.width() - (_rightColumn ? _rightColumn->width() : 0);
	result.h = rect.height();
	result = withScreenInPosition(result);
	result.x -= screenGeometry.x();
	result.y -= screenGeometry.y();
	return result;
}

void MainWindow::applySavedPosition(const Core::WindowPosition &position) {
	const auto geometry = countInitialGeometry(
		Core::AdjustToScale(position, u"Window"_q));
	const auto handle = windowHandle();
	const auto state = handle ? handle->windowState() : Qt::WindowNoState;
	if (position.maximized) {
		setGeometry(geometry);
		setWindowState(Qt::WindowMaximized);
	} else {
		if (state == Qt::WindowMaximized || state == Qt::WindowFullScreen) {
			setWindowState(Qt::WindowNoState);
		}
		setGeometry(geometry);
	}
	_ownPosition.maximized = position.maximized ? 1 : 0;
	rememberOwnGeometry(geometry);
	_maximizedBeforeHide = positionMaximized();
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

void MainWindow::showRightColumn(object_ptr<Ui::RpWidget> widget) {
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
	const auto desktop = ScreenAvailableGeometry(this);
	return std::max(desktop.width() - body()->width(), 0);
}

bool MainWindow::canExtendNoMove(int extendBy) const {
	const auto desktop = ScreenAvailableGeometry(this);
	if (desktop.isEmpty()) {
		return false;
	}
	auto inner = body()->mapToGlobal(body()->rect());
	auto innerRight = (inner.x() + inner.width() + extendBy);
	auto desktopRight = (desktop.x() + desktop.width());
	return innerRight <= desktopRight;
}

int MainWindow::tryToExtendWidthBy(int addToWidth) {
	const auto desktop = ScreenAvailableGeometry(this);
	if (desktop.isEmpty()) {
		updateControlsGeometry();
		return 0;
	}
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
		Fn<void()> &&callback,
		QPixmap pixmap) {
	// Qt destroys this QDrag automatically after the drag is finished
	// We must not delete this at the end of this function, as this breaks DnD on Linux
	auto drag = new QDrag(this);
	KUrlMimeData::exportUrlsToPortal(data.get());
	drag->setMimeData(data.release());
	if (!pixmap.isNull()) {
		drag->setPixmap(std::move(pixmap));
	}
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
		QSize minimal,
		const QString &name) {
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
	DEBUG_LOG(("%1 Pos: Screen found, geometry: %2, %3, %4, %5"
		).arg(name
		).arg(geometry.x()
		).arg(geometry.y()
		).arg(geometry.width()
		).arg(geometry.height()));
	return position;
}

WindowPosition PositionWithScreen(
		WindowPosition position,
		not_null<const QWidget*> widget,
		QSize minimal,
		const QString &name) {
	const auto screen = widget->screen();
	return PositionWithScreen(
		position,
		screen ? screen : QGuiApplication::primaryScreen(),
		minimal,
		name);
}

QRect CountInitialGeometry(
		not_null<const Ui::RpWindow*> widget,
		WindowPosition position,
		WindowPosition initial,
		QSize minSize,
		const QString &name) {
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
		return QGuiApplication::screenAt(position.rect().center());
	}();
	if (!screen) {
		return initial.rect();
	}
	const auto frame = widget->frameMargins();
	const auto screenGeometry = screen->geometry();
	const auto availableGeometry = screen->availableGeometry();
	const auto spaceForInner = availableGeometry.marginsRemoved(frame);
	DEBUG_LOG(("%1 Pos: "
		"Screen found, screen geometry: %2, %3, %4, %5, "
		"available: %6, %7, %8, %9"
		).arg(name
		).arg(screenGeometry.x()
		).arg(screenGeometry.y()
		).arg(screenGeometry.width()
		).arg(screenGeometry.height()
		).arg(availableGeometry.x()
		).arg(availableGeometry.y()
		).arg(availableGeometry.width()
		).arg(availableGeometry.height()));
	DEBUG_LOG(("%1 Pos: "
		"Window frame margins: %2, %3, %4, %5, "
		"available space for inner geometry: %6, %7, %8, %9"
		).arg(name
		).arg(frame.left()
		).arg(frame.top()
		).arg(frame.right()
		).arg(frame.bottom()
		).arg(spaceForInner.x()
		).arg(spaceForInner.y()
		).arg(spaceForInner.width()
		).arg(spaceForInner.height()));

	const auto x = spaceForInner.x()
		- (position.moncrc ? screenGeometry.x() : 0);
	const auto y = spaceForInner.y()
		- (position.moncrc ? screenGeometry.y() : 0);
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
	if (position.moncrc) {
		position.x += screenGeometry.x();
		position.y += screenGeometry.y();
	}
	if ((position.x + st::windowMinWidth
		> screenGeometry.x() + screenGeometry.width())
		|| (position.y + st::windowMinHeight
			> screenGeometry.y() + screenGeometry.height())) {
		return initial.rect();
	}
	DEBUG_LOG(("%1 Pos: Resulting geometry is %2, %3, %4, %5"
		).arg(name
		).arg(position.x
		).arg(position.y
		).arg(position.w
		).arg(position.h));
	return position.rect();
}

} // namespace Window
