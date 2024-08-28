/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/player/media_player_dropdown.h"

#include "base/invoke_queued.h"
#include "base/timer.h"
#include "lang/lang_keys.h"
#include "media/player/media_player_button.h"
#include "ui/cached_round_corners.h"
#include "ui/widgets/menu/menu.h"
#include "ui/widgets/menu/menu_action.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/widgets/dropdown_menu.h"
#include "ui/widgets/shadow.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "styles/style_media_player.h"
#include "styles/style_widgets.h"

namespace Media::Player {
namespace {

constexpr auto kSpeedDebounceTimeout = crl::time(1000);

[[nodiscard]] float64 SpeedToSliderValue(float64 speed) {
	return (speed - kSpeedMin) / (kSpeedMax - kSpeedMin);
}

[[nodiscard]] float64 SliderValueToSpeed(float64 value) {
	const auto speed = value * (kSpeedMax - kSpeedMin) + kSpeedMin;
	return base::SafeRound(speed * 10) / 10.;
}

constexpr auto kSpeedStickedValues
	= std::array<std::pair<float64, float64>, 7>{{
		{ 0.8, 0.05 },
		{ 1.0, 0.05 },
		{ 1.2, 0.05 },
		{ 1.5, 0.05 },
		{ 1.7, 0.05 },
		{ 2.0, 0.05 },
		{ 2.2, 0.05 },
	}};

class SpeedSliderItem final : public Ui::Menu::ItemBase {
public:
	SpeedSliderItem(
		not_null<RpWidget*> parent,
		const style::MediaSpeedMenu &st,
		rpl::producer<float64> value);

	not_null<QAction*> action() const override;
	bool isEnabled() const override;

	[[nodiscard]] float64 current() const;
	[[nodiscard]] rpl::producer<float64> changing() const;
	[[nodiscard]] rpl::producer<float64> changed() const;
	[[nodiscard]] rpl::producer<float64> debouncedChanges() const;

protected:
	int contentHeight() const override;

private:
	void setExternalValue(float64 speed);
	void setSliderValue(float64 speed);

	const base::unique_qptr<Ui::MediaSlider> _slider;
	const not_null<QAction*> _dummyAction;
	const style::MediaSpeedMenu &_st;
	Ui::Text::String _text;
	int _height = 0;

