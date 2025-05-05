/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"
#include "ui/effects/animations.h"
#include "ui/gl/gl_window.h"

namespace base {
class PowerSaveBlocker;
} // namespace base

namespace style {
struct WindowTitle;
} // namespace style

namespace Ui {
class BoxContent;
class RpWindow;
class RpWidget;
class LayerManager;
class LayerWidget;
enum class LayerOption;
using LayerOptions = base::flags<LayerOption>;
class Show;
} // namespace Ui

namespace Ui::Platform {
struct SeparateTitleControls;
struct TitleLayout;
} // namespace Ui::Platform

namespace Ui::Toast {
struct Config;
class Instance;
} // namespace Ui::Toast

namespace Calls {

class Window final : public base::has_weak_ptr {
public:
	Window();
	~Window();

	[[nodiscard]] Ui::GL::Backend backend() const;
	[[nodiscard]] not_null<Ui::RpWindow*> window() const;
	[[nodiscard]] not_null<Ui::RpWidget*> widget() const;

	void raiseControls();
	void setControlsStyle(const style::WindowTitle &st);
	void setControlsShown(float64 shown);
	[[nodiscard]] int controlsWrapTop() const;
	[[nodiscard]] QRect controlsGeometry() const;
	[[nodiscard]] auto controlsLayoutChanges() const
		-> rpl::producer<Ui::Platform::TitleLayout>;
	[[nodiscard]] bool controlsHasHitTest(QPoint widgetPoint) const;
	[[nodiscard]] rpl::producer<bool> maximizeRequests() const;

	void raiseLayers();
	[[nodiscard]] const Ui::LayerWidget *topShownLayer() const;

	base::weak_ptr<Ui::Toast::Instance> showToast(
		const QString &text,
		crl::time duration = 0);
	base::weak_ptr<Ui::Toast::Instance> showToast(
		TextWithEntities &&text,
		crl::time duration = 0);
	base::weak_ptr<Ui::Toast::Instance> showToast(
		Ui::Toast::Config &&config);

	void showBox(object_ptr<Ui::BoxContent> box);
	void showBox(
		object_ptr<Ui::BoxContent> box,
		Ui::LayerOptions options,
		anim::type animated = anim::type::normal);
	void showLayer(
		std::unique_ptr<Ui::LayerWidget> layer,
		Ui::LayerOptions options,
		anim::type animated = anim::type::normal);
	void hideLayer(anim::type animated = anim::type::normal);
	[[nodiscard]] bool isLayerShown() const;

	[[nodiscard]] rpl::producer<> showingLayer() const {
		return _showingLayer.events();
	}

	[[nodiscard]] std::shared_ptr<Ui::Show> uiShow();

	void togglePowerSaveBlocker(bool enabled);

private:
	Ui::GL::Window _window;
	const std::unique_ptr<Ui::LayerManager> _layerBg;

#ifndef Q_OS_MAC
	rpl::variable<int> _controlsTop = 0;
	const std::unique_ptr<Ui::Platform::SeparateTitleControls> _controls;
#endif // !Q_OS_MAC

	std::unique_ptr<base::PowerSaveBlocker> _powerSaveBlocker;

	rpl::event_stream<bool> _maximizeRequests;
	rpl::event_stream<> _showingLayer;

};

} // namespace Calls
