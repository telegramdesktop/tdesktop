/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/mac/tray_mac.h"

#include "base/platform/mac/base_utilities_mac.h"
#include "core/application.h"
#include "core/sandbox.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "ui/painter.h"
#include "styles/style_window.h"

#include <QtWidgets/QMenu>

#import <AppKit/NSMenu.h>
#import <AppKit/NSStatusItem.h>

@interface CommonDelegate : NSObject<NSMenuDelegate> {
}

- (void) menuDidClose:(NSMenu *)menu;
- (void) menuWillOpen:(NSMenu *)menu;
- (void) observeValueForKeyPath:(NSString *)keyPath
	ofObject:(id)object
	change:(NSDictionary<NSKeyValueChangeKey, id> *)change
	context:(void *)context;

- (rpl::producer<>) closes;
- (rpl::producer<>) aboutToShowRequests;
- (rpl::producer<>) appearanceChanges;

@end // @interface CommonDelegate

@implementation CommonDelegate {
	rpl::event_stream<> _closes;
	rpl::event_stream<> _aboutToShowRequests;
	rpl::event_stream<> _appearanceChanges;
}

- (void) menuDidClose:(NSMenu *)menu {
	Core::Sandbox::Instance().customEnterFromEventLoop([&] {
		_closes.fire({});
	});
}

- (void) menuWillOpen:(NSMenu *)menu {
	Core::Sandbox::Instance().customEnterFromEventLoop([&] {
		_aboutToShowRequests.fire({});
	});
}

// Thanks https://stackoverflow.com/a/64525038
- (void) observeValueForKeyPath:(NSString *)keyPath
		ofObject:(id)object
		change:(NSDictionary<NSKeyValueChangeKey, id> *)change
		context:(void *)context {
	if ([keyPath isEqualToString:@"button.effectiveAppearance"]) {
		_appearanceChanges.fire({});
	}
}

- (rpl::producer<>) closes {
	return _closes.events();
}

- (rpl::producer<>) aboutToShowRequests {
	return _aboutToShowRequests.events();
}

- (rpl::producer<>) appearanceChanges {
	return _appearanceChanges.events();
}

@end // @implementation MenuDelegate

