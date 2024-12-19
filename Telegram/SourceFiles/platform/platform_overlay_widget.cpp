/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/platform_overlay_widget.h"

#include "ui/effects/animations.h"
#include "ui/platform/ui_platform_window_title.h"
#include "ui/widgets/rp_window.h"
#include "ui/abstract_button.h"
#include "styles/style_media_view.h"

namespace Media::View {

QColor OverBackgroundColor() {
	auto c1 = st::mediaviewBg->c;
	auto c2 = QColor(255, 255, 255);
	const auto mix = [&](int a, int b) {
		constexpr auto k1 = 0.15 * 0.85 / (1. - 0.85 * 0.85);
		constexpr auto k2 = 0.15 / (1. - 0.85 * 0.85);
		return int(a * k1 + b * k2);
	};
	return QColor(
		mix(c1.red(), c2.red()),
		mix(c1.green(), c2.green()),
		mix(c1.blue(), c2.blue()));
}

} // namespace Media::View

namespace Platform {
namespace {

using namespace Media::View;

} // namespace

class DefaultOverlayWidgetHelper::Buttons final
	: public Ui::Platform::AbstractTitleButtons {
public:
	using Control = Ui::Platform::TitleControl;

	object_ptr<Ui::AbstractButton> create(
		not_null<QWidget*> parent,
		Control control,
		const style::WindowTitle &st) override;
	void updateState(
		bool active,
		bool maximized,
		const style::WindowTitle &st) override;
	void notifySynteticOver(Control control, bool over) override;

	void setMasterOpacity(float64 opacity);
	[[nodiscard]] rpl::producer<> activations() const;

	void clearState();

private:
	rpl::event_stream<> _activations;
	rpl::variable<float64> _masterOpacity = 1.;
	rpl::variable<bool> _maximized = false;
	rpl::event_stream<> _clearStateRequests;

};

object_ptr<Ui::AbstractButton> DefaultOverlayWidgetHelper::Buttons::create(
		not_null<QWidget*> parent,
		Control control,
		const style::WindowTitle &st) {
	auto result = object_ptr<Ui::AbstractButton>(parent);
	const auto raw = result.data();

	struct State {
		Ui::Animations::Simple animation;
		float64 progress = -1.;
		QImage frame;
		bool maximized = false;
		bool over = false;
	};
	const auto state = raw->lifetime().make_state<State>();

	rpl::merge(
		_masterOpacity.changes() | rpl::to_empty,
		_maximized.changes() | rpl::to_empty
	) | rpl::start_with_next([=] {
		raw->update();
	}, raw->lifetime());

	_clearStateRequests.events(
	) | rpl::start_with_next([=] {
		raw->clearState();
		raw->update();
		state->over = raw->isOver();
		state->animation.stop();
	}, raw->lifetime());

	const auto icon = [&] {
		switch (control) {
		case Control::Minimize: return &st::mediaviewTitleMinimize;
		case Control::Maximize: return &st::mediaviewTitleMaximize;
		case Control::Close: return &st::mediaviewTitleClose;
		}
		Unexpected("Value in DefaultOverlayWidgetHelper::Buttons::create.");
	}();

	raw->resize(icon->size());
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
		state->animation.start(
			[=] { raw->update(); },
			state->over ? 0. : 1.,
			state->over ? 1. : 0.,
			st::mediaviewFadeDuration);
	};

	const auto prepareFrame = [=] {
		const auto progress = state->animation.value(state->over ? 1. : 0.);
		const auto maximized = _maximized.current();
		if (state->progress == progress && state->maximized == maximized) {
			return;
		}
		state->progress = progress;
		state->maximized = maximized;
		auto current = icon;
		if (control == Control::Maximize) {
			current = maximized ? &st::mediaviewTitleRestore : icon;
		}
		const auto alpha = progress * kOverBackgroundOpacity;
		auto color = OverBackgroundColor();
		color.setAlpha(anim::interpolate(0, 255, alpha));
		state->frame.fill(color);

		auto q = QPainter(&state->frame);
		const auto normal = maximized
			? kMaximizedIconOpacity
			: kNormalIconOpacity;
		q.setOpacity(progress + (1 - progress) * normal);
		current->paint(q, 0, 0, raw->width());
		q.end();
	};