	rpl::event_stream<float64> _changing;
	rpl::event_stream<float64> _changed;
	rpl::event_stream<float64> _debounced;
	base::Timer _debounceTimer;
	rpl::variable<float64> _last = 0.;

};

SpeedSliderItem::SpeedSliderItem(
	not_null<RpWidget*> parent,
	const style::MediaSpeedMenu &st,
	rpl::producer<float64> value)
: Ui::Menu::ItemBase(parent, st.dropdown.menu)
, _slider(base::make_unique_q<Ui::MediaSlider>(this, st.slider))
, _dummyAction(new QAction(parent))
, _st(st)
, _height(st.sliderPadding.top()
	+ st.dropdown.menu.itemStyle.font->height
	+ st.sliderPadding.bottom())
, _debounceTimer([=] { _debounced.fire(current()); }) {
	initResizeHook(parent->sizeValue());
	enableMouseSelecting();
	enableMouseSelecting(_slider.get());

	setPointerCursor(false);
	setMinWidth(st.sliderPadding.left()
		+ st.sliderWidth
		+ st.sliderPadding.right());
	_slider->setAlwaysDisplayMarker(true);

	sizeValue(
	) | rpl::start_with_next([=](const QSize &size) {
		const auto geometry = QRect(QPoint(), size);
		const auto padding = _st.sliderPadding;
		const auto inner = geometry - padding;
		_slider->setGeometry(
			padding.left(),
			inner.y(),
			(geometry.width() - padding.left() - padding.right()),
			inner.height());
	}, lifetime());

	paintRequest(
	) | rpl::start_with_next([=](const QRect &clip) {
		auto p = Painter(this);

		p.fillRect(clip, _st.dropdown.menu.itemBg);

		const auto left = (_st.sliderPadding.left() - _text.maxWidth()) / 2;
		const auto top = _st.dropdown.menu.itemPadding.top();
		p.setPen(_st.dropdown.menu.itemFg);
		_text.drawLeftElided(p, left, top, _text.maxWidth(), width());
	}, lifetime());

	_slider->setChangeProgressCallback([=](float64 value) {
		const auto speed = SliderValueToSpeed(value);
		if (!EqualSpeeds(current(), speed)) {
			_last = speed;
			_changing.fire_copy(speed);
			_debounceTimer.callOnce(kSpeedDebounceTimeout);
		}
	});

	_slider->setChangeFinishedCallback([=](float64 value) {
		const auto speed = SliderValueToSpeed(value);
		_last = speed;
		_changed.fire_copy(speed);
		_debounced.fire_copy(speed);
		_debounceTimer.cancel();
	});

	std::move(
		value
	) | rpl::start_with_next([=](float64 external) {
		setExternalValue(external);
	}, lifetime());

	_last.value(
	) | rpl::start_with_next([=](float64 value) {
		const auto text = QString::number(value, 'f', 1) + 'x';
		if (_text.toString() != text) {
			_text.setText(_st.sliderStyle, text);
			update();
		}
	}, lifetime());

	_slider->setAdjustCallback([=](float64 value) {
		const auto speed = SliderValueToSpeed(value);
		for (const auto &snap : kSpeedStickedValues) {
			if (speed > (snap.first - snap.second)
				&& speed < (snap.first + snap.second)) {
				return SpeedToSliderValue(snap.first);
			}
		}
		return value;
	});
}

void FillSpeedMenu(
		not_null<Ui::Menu::Menu*> menu,
		const style::MediaSpeedMenu &st,
		rpl::producer<float64> value,
		Fn<void(float64)> callback) {
	auto slider = base::make_unique_q<SpeedSliderItem>(
		menu,
		st,
		rpl::duplicate(value));

	slider->debouncedChanges(
	) | rpl::start_with_next(callback, slider->lifetime());

	struct State {
		rpl::variable<float64> realtime;
	};
	const auto state = slider->lifetime().make_state<State>();
	state->realtime = rpl::single(
		slider->current()
	) | rpl::then(rpl::merge(
		slider->changing(),
		slider->changed()
	));

	menu->addAction(std::move(slider));
	menu->addSeparator(&st.dropdown.menu.separator);

	struct SpeedPoint {
		float64 speed = 0.;
		tr::phrase<> text;
		const style::icon &icon;
		const style::icon &iconActive;
	};
	const auto points = std::vector<SpeedPoint>{
		{
			0.5,
			tr::lng_voice_speed_slow,
			st.slow,
			st.slowActive },
		{
			1.0,
			tr::lng_voice_speed_normal,
			st.normal,
			st.normalActive },
		{
			1.2,
			tr::lng_voice_speed_medium,
			st.medium,
			st.mediumActive },
		{
			1.5,
			tr::lng_voice_speed_fast,
			st.fast,
			st.fastActive },
		{
			1.7,
			tr::lng_voice_speed_very_fast,
			st.veryFast,
			st.veryFastActive },
		{
			2.0,
			tr::lng_voice_speed_super_fast,
			st.superFast,
			st.superFastActive },
	};
	for (const auto &point : points) {
		const auto speed = point.speed;
		const auto text = point.text(tr::now);
		const auto icon = &point.icon;
		const auto iconActive = &point.iconActive;
		auto action = base::make_unique_q<Ui::Menu::Action>(
			menu,
			st.dropdown.menu,
			Ui::Menu::CreateAction(menu, text, [=] { callback(speed); }),
			&point.icon,
			&point.icon);
		const auto raw = action.get();
		const auto check = Ui::CreateChild<Ui::RpWidget>(raw);
		check->resize(st.activeCheck.size());
		check->paintRequest(
		) | rpl::start_with_next([check, icon = &st.activeCheck] {
			auto p = QPainter(check);
			icon->paint(p, 0, 0, check->width());
		}, check->lifetime());
		raw->sizeValue(
		) | rpl::start_with_next([=, skip = st.activeCheckSkip](QSize size) {
			check->moveToRight(
				skip,
				(size.height() - check->height()) / 2,
				size.width());
		}, check->lifetime());
		check->setAttribute(Qt::WA_TransparentForMouseEvents);
		state->realtime.value(
		) | rpl::start_with_next([=](float64 now) {
			const auto chosen = EqualSpeeds(speed, now);
			const auto overriden = chosen ? iconActive : icon;
			raw->setIcon(overriden, overriden);
			raw->action()->setEnabled(!chosen);
			check->setVisible(chosen);
		}, raw->lifetime());
		menu->addAction(std::move(action));
	}
}

void SpeedSliderItem::setExternalValue(float64 speed) {
	if (!_slider->isChanging()) {
		setSliderValue(speed);
	}
}

void SpeedSliderItem::setSliderValue(float64 speed) {
	const auto value = SpeedToSliderValue(speed);
	_slider->setValue(value);
	_last = speed;
	_changed.fire_copy(speed);
}

not_null<QAction*> SpeedSliderItem::action() const {
	return _dummyAction;
}

bool SpeedSliderItem::isEnabled() const {
	return false;
}

int SpeedSliderItem::contentHeight() const {
	return _height;
}

float64 SpeedSliderItem::current() const {
	return _last.current();
}

rpl::producer<float64> SpeedSliderItem::changing() const {
	return _changing.events();
}

rpl::producer<float64> SpeedSliderItem::changed() const {
	return _changed.events();
}

rpl::producer<float64> SpeedSliderItem::debouncedChanges() const {
	return _debounced.events();
}

} // namespace

Dropdown::Dropdown(QWidget *parent)
: RpWidget(parent)
, _hideTimer([=] { startHide(); })
, _showTimer([=] { startShow(); }) {
	hide();

	macWindowDeactivateEvents(
	) | rpl::filter([=] {
		return !isHidden();
	}) | rpl::start_with_next([=] {
		leaveEvent(nullptr);
	}, lifetime());

	hide();
	auto margin = getMargin();
	resize(margin.left() + st::mediaPlayerVolumeSize.width() + margin.right(), margin.top() + st::mediaPlayerVolumeSize.height() + margin.bottom());
}

QMargins Dropdown::getMargin() const {
	const auto top1 = st::mediaPlayerHeight
		+ st::lineWidth
		- st::mediaPlayerPlayTop
		- st::mediaPlayerVolumeToggle.height;
	const auto top2 = st::mediaPlayerPlayback.fullWidth;
	const auto top = std::max(top1, top2);
	return QMargins(st::mediaPlayerVolumeMargin, top, st::mediaPlayerVolumeMargin, st::mediaPlayerVolumeMargin);
}

bool Dropdown::overlaps(const QRect &globalRect) {
	if (isHidden() || _a_appearance.animating()) return false;

	return rect().marginsRemoved(getMargin()).contains(QRect(mapFromGlobal(globalRect.topLeft()), globalRect.size()));
}

void Dropdown::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	if (!_cache.isNull()) {
		bool animating = _a_appearance.animating();
		if (animating) {
			p.setOpacity(_a_appearance.value(_hiding ? 0. : 1.));
		} else if (_hiding || isHidden()) {
			hidingFinished();
			return;
		}
		p.drawPixmap(0, 0, _cache);
		if (!animating) {
			showChildren();
			_cache = QPixmap();
		}
		return;
	}

