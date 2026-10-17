/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/editor_audio_menu.h"

#include "editor/photo_editor_common.h"
#include "lang/lang_keys.h"
#include "ui/effects/animation_value.h"
#include "ui/effects/cross_line.h"
#include "ui/paint/arcs.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/widgets/popup_menu.h"
#include "ui/color_int_conversion.h"
#include "ui/rect.h"
#include "styles/style_calls.h"
#include "styles/style_editor.h"

namespace Editor {
namespace {

constexpr auto kMuteDuration = crl::time(150);

const auto kSpeakerThresholds = std::vector<float>{ 0.05f, 0.25f, 0.75f };

constexpr auto kVolumeStickedValues
	= std::array<std::pair<float64, float64>, 3>{{
		{ 0.25, 0.02 },
		{ 0.5, 0.02 },
		{ 0.75, 0.02 },
	}};

} // namespace

MenuVolumeItem::MenuVolumeItem(
	not_null<Ui::Menu::Menu*> parent,
	const style::Menu &st,
	float64 volume)
: Ui::Menu::ItemBase(parent, st)
, _muted(volume <= 0.)
, _slider(base::make_unique_q<Ui::MediaSlider>(
	this,
	st::groupCallMenuVolumeSlider))
, _dummyAction(new QAction(parent))
, _st(st)
, _stCross(st::groupCallMuteCrossLine)
, _crossLineMute(std::make_unique<Ui::CrossLineAnimation>(_stCross, true))
, _arcs(std::make_unique<Ui::Paint::ArcsAnimation>(
	st::groupCallSpeakerArcsAnimation,
	kSpeakerThresholds,
	volume,
	Ui::Paint::ArcsAnimation::Direction::Right)) {
	fitToMenuWidth();
	enableMouseSelecting();
	enableMouseSelecting(_slider.get());

	_slider->setAlwaysDisplayMarker(true);

	sizeValue(
	) | rpl::on_next([=](const QSize &size) {
		const auto &margin = st::photoEditorAudioVolumeMargin;
		_itemRect = Rect(size) - st::photoEditorAudioVolumePadding;
		_speakerRect = QRect(_itemRect.topLeft(), _stCross.icon.size());
		_arcPosition = rect::center(_speakerRect)
			+ QPoint(0, st::groupCallMenuSpeakerArcsSkip);
		_slider->setGeometry(
			margin.left(),
			_speakerRect.y(),
			size.width() - margin.left() - margin.right(),
			_speakerRect.height());
	}, lifetime());

	_slider->setValue(volume);
	updateSliderColor(volume);

	paintRequest(
	) | rpl::on_next([=](const QRect &clip) {
		auto p = QPainter(this);
		const auto muteProgress = _crossLineAnimation.value(_muted ? 1. : 0.);
		p.fillRect(clip, isSelected() ? _st.itemBgOver : _st.itemBg);
		const auto mutePen = anim::color(
			unmuteColor(),
			muteColor(),
			muteProgress);
		_crossLineMute->paint(
			p,
			_speakerRect.topLeft(),
			muteProgress,
			(muteProgress > 0) ? std::make_optional(mutePen) : std::nullopt);
		p.translate(_arcPosition);
		_arcs->paint(p);
	}, lifetime());

	_slider->setChangeProgressCallback([=](float64 value) {
		applyVolume(value);
	});
	_slider->setAdjustCallback([=](float64 value) {
		for (const auto &snap : kVolumeStickedValues) {
			if (value > (snap.first - snap.second)
				&& value < (snap.first + snap.second)) {
				return snap.first;
			}
		}
		return value;
	});

	initArcsAnimation();
}

void MenuVolumeItem::initArcsAnimation() {
	_arcsAnimation.init([=](crl::time now) {
		_arcs->update(now);
		update(_speakerRect);
	});

	_arcs->startUpdateRequests(
	) | rpl::on_next([=] {
		if (!_arcsAnimation.animating()) {
			_arcsAnimation.start();
		}
	}, lifetime());

	_arcs->stopUpdateRequests(
	) | rpl::on_next([=] {
		_arcsAnimation.stop();
	}, lifetime());
}

void MenuVolumeItem::applyVolume(float64 value) {
	const auto muted = (value <= 0.);
	if (_muted != muted) {
		_muted = muted;
		_crossLineAnimation.start(
			[=] { update(_speakerRect); },
			muted ? 0. : 1.,
			muted ? 1. : 0.,
			kMuteDuration);
	}
	_arcs->setValue(value);
	updateSliderColor(value);
	_changes.fire_copy(value);
}

QColor MenuVolumeItem::unmuteColor() const {
	return (isSelected()
		? _st.itemFgOver
		: isEnabled()
		? _st.itemFg
		: _st.itemFgDisabled)->c;
}

QColor MenuVolumeItem::muteColor() const {
	return (isSelected()
		? st::attentionButtonFgOver
		: st::attentionButtonFg)->c;
}

void MenuVolumeItem::updateSliderColor(float64 value) {
	value = std::clamp(value, 0., 1.);
	const auto colors = std::array<QColor, 4>{ {
		Ui::ColorFromSerialized(0xF66464),
		Ui::ColorFromSerialized(0xD0B738),
		Ui::ColorFromSerialized(0x24CD80),
		Ui::ColorFromSerialized(0x3BBCEC),
	} };
	_slider->setColorOverrides({
		.activeFg = (value < 0.25)
			? anim::color(colors[0], colors[1], value / 0.25)
			: (value < 0.5)
			? anim::color(colors[1], colors[2], (value - 0.25) / 0.25)
			: anim::color(colors[2], colors[3], (value - 0.5) / 0.5),
	});
}

not_null<QAction*> MenuVolumeItem::action() const {
	return _dummyAction;
}

bool MenuVolumeItem::isEnabled() const {
	return true;
}

int MenuVolumeItem::contentHeight() const {
	return rect::m::sum::v(st::photoEditorAudioVolumePadding)
		+ _stCross.icon.height();
}

rpl::producer<float64> MenuVolumeItem::changes() const {
	return _changes.events();
}

void AddVolumeAction(
		not_null<Ui::PopupMenu*> menu,
		float64 volume,
		Fn<void(float64)> changed) {
	auto item = base::make_unique_q<MenuVolumeItem>(
		menu->menu(),
		st::photoEditorAudioVolumeItem,
		volume);
	item->changes(
	) | rpl::on_next(std::move(changed), item->lifetime());
	menu->addAction(std::move(item));
}

base::unique_qptr<Ui::PopupMenu> CreateAudioMenu(
		not_null<QWidget*> parent,
		std::shared_ptr<AudioTrack> track,
		Fn<void()> volumeChanged,
		Fn<void()> remove) {
	auto result = base::make_unique_q<Ui::PopupMenu>(
		parent,
		st::photoEditorMediaMenu);
	AddVolumeAction(result.get(), track->volume, [=](float64 value) {
		track->volume = value;
		volumeChanged();
	});
	result->addSeparator();
	result->addAction(
		tr::lng_photo_editor_audio_remove(tr::now),
		std::move(remove),
		&st::photoEditorMenuDelete);
	return result;
}

} // namespace Editor
