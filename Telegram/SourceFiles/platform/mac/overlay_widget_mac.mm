/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/mac/overlay_widget_mac.h"

#include "base/object_ptr.h"
#include "ui/platform/ui_platform_window_title.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/rp_window.h"
#include "styles/style_media_view.h"

#include <QtGui/QWindow>
#include <Cocoa/Cocoa.h>

namespace Platform {
namespace {

using namespace Media::View;

} // namespace

struct MacOverlayWidgetHelper::Data {
	const not_null<Ui::RpWindow*> window;
	const Fn<void(bool)> maximize;
	object_ptr<Ui::AbstractButton> buttonClose = { nullptr };
	object_ptr<Ui::AbstractButton> buttonMinimize = { nullptr };
	object_ptr<Ui::AbstractButton> buttonMaximize = { nullptr };
	rpl::event_stream<> activations;
	rpl::variable<float64> masterOpacity = 1.;
	rpl::variable<bool> maximized = false;
	rpl::event_stream<> clearStateRequests;
	bool anyOver = false;
	NSWindow * __weak native = nil;
	rpl::variable<int> topNotchSkip;
};

MacOverlayWidgetHelper::MacOverlayWidgetHelper(
	not_null<Ui::RpWindow*> window,
	Fn<void(bool)> maximize)
: _data(std::make_unique<Data>(Data{
	.window = window,
	.maximize = std::move(maximize),
})) {
	_data->buttonClose = create(window, Control::Close);
	_data->buttonMinimize = create(window, Control::Minimize);
	_data->buttonMaximize = create(window, Control::Maximize);
}

MacOverlayWidgetHelper::~MacOverlayWidgetHelper() = default;

void MacOverlayWidgetHelper::activate(Control control) {
	const auto fullscreen = (_data->window->windowHandle()->flags() & Qt::FramelessWindowHint);
	switch (control) {
	case Control::Close: _data->window->close(); return;
	case Control::Minimize: [_data->native miniaturize:_data->native]; return;
	case Control::Maximize: _data->maximize(!fullscreen); return;
	}
}

void MacOverlayWidgetHelper::beforeShow(bool fullscreen) {
	_data->window->setAttribute(Qt::WA_MacAlwaysShowToolWindow, !fullscreen);
	_data->window->windowHandle()->setFlag(Qt::FramelessWindowHint, fullscreen);
	updateStyles(fullscreen);
	clearState();
}

void MacOverlayWidgetHelper::afterShow(bool fullscreen) {
	updateStyles(fullscreen);
	refreshButtons(fullscreen);
	_data->window->activateWindow();
}

void MacOverlayWidgetHelper::resolveNative() {
	if (const auto handle = _data->window->winId()) {
		_data->native = [reinterpret_cast<NSView*>(handle) window];
	}
}

void MacOverlayWidgetHelper::updateStyles(bool fullscreen) {
	_data->maximized = fullscreen;

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
	if (@available(macOS 12.0, *)) {
		_data->topNotchSkip = [[window screen] safeAreaInsets].top;
	}
}

void MacOverlayWidgetHelper::refreshButtons(bool fullscreen) {
	Expects(_data->native != nullptr);

	const auto window = _data->native;
	const auto process = [&](NSWindowButton type) {
		if (const auto button = [window standardWindowButton:type]) {
			[button setHidden:YES];
		}
	};
	process(NSWindowCloseButton);
	process(NSWindowMiniaturizeButton);
	process(NSWindowZoomButton);
	_data->buttonClose->moveToLeft(0, 0);
	_data->buttonClose->raise();
	_data->buttonClose->show();
	_data->buttonMinimize->moveToLeft(_data->buttonClose->width(), 0);
	_data->buttonMinimize->raise();
	_data->buttonMinimize->show();
	_data->buttonMaximize->moveToLeft(_data->buttonClose->width() + _data->buttonMinimize->width(), 0);
	_data->buttonMaximize->raise();
	_data->buttonMaximize->show();
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
		[_data->native miniaturize:_data->native];
	}
}

void MacOverlayWidgetHelper::clearState() {
	_data->clearStateRequests.fire({});
}

void MacOverlayWidgetHelper::setControlsOpacity(float64 opacity) {
	_data->masterOpacity = opacity;
}

rpl::producer<bool> MacOverlayWidgetHelper::controlsSideRightValue() {
	return rpl::single(false);
}