	raw->paintRequest(
	) | rpl::start_with_next([=] {
		updateOver();
		prepareFrame();

		auto p = QPainter(raw);
		p.setOpacity(_masterOpacity.current());
		p.drawImage(0, 0, state->frame);
	}, raw->lifetime());

	return result;
}

void DefaultOverlayWidgetHelper::Buttons::updateState(
		bool active,
		bool maximized,
		const style::WindowTitle &st) {
	_maximized = maximized;
}

void DefaultOverlayWidgetHelper::Buttons::notifySynteticOver(
		Ui::Platform::TitleControl control,
		bool over) {
	if (over) {
		_activations.fire({});
	}
}

void DefaultOverlayWidgetHelper::Buttons::clearState() {
	_clearStateRequests.fire({});
}

void DefaultOverlayWidgetHelper::Buttons::setMasterOpacity(float64 opacity) {
	_masterOpacity = opacity;
}

rpl::producer<> DefaultOverlayWidgetHelper::Buttons::activations() const {
	return _activations.events();
}

void OverlayWidgetHelper::minimize(not_null<Ui::RpWindow*> window) {
	window->setWindowState(window->windowState() | Qt::WindowMinimized);
}

DefaultOverlayWidgetHelper::DefaultOverlayWidgetHelper(
	not_null<Ui::RpWindow*> window,
	Fn<void(bool)> maximize)
: _buttons(new DefaultOverlayWidgetHelper::Buttons())
, _controls(Ui::Platform::SetupSeparateTitleControls(
	window,
	std::make_unique<Ui::Platform::SeparateTitleControls>(
		window->body(),
		st::mediaviewTitle,
		std::unique_ptr<DefaultOverlayWidgetHelper::Buttons>(_buttons.get()),
		std::move(maximize)))) {
}

DefaultOverlayWidgetHelper::~DefaultOverlayWidgetHelper() = default;

void DefaultOverlayWidgetHelper::orderWidgets() {
	_controls->wrap.raise();
}

bool DefaultOverlayWidgetHelper::skipTitleHitTest(QPoint position) {
	using namespace Ui::Platform;
	return _controls->controls.hitTest(position) != HitTestResult::None;
}

rpl::producer<> DefaultOverlayWidgetHelper::controlsActivations() {
	return _buttons->activations();
}

rpl::producer<bool> DefaultOverlayWidgetHelper::controlsSideRightValue() {
	return _controls->controls.layout().value(
	) | rpl::map([=](const auto &layout) {
		return !layout.onLeft();
	}) | rpl::distinct_until_changed();
}

void DefaultOverlayWidgetHelper::beforeShow(bool fullscreen) {
	_buttons->clearState();
}

void DefaultOverlayWidgetHelper::clearState() {
	_buttons->clearState();
}

void DefaultOverlayWidgetHelper::setControlsOpacity(float64 opacity) {
	_buttons->setMasterOpacity(opacity);
}

auto DefaultOverlayWidgetHelper::mouseEvents() const
-> rpl::producer<not_null<QMouseEvent*>> {
	return _controls->wrap.events(
	) | rpl::filter([](not_null<QEvent*> e) {
		const auto type = e->type();
		return (type == QEvent::MouseButtonPress)
			|| (type == QEvent::MouseButtonRelease)
			|| (type == QEvent::MouseMove)
			|| (type == QEvent::MouseButtonDblClick);
	}) | rpl::map([](not_null<QEvent*> e) {
		return not_null{ static_cast<QMouseEvent*>(e.get()) };
	});
}

} // namespace Platform