	// draw shadow
	auto shadowedRect = rect().marginsRemoved(getMargin());
	auto shadowedSides = RectPart::Left | RectPart::Right | RectPart::Bottom;
	Ui::Shadow::paint(p, shadowedRect, width(), st::defaultRoundShadow, shadowedSides);
	const auto &corners = Ui::CachedCornerPixmaps(Ui::MenuCorners);
	const auto fill = Ui::CornersPixmaps{
		.p = { QPixmap(), QPixmap(), corners.p[2], corners.p[3] },
	};
	Ui::FillRoundRect(
		p,
		shadowedRect.x(),
		0,
		shadowedRect.width(),
		shadowedRect.y() + shadowedRect.height(),
		st::menuBg,
		fill);
}

void Dropdown::enterEventHook(QEnterEvent *e) {
	_hideTimer.cancel();
	if (_a_appearance.animating()) {
		startShow();
	} else {
		_showTimer.callOnce(0);
	}
	return RpWidget::enterEventHook(e);
}

void Dropdown::leaveEventHook(QEvent *e) {
	_showTimer.cancel();
	if (_a_appearance.animating()) {
		startHide();
	} else {
		_hideTimer.callOnce(300);
	}
	return RpWidget::leaveEventHook(e);
}

void Dropdown::otherEnter() {
	_hideTimer.cancel();
	if (_a_appearance.animating()) {
		startShow();
	} else {
		_showTimer.callOnce(0);
	}
}

void Dropdown::otherLeave() {
	_showTimer.cancel();
	if (_a_appearance.animating()) {
		startHide();
	} else {
		_hideTimer.callOnce(0);
	}
}

void Dropdown::startShow() {
	if (isHidden()) {
		show();
	} else if (!_hiding) {
		return;
	}
	_hiding = false;
	startAnimation();
}

void Dropdown::startHide() {
	if (_hiding) {
		return;
	}

	_hiding = true;
	startAnimation();
}

void Dropdown::startAnimation() {
	if (_cache.isNull()) {
		showChildren();
		_cache = Ui::GrabWidget(this);
	}
	hideChildren();
	_a_appearance.start(
		[=] { appearanceCallback(); },
		_hiding ? 1. : 0.,
		_hiding ? 0. : 1.,
		st::defaultInnerDropdown.duration);
}