rpl::producer<int> MacOverlayWidgetHelper::topNotchSkipValue() {
	return _data->topNotchSkip.value();
}

object_ptr<Ui::AbstractButton> MacOverlayWidgetHelper::create(
		not_null<QWidget*> parent,
		Control control) {
	auto result = object_ptr<Ui::AbstractButton>(parent);
	const auto raw = result.data();

	raw->setClickedCallback([=] { activate(control); });

	struct State {
		Ui::Animations::Simple animation;
		float64 progress = -1.;
		QImage frame;
		bool maximized = false;
		bool anyOver = false;
		bool over = false;
	};
	const auto state = raw->lifetime().make_state<State>();

	rpl::merge(
		_data->masterOpacity.changes() | rpl::to_empty,
		_data->maximized.changes() | rpl::to_empty
	) | rpl::start_with_next([=] {
		raw->update();
	}, raw->lifetime());

	_data->clearStateRequests.events(
	) | rpl::start_with_next([=] {
		raw->clearState();
		raw->update();
		state->over = raw->isOver();
		_data->anyOver = false;
		state->animation.stop();
	}, raw->lifetime());

	struct Info {
		const style::icon *icon = nullptr;
		style::margins padding;
	};
	const auto info = [&]() -> Info {
		switch (control) {
		case Control::Minimize:
			return { &st::mediaviewTitleMinimizeMac, st::mediaviewTitleMinimizeMacPadding };
		case Control::Maximize:
			return { &st::mediaviewTitleMaximizeMac, st::mediaviewTitleMaximizeMacPadding };
		case Control::Close:
			return { &st::mediaviewTitleCloseMac, st::mediaviewTitleCloseMacPadding };
		}
		Unexpected("Value in DefaultOverlayWidgetHelper::Buttons::create.");
	}();
	const auto icon = info.icon;

	raw->resize(QRect(QPoint(), icon->size()).marginsAdded(info.padding).size());
	state->frame = QImage(
		icon->size() * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	state->frame.setDevicePixelRatio(style::DevicePixelRatio());

	const auto updateOver = [=] {
		const auto over = raw->isOver();
		if (state->over == over) {
			return;
		}
		state->over = over;
		const auto anyOver = over
			|| _data->buttonClose->isOver()
			|| _data->buttonMinimize->isOver()
			|| _data->buttonMaximize->isOver();
		if (_data->anyOver != anyOver) {
			_data->anyOver = anyOver;
			_data->buttonClose->update();
			_data->buttonMinimize->update();
			_data->buttonMaximize->update();
		}
		state->animation.start(
			[=] { raw->update(); },
			state->over ? 0. : 1.,
			state->over ? 1. : 0.,
			st::mediaviewFadeDuration);
	};

	const auto prepareFrame = [=] {
		const auto progress = state->animation.value(state->over ? 1. : 0.);
		const auto maximized = _data->maximized.current();
		const auto anyOver = _data->anyOver;
		if (state->progress == progress
			&& state->maximized == maximized
			&& state->anyOver == anyOver) {
			return;
		}
		state->progress = progress;
		state->maximized = maximized;
		state->anyOver = anyOver;
		auto current = icon;
		if (control == Control::Maximize) {
			current = maximized ? &st::mediaviewTitleRestoreMac : icon;
		}
		state->frame.fill(Qt::transparent);

		auto q = QPainter(&state->frame);
		const auto normal = maximized
			? kMaximizedIconOpacity
			: kNormalIconOpacity;
		q.setOpacity(progress + (1 - progress) * normal);
		st::mediaviewTitleButtonMac.paint(q, 0, 0, raw->width());
		if (anyOver) {
			q.setOpacity(1.);
			current->paint(q, 0, 0, raw->width());
		}
		q.end();
	};

	raw->paintRequest(
	) | rpl::start_with_next([=, padding = info.padding] {
		updateOver();
		prepareFrame();

		auto p = QPainter(raw);
		p.setOpacity(_data->masterOpacity.current());
		p.drawImage(padding.left(), padding.top(), state->frame);
	}, raw->lifetime());

	return result;
}

std::unique_ptr<OverlayWidgetHelper> CreateOverlayWidgetHelper(
		not_null<Ui::RpWindow*> window,
		Fn<void(bool)> maximize) {
	return std::make_unique<MacOverlayWidgetHelper>(
		window,
		std::move(maximize));
}

} // namespace Platform
