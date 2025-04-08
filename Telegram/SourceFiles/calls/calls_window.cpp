/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/calls_window.h"

#include "base/power_save_blocker.h"
#include "ui/platform/ui_platform_window_title.h"
#include "ui/widgets/rp_window.h"
#include "ui/layers/layer_manager.h"
#include "ui/layers/show.h"
#include "styles/style_calls.h"

namespace Calls {
namespace {

class Show final : public Ui::Show {
public:
	explicit Show(not_null<Window*> window);
	~Show();

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
	const base::weak_ptr<Window> _window;

};

Show::Show(not_null<Window*> window)
: _window(base::make_weak(window)) {
}

Show::~Show() = default;

void Show::showOrHideBoxOrLayer(
		std::variant<
			v::null_t,
			object_ptr<Ui::BoxContent>,
			std::unique_ptr<Ui::LayerWidget>> &&layer,
		Ui::LayerOptions options,
		anim::type animated) const {
	using UniqueLayer = std::unique_ptr<Ui::LayerWidget>;
	using ObjectBox = object_ptr<Ui::BoxContent>;
	if (auto layerWidget = std::get_if<UniqueLayer>(&layer)) {
		if (const auto window = _window.get()) {
			window->showLayer(std::move(*layerWidget), options, animated);
		}
	} else if (auto box = std::get_if<ObjectBox>(&layer)) {
		if (const auto window = _window.get()) {
			window->showBox(std::move(*box), options, animated);
		}
	} else if (const auto window = _window.get()) {
		window->hideLayer(animated);
	}
}

not_null<QWidget*> Show::toastParent() const {
	const auto window = _window.get();
	Assert(window != nullptr);
	return window->widget();
}

bool Show::valid() const {
	return !_window.empty();
}

Show::operator bool() const {
	return valid();
}

} // namespace

Window::Window()
: _layerBg(std::make_unique<Ui::LayerManager>(widget()))
#ifndef Q_OS_MAC
, _controls(Ui::Platform::SetupSeparateTitleControls(
	window(),
	st::callTitle,
	[=](bool maximized) { _maximizeRequests.fire_copy(maximized); },
	_controlsTop.value()))
#endif // !Q_OS_MAC
{
	_layerBg->setStyleOverrides(&st::groupCallBox, &st::groupCallLayerBox);
	_layerBg->setHideByBackgroundClick(true);
}

Window::~Window() = default;

Ui::GL::Backend Window::backend() const {
	return _window.backend();
}

not_null<Ui::RpWindow*> Window::window() const {
	return _window.window();
}

not_null<Ui::RpWidget*> Window::widget() const {
	return _window.widget();
}

void Window::raiseControls() {
#ifndef Q_OS_MAC
	_controls->wrap.raise();
#endif // !Q_OS_MAC
}

void Window::setControlsStyle(const style::WindowTitle &st) {
#ifndef Q_OS_MAC
	_controls->controls.setStyle(st);
#endif // Q_OS_MAC
}

void Window::setControlsShown(float64 shown) {
#ifndef Q_OS_MAC
	_controlsTop = anim::interpolate(-_controls->wrap.height(), 0, shown);
#endif // Q_OS_MAC
}

int Window::controlsWrapTop() const {
#ifndef Q_OS_MAC
	return _controls->wrap.y();
#else // Q_OS_MAC
	return 0;
#endif // Q_OS_MAC
}

QRect Window::controlsGeometry() const {
#ifndef Q_OS_MAC
	return _controls->controls.geometry();
#else // Q_OS_MAC
	return QRect();
#endif // Q_OS_MAC
}

auto Window::controlsLayoutChanges() const
-> rpl::producer<Ui::Platform::TitleLayout> {
#ifndef Q_OS_MAC
	return _controls->controls.layout().changes();
#else // Q_OS_MAC
	return rpl::never<Ui::Platform::TitleLayout>();
#endif // Q_OS_MAC
}

bool Window::controlsHasHitTest(QPoint widgetPoint) const {
#ifndef Q_OS_MAC
	using Result = Ui::Platform::HitTestResult;
	const auto windowPoint = widget()->mapTo(window(), widgetPoint);
	return (_controls->controls.hitTest(windowPoint) != Result::None);
#else // Q_OS_MAC
	return false;
#endif // Q_OS_MAC
}

rpl::producer<bool> Window::maximizeRequests() const {
	return _maximizeRequests.events();
}

base::weak_ptr<Ui::Toast::Instance> Window::showToast(
		const QString &text,
		crl::time duration) {
	return Show(this).showToast(text, duration);
}

base::weak_ptr<Ui::Toast::Instance> Window::showToast(
		TextWithEntities &&text,
		crl::time duration) {
	return Show(this).showToast(std::move(text), duration);
}

base::weak_ptr<Ui::Toast::Instance> Window::showToast(
		Ui::Toast::Config &&config) {
	return Show(this).showToast(std::move(config));
}

void Window::raiseLayers() {
	_layerBg->raise();
}

const Ui::LayerWidget *Window::topShownLayer() const {
	return _layerBg->topShownLayer();
}

void Window::showBox(object_ptr<Ui::BoxContent> box) {
	showBox(std::move(box), Ui::LayerOption::KeepOther, anim::type::normal);
}

void Window::showBox(
		object_ptr<Ui::BoxContent> box,
		Ui::LayerOptions options,
		anim::type animated) {
	_showingLayer.fire({});
	if (window()->width() < st::groupCallWidth
		|| window()->height() < st::groupCallWidth) {
		window()->resize(
			std::max(window()->width(), st::groupCallWidth),
			std::max(window()->height(), st::groupCallWidth));
	}
	_layerBg->showBox(std::move(box), options, animated);
}

void Window::showLayer(
		std::unique_ptr<Ui::LayerWidget> layer,
		Ui::LayerOptions options,
		anim::type animated) {
	_showingLayer.fire({});
	if (window()->width() < st::groupCallWidth
		|| window()->height() < st::groupCallWidth) {
		window()->resize(
			std::max(window()->width(), st::groupCallWidth),
			std::max(window()->height(), st::groupCallWidth));
	}
	_layerBg->showLayer(std::move(layer), options, animated);
}

void Window::hideLayer(anim::type animated) {
	_layerBg->hideAll(animated);
}

bool Window::isLayerShown() const {
	return _layerBg->topShownLayer() != nullptr;
}

std::shared_ptr<Ui::Show> Window::uiShow() {
	return std::make_shared<Show>(this);
}

void Window::togglePowerSaveBlocker(bool enabled) {
	if (!enabled) {
		_powerSaveBlocker = nullptr;
	} else if (!_powerSaveBlocker) {
		_powerSaveBlocker = std::make_unique<base::PowerSaveBlocker>(
			base::PowerSaveBlockType::PreventDisplaySleep,
			u"Video call is active"_q,
			window()->windowHandle());
	}
}

} // namespace Calls