void Dropdown::appearanceCallback() {
	if (!_a_appearance.animating() && _hiding) {
		_hiding = false;
		hidingFinished();
	} else {
		update();
	}
}

void Dropdown::hidingFinished() {
	hide();
	_cache = QPixmap();
}

bool Dropdown::eventFilter(QObject *obj, QEvent *e) {
	if (e->type() == QEvent::Enter) {
		otherEnter();
	} else if (e->type() == QEvent::Leave) {
		otherLeave();
	}
	return false;
}

WithDropdownController::WithDropdownController(
	not_null<Ui::AbstractButton*> button,
	not_null<QWidget*> menuParent,
	const style::DropdownMenu &menuSt,
	Qt::Alignment menuAlign,
	Fn<void(bool)> menuOverCallback)
: _button(button)
, _menuParent(menuParent)
, _menuSt(menuSt)
, _menuAlign(menuAlign)
, _menuOverCallback(std::move(menuOverCallback)) {
	button->events(
	) | rpl::filter([=](not_null<QEvent*> e) {
		return (e->type() == QEvent::Enter)
			|| (e->type() == QEvent::Leave);
	}) | rpl::start_with_next([=](not_null<QEvent*> e) {
		_overButton = (e->type() == QEvent::Enter);
		if (_overButton) {
			InvokeQueued(button, [=] {
				if (_overButton) {
					showMenu();
				}
			});
		}
	}, button->lifetime());
}

not_null<Ui::AbstractButton*> WithDropdownController::button() const {
	return _button;
}

Ui::DropdownMenu *WithDropdownController::menu() const {
	return _menu.get();
}

void WithDropdownController::updateDropdownGeometry() {
	if (!_menu) {
		return;
	}
	const auto bwidth = _button->width();
	const auto bheight = _button->height();
	const auto mwidth = _menu->width();
	const auto mheight = _menu->height();
	const auto padding = _menuSt.wrap.padding;
	const auto x = st::mediaPlayerMenuPosition.x();
	const auto y = st::mediaPlayerMenuPosition.y();
	const auto position = _menu->parentWidget()->mapFromGlobal(
		_button->mapToGlobal(QPoint())
	) + [&] {
		switch (_menuAlign) {
		case style::al_topleft: return QPoint(
			-padding.left() - x,
			bheight - padding.top() + y);
		case style::al_topright: return QPoint(
			bwidth - mwidth + padding.right() + x,
			bheight - padding.top() + y);
		case style::al_bottomright: return QPoint(
			bwidth - mwidth + padding.right() + x,
			-mheight + padding.bottom() - y);
		case style::al_bottomleft: return QPoint(
			-padding.left() - x,
			-mheight + padding.bottom() - y);
		}
		Unexpected("Menu align value.");
	}();
	_menu->move(position);
}

void WithDropdownController::hideTemporarily() {
	if (_menu && !_menu->isHidden()) {
		_temporarilyHidden = true;
		_menu->hide();
	}
}

void WithDropdownController::showBack() {
	if (_temporarilyHidden) {
		_temporarilyHidden = false;
		if (_menu && _menu->isHidden()) {
			_menu->show();
		}
	}
}

void WithDropdownController::showMenu() {
	if (_menu) {
		return;
	}
	_menu.emplace(_menuParent, _menuSt);
	const auto raw = _menu.get();
	_menu->events(
	) | rpl::start_with_next([this](not_null<QEvent*> e) {
		const auto type = e->type();
		if (type == QEvent::Enter) {
			_menuOverCallback(true);
		} else if (type == QEvent::Leave) {
			_menuOverCallback(false);
		}
	}, _menu->lifetime());
	_menu->setHiddenCallback([=]{
		Ui::PostponeCall(raw, [this] {
			_menu = nullptr;
		});
	});
	_button->installEventFilter(raw);
	fillMenu(raw);
	updateDropdownGeometry();
	const auto origin = [&] {
		using Origin = Ui::PanelAnimation::Origin;
		switch (_menuAlign) {
		case style::al_topleft: return Origin::TopLeft;
		case style::al_topright: return Origin::TopRight;
		case style::al_bottomright: return Origin::BottomRight;
		case style::al_bottomleft: return Origin::BottomLeft;
		}
		Unexpected("Menu align value.");
	}();
	_menu->showAnimated(origin);
}

OrderController::OrderController(
	not_null<Ui::IconButton*> button,
	not_null<QWidget*> menuParent,
	Fn<void(bool)> menuOverCallback,
	rpl::producer<OrderMode> value,
	Fn<void(OrderMode)> change)
