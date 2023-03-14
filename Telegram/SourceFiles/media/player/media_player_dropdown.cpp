/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/player/media_player_dropdown.h"

#include "base/timer.h"
#include "lang/lang_keys.h"
#include "ui/cached_round_corners.h"
#include "ui/widgets/menu/menu.h"
#include "ui/widgets/menu/menu_action.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/widgets/shadow.h"
#include "ui/painter.h"
#include "styles/style_media_player.h"
#include "styles/style_widgets.h"

#include "base/debug_log.h"

namespace Media::Player {
namespace {

constexpr auto kSpeedMin = 0.5;
constexpr auto kSpeedMax = 2.5;
constexpr auto kSpeedDebounceTimeout = crl::time(1000);

[[nodiscard]] float64 SpeedToSliderValue(float64 speed) {
	return (speed - kSpeedMin) / (kSpeedMax - kSpeedMin);
}

[[nodiscard]] float64 SliderValueToSpeed(float64 value) {
	const auto speed = value * (kSpeedMax - kSpeedMin) + kSpeedMin;
	return base::SafeRound(speed * 10) / 10.;
}

constexpr auto kSpeedStickedValues =
	std::array<std::pair<float64, float64>, 7>{{
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
: Ui::Menu::ItemBase(parent, st.menu)
, _slider(base::make_unique_q<Ui::MediaSlider>(this, st.slider))
, _dummyAction(new QAction(parent))
, _st(st)
, _height(st.sliderPadding.top()
	+ st.menu.itemStyle.font->height
	+ st.sliderPadding.bottom())
, _debounceTimer([=] { _debounced.fire(current()); }) {
	initResizeHook(parent->sizeValue());
	enableMouseSelecting();
	enableMouseSelecting(_slider.get());

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

		p.fillRect(clip, _st.menu.itemBg);

		const auto left = (_st.sliderPadding.left() - _text.maxWidth()) / 2;
		const auto top = _st.menu.itemPadding.top();
		p.setPen(_st.menu.itemFg);
		_text.drawLeftElided(p, left, top, _text.maxWidth(), width());
	}, lifetime());

	_slider->setChangeProgressCallback([=](float64 value) {
		const auto speed = SliderValueToSpeed(value);
		if (current() != speed) {
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
	menu->addSeparator(&st.menu.separator);

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
			st::mediaSpeedSlow,
			st::mediaSpeedSlowActive },
		{
			1.0,
			tr::lng_voice_speed_normal,
			st::mediaSpeedNormal,
			st::mediaSpeedNormalActive },
		{
			1.2,
			tr::lng_voice_speed_medium,
			st::mediaSpeedMedium,
			st::mediaSpeedMediumActive },
		{
			1.5,
			tr::lng_voice_speed_fast,
			st::mediaSpeedFast,
			st::mediaSpeedFastActive },
		{
			1.7,
			tr::lng_voice_speed_very_fast,
			st::mediaSpeedVeryFast,
			st::mediaSpeedVeryFastActive },
		{
			2.0,
			tr::lng_voice_speed_super_fast,
			st::mediaSpeedSuperFast,
			st::mediaSpeedSuperFastActive },
	};
	for (const auto &point : points) {
		const auto speed = point.speed;
		const auto text = point.text(tr::now);
		const auto icon = &point.icon;
		const auto iconActive = &point.iconActive;
		auto action = base::make_unique_q<Ui::Menu::Action>(
			menu,
			st::mediaSpeedMenu.menu,
			Ui::Menu::CreateAction(menu, text, [=] { callback(speed); }),
			&point.icon,
			&point.icon);
		const auto raw = action.get();
		const auto check = Ui::CreateChild<Ui::RpWidget>(raw);
		const auto skip = st.activeCheckSkip;
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
			const auto chosen = (speed == now);
			const auto overriden = chosen ? iconActive : icon;
			raw->setIcon(overriden, overriden);
			raw->action()->setEnabled(!chosen);
			check->setVisible(chosen);
		}, raw->lifetime());
		menu->addAction(std::move(action));
	}
}

} // namespace Media::Player