namespace Platform {

namespace {

[[nodiscard]] bool IsAnyActiveForTrayMenu() {
	for (const NSWindow *w in [[NSApplication sharedApplication] windows]) {
		if (w.isKeyWindow) {
			return true;
		}
	}
	return false;
}

[[nodiscard]] QImage TrayIconBack(bool darkMode) {
	static const auto WithColor = [](QColor color) {
		return st::macTrayIcon.instance(color, 100);
	};
	static const auto DarkModeResult = WithColor({ 255, 255, 255 });
	static const auto LightModeResult = WithColor({ 0, 0, 0, 180 });
	auto result = darkMode ? DarkModeResult : LightModeResult;
	result.detach();
	return result;
}

void PlaceCounter(
		QImage &img,
		int size,
		int count,
		style::color bg,
		style::color color) {
	if (!count) {
		return;
	}
	const auto savedRatio = img.devicePixelRatio();
	img.setDevicePixelRatio(1.);

	{
		Painter p(&img);
		PainterHighQualityEnabler hq(p);

		const auto cnt = (count < 100)
			? QString("%1").arg(count)
			: QString("..%1").arg(count % 100, 2, 10, QChar('0'));
		const auto cntSize = cnt.size();

		p.setBrush(bg);
		p.setPen(Qt::NoPen);
		int32 fontSize, skip;
		if (size == 22) {
			skip = 1;
			fontSize = 8;
		} else {
			skip = 2;
			fontSize = 16;
		}
		style::font f(fontSize, 0, 0);
		int32 w = f->width(cnt), d, r;
		if (size == 22) {
			d = (cntSize < 2) ? 3 : 2;
			r = (cntSize < 2) ? 6 : 5;
		} else {
			d = (cntSize < 2) ? 6 : 5;
			r = (cntSize < 2) ? 9 : 11;
		}
		p.drawRoundedRect(
			QRect(
				size - w - d * 2 - skip,
				size - f->height - skip,
				w + d * 2,
				f->height),
			r,
			r);

		p.setCompositionMode(QPainter::CompositionMode_Source);
		p.setFont(f);
		p.setPen(color);
		p.drawText(
			size - w - d - skip,
			size - f->height + f->ascent - skip,
			cnt);
	}
	img.setDevicePixelRatio(savedRatio);
}

void UpdateIcon(const NSStatusItem *status) {
	if (!status) {
		return;
	}

	const auto appearance = status.button.effectiveAppearance;
	const auto darkMode = [[appearance.name lowercaseString]
		containsString:@"dark"];

	// The recommended maximum title bar icon height is 18 points
	// (device independent pixels). The menu height on past and
	// current OS X versions is 22 points. Provide some future-proofing
	// by deriving the icon height from the menu height.
	const int padding = 0;
	const int menuHeight = NSStatusBar.systemStatusBar.thickness;
	// [[status.button window] backingScaleFactor];
	const int maxImageHeight = (menuHeight - padding)
		* style::DevicePixelRatio();

	// Select pixmap based on the device pixel height. Ideally we would use
	// the devicePixelRatio of the target screen, but that value is not
	// known until draw time. Use qApp->devicePixelRatio, which returns the
	// devicePixelRatio for the "best" screen on the system.

	const auto side = 22 * style::DevicePixelRatio();
	const auto selectedSize = QSize(side, side);

	auto result = TrayIconBack(darkMode);
	auto resultActive = result;
	resultActive.detach();

	const auto counter = Core::App().unreadBadge();
	const auto muted = Core::App().unreadBadgeMuted();

	const auto &bg = (muted ? st::trayCounterBgMute : st::trayCounterBg);
	const auto &fg = st::trayCounterFg;
	const auto &fgInvert = st::trayCounterFgMacInvert;
	const auto &bgInvert = st::trayCounterBgMacInvert;

	const auto &resultFg = !darkMode ? fg : muted ? fgInvert : fg;
	PlaceCounter(result, side, counter, bg, resultFg);
	PlaceCounter(resultActive, side, counter, bgInvert, fgInvert);

	// Scale large pixmaps to fit the available menu bar area.
	if (result.height() > maxImageHeight) {
		result = result.scaledToHeight(
			maxImageHeight,
			Qt::SmoothTransformation);
	}
	if (resultActive.height() > maxImageHeight) {
		resultActive = resultActive.scaledToHeight(
			maxImageHeight,
			Qt::SmoothTransformation);
	}

	status.button.image = Q2NSImage(result);
	status.button.alternateImage = Q2NSImage(resultActive);
	status.button.imageScaling = NSImageScaleProportionallyDown;
}

} // namespace

class NativeIcon final {
public:
	NativeIcon();
	~NativeIcon();

	void updateIcon();
	void showMenu(not_null<QMenu*> menu);
	void deactivateButton();

	[[nodiscard]] rpl::producer<> clicks() const;
	[[nodiscard]] rpl::producer<> aboutToShowRequests() const;

private:
	CommonDelegate *_delegate;
	NSStatusItem *_status;

	rpl::event_stream<> _clicks;

