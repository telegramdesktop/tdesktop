/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "media/media_common.h"
#include "ui/rp_widget.h"
#include "ui/effects/animations.h"

namespace style {
struct MediaSpeedMenu;
struct MediaSpeedButton;
struct DropdownMenu;
} // namespace style

namespace Ui {
class DropdownMenu;
class AbstractButton;
class IconButton;
} // namespace Ui

namespace Ui::Menu {
class Menu;
} // namespace Ui::Menu

namespace Media::Player {

class SpeedButton;

class Dropdown final : public Ui::RpWidget {
public:
	explicit Dropdown(QWidget *parent);

	bool overlaps(const QRect &globalRect);

	QMargins getMargin() const;

protected:
	void paintEvent(QPaintEvent *e) override;
	void enterEventHook(QEnterEvent *e) override;
	void leaveEventHook(QEvent *e) override;

	bool eventFilter(QObject *obj, QEvent *e) override;

private:
	void startHide();
	void startShow();

	void otherEnter();
	void otherLeave();

	void appearanceCallback();
	void hidingFinished();
	void startAnimation();

	bool _hiding = false;

	QPixmap _cache;
	Ui::Animations::Simple _a_appearance;

	base::Timer _hideTimer;
	base::Timer _showTimer;

};

class WithDropdownController {
public:
	WithDropdownController(
		not_null<Ui::AbstractButton*> button,
		not_null<QWidget*> menuParent,
		const style::DropdownMenu &menuSt,
		Qt::Alignment menuAlign,
		Fn<void(bool)> menuOverCallback);
	virtual ~WithDropdownController() = default;

	[[nodiscard]] not_null<Ui::AbstractButton*> button() const;
	Ui::DropdownMenu *menu() const;

	void updateDropdownGeometry();

	void hideTemporarily();
	void showBack();

protected:
	void showMenu();

private:
	virtual void fillMenu(not_null<Ui::DropdownMenu*> menu) = 0;

	const not_null<Ui::AbstractButton*> _button;
	const not_null<QWidget*> _menuParent;
	const style::DropdownMenu &_menuSt;
	const Qt::Alignment _menuAlign = Qt::AlignTop | Qt::AlignRight;
	const Fn<void(bool)> _menuOverCallback;
	base::unique_qptr<Ui::DropdownMenu> _menu;
	bool _temporarilyHidden = false;
	bool _overButton = false;

};

class OrderController final : public WithDropdownController {
public:
	OrderController(
		not_null<Ui::IconButton*> button,
		not_null<QWidget*> menuParent,
		Fn<void(bool)> menuOverCallback,
		rpl::producer<OrderMode> value,
		Fn<void(OrderMode)> change);

private:
	void fillMenu(not_null<Ui::DropdownMenu*> menu) override;
	void updateIcon();

	const not_null<Ui::IconButton*> _button;
	rpl::variable<OrderMode> _appOrder;
	Fn<void(OrderMode)> _change;

};

class SpeedController final : public WithDropdownController {
public:
	SpeedController(
		not_null<SpeedButton*> button,
		not_null<QWidget*> menuParent,
		Fn<void(bool)> menuOverCallback,
		Fn<float64(bool lastNonDefault)> value,
		Fn<void(float64)> change);

	[[nodiscard]] rpl::producer<> saved() const;

private:
	void fillMenu(not_null<Ui::DropdownMenu*> menu) override;

	[[nodiscard]] float64 speed() const;
	[[nodiscard]] bool isDefault() const;
	[[nodiscard]] float64 lastNonDefaultSpeed() const;
	void toggleDefault();
	void setSpeed(float64 newSpeed);
	void save();

	const style::MediaSpeedButton &_st;
	Fn<float64(bool lastNonDefault)> _lookup;
	Fn<void(float64)> _change;
	float64 _speed = kSpedUpDefault;
	bool _isDefault = true;
	rpl::event_stream<float64> _speedChanged;
	rpl::event_stream<> _saved;

};

} // namespace Media::Player
