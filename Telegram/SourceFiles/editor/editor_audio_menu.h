/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"
#include "ui/effects/animations.h"
#include "ui/widgets/menu/menu_item_base.h"

namespace Ui {
class CrossLineAnimation;
class MediaSlider;
class PopupMenu;
namespace Paint {
class ArcsAnimation;
} // namespace Paint
} // namespace Ui

namespace Editor {

struct AudioTrack;

class MenuVolumeItem final : public Ui::Menu::ItemBase {
public:
	MenuVolumeItem(
		not_null<Ui::Menu::Menu*> parent,
		const style::Menu &st,
		float64 volume);

	not_null<QAction*> action() const override;
	bool isEnabled() const override;

	[[nodiscard]] rpl::producer<float64> changes() const;

protected:
	int contentHeight() const override;

private:
	void initArcsAnimation();
	void applyVolume(float64 value);
	void updateSliderColor(float64 value);

	[[nodiscard]] QColor unmuteColor() const;
	[[nodiscard]] QColor muteColor() const;

	QRect _itemRect;
	QRect _speakerRect;
	QPoint _arcPosition;
	bool _muted = false;

	const base::unique_qptr<Ui::MediaSlider> _slider;
	const not_null<QAction*> _dummyAction;
	const style::Menu &_st;
	const style::CrossLineAnimation &_stCross;

	const std::unique_ptr<Ui::CrossLineAnimation> _crossLineMute;
	Ui::Animations::Simple _crossLineAnimation;
	const std::unique_ptr<Ui::Paint::ArcsAnimation> _arcs;
	Ui::Animations::Basic _arcsAnimation;

	rpl::event_stream<float64> _changes;

};

void AddVolumeAction(
	not_null<Ui::PopupMenu*> menu,
	float64 volume,
	Fn<void(float64)> changed);

[[nodiscard]] base::unique_qptr<Ui::PopupMenu> CreateAudioMenu(
	not_null<QWidget*> parent,
	std::shared_ptr<AudioTrack> track,
	Fn<void()> volumeChanged,
	Fn<void()> remove);

} // namespace Editor
