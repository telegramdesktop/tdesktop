/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_qptr.h"
#include "ui/style/style_core_palette.h"

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Window::Theme {
enum class EmbeddedType;
class EmbeddedAccentRecipe;
} // namespace Window::Theme

namespace Wallet {

struct CardLook;

[[nodiscard]] QColor ComputeMatchedAccent(
	Window::Theme::EmbeddedType type,
	const QColor &target);
[[nodiscard]] QColor CardMatchedAccent(Window::Theme::EmbeddedType type);

class WindowPalette final {
public:
	explicit WindowPalette(not_null<QWidget*> window);

	[[nodiscard]] const style::palette *resolve(bool child);
	void prepare(const CardLook &look);
	void setLook(const CardLook &look);
	void holdChildren();
	void releaseChildren();

private:
	struct Derived {
		Window::Theme::EmbeddedType type = {};
		QRgb target = 0;
		QColor accent;
	};

	void notify();
	void rebuild();
	[[nodiscard]] QColor accent(Window::Theme::EmbeddedType type);
	QColor derived(Window::Theme::EmbeddedType type, const QColor &target);

	const base::weak_qptr<QWidget> _window;
	std::unique_ptr<style::palette> _palette;
	std::unique_ptr<style::palette> _children;
	const Window::Theme::EmbeddedAccentRecipe *_recipe = nullptr;
	std::optional<Window::Theme::EmbeddedType> _type;
	std::optional<QColor> _target;
	std::vector<Derived> _derived;
	int _version = -1;
	bool _childrenHeld = false;

};

[[nodiscard]] std::shared_ptr<WindowPalette> UseWindowPalette(
	not_null<Ui::RpWidget*> window);
[[nodiscard]] style::main_palette::Override WindowPaletteScope(
	not_null<const QWidget*> widget);
[[nodiscard]] rpl::producer<> WindowPaletteChanged(
	not_null<const QWidget*> widget);

} // namespace Wallet