	rpl::lifetime _lifetime;

};

NativeIcon::NativeIcon()
: _delegate([[CommonDelegate alloc] init])
, _status([
	[NSStatusBar.systemStatusBar
		statusItemWithLength:NSSquareStatusItemLength] retain]) {

	[_status
		addObserver:_delegate
		forKeyPath:@"button.effectiveAppearance"
		options:0
			| NSKeyValueObservingOptionNew
			| NSKeyValueObservingOptionInitial
		context:nil];

	[_delegate closes] | rpl::start_with_next([=] {
		_status.menu = nil;
	}, _lifetime);

	[_delegate appearanceChanges] | rpl::start_with_next([=] {
		updateIcon();
	}, _lifetime);

	const auto masks = NSEventMaskLeftMouseDown
		| NSEventMaskLeftMouseUp
		| NSEventMaskRightMouseDown
		| NSEventMaskRightMouseUp
		| NSEventMaskOtherMouseUp;
	[_status.button sendActionOn:masks];

	id buttonCallback = [^{
		const auto type = NSApp.currentEvent.type;

		if ((type == NSEventTypeLeftMouseDown)
			|| (type == NSEventTypeRightMouseDown)) {
			Core::Sandbox::Instance().customEnterFromEventLoop([=] {
				_clicks.fire({});
			});
		}
	} copy];

	_lifetime.add([=] {
		[buttonCallback release];
	});

	_status.button.target = buttonCallback;
	_status.button.action = @selector(invoke);
	_status.button.toolTip = Q2NSString(AppName.utf16());
}

NativeIcon::~NativeIcon() {
	[_status
		removeObserver:_delegate
		forKeyPath:@"button.effectiveAppearance"];
	[NSStatusBar.systemStatusBar removeStatusItem:_status];

	[_status release];
	[_delegate release];
}

void NativeIcon::updateIcon() {
	UpdateIcon(_status);
}

void NativeIcon::showMenu(not_null<QMenu*> menu) {
	_status.menu = menu->toNSMenu();
	_status.menu.delegate = _delegate;
	[_status.button performClick:nil];
}

void NativeIcon::deactivateButton() {
	[_status.button highlight:false];
}

rpl::producer<> NativeIcon::clicks() const {
	return _clicks.events();
}

rpl::producer<> NativeIcon::aboutToShowRequests() const {
	return [_delegate aboutToShowRequests];
}

Tray::Tray() {
}

void Tray::createIcon() {
	if (!_nativeIcon) {
		_nativeIcon = std::make_unique<NativeIcon>();
		// On macOS we are activating the window on click
		// instead of showing the menu, when the window is not activated.
		_nativeIcon->clicks(
		) | rpl::start_with_next([=] {
			if (IsAnyActiveForTrayMenu()) {
				_nativeIcon->showMenu(_menu.get());
			} else {
				_nativeIcon->deactivateButton();
				_showFromTrayRequests.fire({});
			}
		}, _lifetime);
	}
	updateIcon();
}

void Tray::destroyIcon() {
	_nativeIcon = nullptr;
}

void Tray::updateIcon() {
	if (_nativeIcon) {
		_nativeIcon->updateIcon();
	}
}

void Tray::createMenu() {
	if (!_menu) {
		_menu = base::make_unique_q<QMenu>(nullptr);
	}
}

void Tray::destroyMenu() {
	if (_menu) {
		_menu->clear();
	}
	_actionsLifetime.destroy();
}

void Tray::addAction(rpl::producer<QString> text, Fn<void()> &&callback) {
	if (!_menu) {
		return;
	}

	const auto action = _menu->addAction(QString(), std::move(callback));
	std::move(
		text
	) | rpl::start_with_next([=](const QString &text) {
		action->setText(text);
	}, _actionsLifetime);
}

void Tray::showTrayMessage() const {
}

bool Tray::hasTrayMessageSupport() const {
	return false;
}

rpl::producer<> Tray::aboutToShowRequests() const {
	return _nativeIcon
		? _nativeIcon->aboutToShowRequests()
		: rpl::never<>();
}

rpl::producer<> Tray::showFromTrayRequests() const {
	return _showFromTrayRequests.events();
}

rpl::producer<> Tray::hideToTrayRequests() const {
	return rpl::never<>();
}

rpl::producer<> Tray::iconClicks() const {
	return rpl::never<>();
}

bool Tray::hasIcon() const {
	return _nativeIcon != nullptr;
}

rpl::lifetime &Tray::lifetime() {
	return _lifetime;
}

Tray::~Tray() = default;

} // namespace Platform
