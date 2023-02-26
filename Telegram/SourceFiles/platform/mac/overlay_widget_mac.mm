/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/mac/overlay_widget_mac.h"

#include "ui/widgets/rp_window.h"

#include <QtGui/QWindow>
#include <Cocoa/Cocoa.h>

@interface ButtonHandler : NSObject {
}

- (instancetype) initWithCallback:(Fn<void(NSWindowButton)>)callback;
- (void) close:(id) sender;
- (void) miniaturize:(id) sender;
- (void) zoom:(id) sender;

@end // @interface ButtonHandler

@implementation ButtonHandler {
	Fn<void(NSWindowButton)> _callback;
}

- (instancetype) initWithCallback:(Fn<void(NSWindowButton)>)callback {
	_callback = std::move(callback);
	return [super init];
}

- (void) close:(id) sender {
	_callback(NSWindowCloseButton);
}

- (void) miniaturize:(id) sender {
	_callback(NSWindowMiniaturizeButton);
}

- (void) zoom:(id) sender {
	_callback(NSWindowZoomButton);
}

@end // @implementation ButtonHandler

namespace Platform {
namespace {

[[nodiscard]] base::flat_map<int, NSRect> ButtonGeometries() {
	auto result = base::flat_map<int, NSRect>();
	auto normal = QWidget();
	normal.hide();
	normal.createWinId();
	const auto view = reinterpret_cast<NSView*>(normal.winId());
	const auto window = [view window];
	const auto process = [&](NSWindowButton type) {
		if (const auto button = [window standardWindowButton:type]) {
			result.emplace(int(type), [button frame]);
		}
	};
	process(NSWindowCloseButton);
	process(NSWindowMiniaturizeButton);
	process(NSWindowZoomButton);

	const auto full = [window frame];
	const auto inner = [window contentRectForFrameRect:full].size.height;
	const auto height = std::max(full.size.height - inner, 0.);

	result[int(NSWindowToolbarButton)] = { CGPoint(), CGSize{ full.size.width, height }};
	return result;
}

} // namespace

struct MacOverlayWidgetHelper::Data {
	const not_null<Ui::RpWindow*> window;
	const Fn<void(bool)> maximize;
	const base::flat_map<int, NSRect> buttons;
	ButtonHandler *handler = nil;
	NSWindow * __weak native = nil;
	NSButton * __weak closeNative = nil;
	NSButton * __weak miniaturizeNative = nil;
	NSButton * __weak zoomNative = nil;
	NSButton * __weak close = nil;
	NSButton * __weak miniaturize = nil;
	NSButton * __weak zoom = nil;
};

MacOverlayWidgetHelper::MacOverlayWidgetHelper(
	not_null<Ui::RpWindow*> window,
	Fn<void(bool)> maximize)
: _data(std::make_unique<Data>(Data{
	.window = window,
	.maximize = std::move(maximize),
	.buttons = ButtonGeometries(),
	.handler = [[ButtonHandler alloc] initWithCallback:[=](NSWindowButton button) {
		activate(int(button));
	}]
})) {
}

MacOverlayWidgetHelper::~MacOverlayWidgetHelper() {
	[_data->handler release];
	_data->handler = nil;
}

void MacOverlayWidgetHelper::activate(int button) {
	const auto fullscreen = (_data->window->windowHandle()->flags() & Qt::FramelessWindowHint);
	switch (NSWindowButton(button)) {
	case NSWindowCloseButton: _data->window->close(); return;
	case NSWindowMiniaturizeButton: [_data->native miniaturize:_data->handler]; return;
	case NSWindowZoomButton: _data->maximize(!fullscreen); return;
	}
}

void MacOverlayWidgetHelper::beforeShow(bool fullscreen) {
	_data->window->setAttribute(Qt::WA_MacAlwaysShowToolWindow, !fullscreen);
	_data->window->windowHandle()->setFlag(Qt::FramelessWindowHint, fullscreen);
	if (!fullscreen) {
		_data->window->setGeometry({ 100, 100, 800, 600 });
	}
	updateStyles(fullscreen);
}

void MacOverlayWidgetHelper::afterShow(bool fullscreen) {
	updateStyles(fullscreen);
	refreshButtons(fullscreen);
}

void MacOverlayWidgetHelper::resolveNative() {
	if (const auto handle = _data->window->winId()) {
		_data->native = [reinterpret_cast<NSView*>(handle) window];
	}
}

void MacOverlayWidgetHelper::updateStyles(bool fullscreen) {
	resolveNative();
	if (!_data->native) {
		return;
	}

	const auto window = _data->native;
	const auto level = !fullscreen
		? NSNormalWindowLevel
		: NSPopUpMenuWindowLevel;
	[window setLevel:level];
	[window setHidesOnDeactivate:!_data->window->testAttribute(Qt::WA_MacAlwaysShowToolWindow)];
	[window setTitleVisibility:NSWindowTitleHidden];
	[window setTitlebarAppearsTransparent:YES];
	[window setStyleMask:[window styleMask] | NSWindowStyleMaskFullSizeContentView];
}

void MacOverlayWidgetHelper::refreshButtons(bool fullscreen) {
	Expects(_data->native != nullptr);

	const auto window = _data->native;
	auto next = CGPoint();
	const auto added = [&](NSRect frame) {
		const auto left = frame.origin.x + frame.size.width * 1.5;
		const auto top = frame.origin.y;
		if (next.x < left) {
			next.x = left;
		}
		next.y = top;
	};
	for (const auto &[type, frame] : _data->buttons) {
		added(frame);
	}
	const auto skip = fullscreen
		? _data->buttons.find(int(NSWindowToolbarButton))->second.size.height
		: 0.;
	const auto process = [&](auto native, auto custom, NSWindowButton type, auto action) {
		auto retained = (NSButton*)nil;
		while (const auto button = [window standardWindowButton:type]) {
			if ([button superview] != [window contentView]) {
				*native = button;
				[button setHidden:YES];
				break;
			} else if (button == *custom) {
				retained = [button retain];
			}
			[button removeFromSuperview];
		}
		const auto i = _data->buttons.find(int(type));
		const auto frame = [&](NSButton *button) {
			if (i != end(_data->buttons)) {
				auto origin = i->second.origin;
				origin.y += skip;
				return NSRect{ origin, i->second.size };
			}
			const auto size = [button frame].size;
			auto result = NSRect{ next, size };
			added(result);
			result.origin.y += skip;
			return result;
		};
		if (!retained) {
			if (*custom) {
				retained = *custom;
				[retained retain];
				[retained removeFromSuperview];
			} else {
				const auto style = NSWindowStyleMaskTitled
					| NSWindowStyleMaskClosable
					| NSWindowStyleMaskMiniaturizable
					| NSWindowStyleMaskResizable;
				*custom
					= retained
					= [NSWindow standardWindowButton:type forStyleMask:style];
				[retained setTarget:_data->handler];
				[retained setAction:action];
				[retained retain];
			}
		}
		[[window contentView] addSubview:retained];
		[retained setFrame:frame(retained)];
		[retained setEnabled:YES];
		[retained setHidden:NO];
		[retained release];
	};
	process(
		&_data->closeNative,
		&_data->close,
		NSWindowCloseButton,
		@selector(close:));
	process(
		&_data->miniaturizeNative,
		&_data->miniaturize,
		NSWindowMiniaturizeButton,
		@selector(miniaturize:));
	process(
		&_data->zoomNative,
		&_data->zoom,
		NSWindowZoomButton,
		@selector(zoom:));
}

void MacOverlayWidgetHelper::notifyFileDialogShown(bool shown) {
	resolveNative();
	if (_data->native && !_data->window->isHidden()) {
		const auto level = [_data->native level];
		if (level != NSNormalWindowLevel) {
			const auto level = shown
				? NSModalPanelWindowLevel
				: NSPopUpMenuWindowLevel;
			[_data->native setLevel:level];
		}
	}
}

void MacOverlayWidgetHelper::minimize(not_null<Ui::RpWindow*> window) {
	resolveNative();
	if (_data->native) {
		[_data->native miniaturize:_data->handler];
	}
}

std::unique_ptr<OverlayWidgetHelper> CreateOverlayWidgetHelper(
		not_null<Ui::RpWindow*> window,
		Fn<void(bool)> maximize) {
	return std::make_unique<MacOverlayWidgetHelper>(
		window,
		std::move(maximize));
}

} // namespace Platform