: WithDropdownController(
	button,
	menuParent,
	st::mediaPlayerMenu,
	style::al_topright,
	std::move(menuOverCallback))
, _button(button)
, _appOrder(std::move(value))
, _change(std::move(change)) {
	button->setClickedCallback([=] {
		showMenu();
	});

	_appOrder.value(
	) | rpl::start_with_next([=] {
		updateIcon();
	}, button->lifetime());
}

void OrderController::fillMenu(not_null<Ui::DropdownMenu*> menu) {
	const auto addOrderAction = [&](OrderMode mode) {
		struct Fields {
			QString label;
			const style::icon &icon;
			const style::icon &activeIcon;
		};
		const auto active = (_appOrder.current() == mode);
		const auto callback = [change = _change, mode, active] {
			change(active ? OrderMode::Default : mode);
		};
		const auto fields = [&]() -> Fields {
			switch (mode) {
			case OrderMode::Reverse: return {
				.label = tr::lng_audio_player_reverse(tr::now),
				.icon = st::mediaPlayerOrderIconReverse,
				.activeIcon = st::mediaPlayerOrderIconReverseActive,
			};
			case OrderMode::Shuffle: return {
				.label = tr::lng_audio_player_shuffle(tr::now),
				.icon = st::mediaPlayerOrderIconShuffle,
				.activeIcon = st::mediaPlayerOrderIconShuffleActive,
			};
			}
			Unexpected("Order mode in addOrderAction.");
		}();
		menu->addAction(base::make_unique_q<Ui::Menu::Action>(
			menu,
			(active
				? st::mediaPlayerOrderMenuActive
				: st::mediaPlayerOrderMenu),
			Ui::Menu::CreateAction(menu, fields.label, callback),
			&(active ? fields.activeIcon : fields.icon),
			&(active ? fields.activeIcon : fields.icon)));
	};
	addOrderAction(OrderMode::Reverse);
	addOrderAction(OrderMode::Shuffle);
}

void OrderController::updateIcon() {
	switch (_appOrder.current()) {
	case OrderMode::Default:
		_button->setIconOverride(
			&st::mediaPlayerReverseDisabledIcon,
			&st::mediaPlayerReverseDisabledIconOver);
		_button->setRippleColorOverride(
			&st::mediaPlayerRepeatDisabledRippleBg);
		break;
	case OrderMode::Reverse:
		_button->setIconOverride(&st::mediaPlayerReverseIcon);
		_button->setRippleColorOverride(nullptr);
		break;
	case OrderMode::Shuffle:
		_button->setIconOverride(&st::mediaPlayerShuffleIcon);
		_button->setRippleColorOverride(nullptr);
		break;
	}
}

SpeedController::SpeedController(
	not_null<SpeedButton*> button,
	not_null<QWidget*> menuParent,
	Fn<void(bool)> menuOverCallback,
	Fn<float64(bool lastNonDefault)> value,
	Fn<void(float64)> change)
: WithDropdownController(
	button,
	menuParent,
	button->st().menu.dropdown,
	button->st().menuAlign,
	std::move(menuOverCallback))
, _st(button->st())
, _lookup(std::move(value))
, _change(std::move(change)) {
	button->setClickedCallback([=] {
		toggleDefault();
		save();
		if (const auto current = menu()) {
			current->otherEnter();
		}
	});

	setSpeed(_lookup(false));
	_speed = _lookup(true);

	button->setSpeed(_speed, anim::type::instant);

	_speedChanged.events_starting_with(
		speed()
	) | rpl::start_with_next([=](float64 speed) {
		button->setSpeed(speed);
	}, button->lifetime());
}

rpl::producer<> SpeedController::saved() const {
	return _saved.events();
}

float64 SpeedController::speed() const {
	return _isDefault ? 1. : _speed;
}

bool SpeedController::isDefault() const {
	return _isDefault;
}

float64 SpeedController::lastNonDefaultSpeed() const {
	return _speed;
}

void SpeedController::toggleDefault() {
	_isDefault = !_isDefault;
	_speedChanged.fire(speed());
}

void SpeedController::setSpeed(float64 newSpeed) {
	if (!(_isDefault = EqualSpeeds(newSpeed, 1.))) {
		_speed = newSpeed;
	}
	_speedChanged.fire(speed());
}

void SpeedController::save() {
	_change(speed());
	_saved.fire({});
}

void SpeedController::fillMenu(not_null<Ui::DropdownMenu*> menu) {
	FillSpeedMenu(
		menu->menu(),
		_st.menu,
		_speedChanged.events_starting_with(speed()),
		[=](float64 speed) { setSpeed(speed); save(); });
}

} // namespace Media::Player
