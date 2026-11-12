/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "menu/menu_check_item.h"

#include "base/platform/base_platform_haptic.h"
#include "ui/effects/shake_animation.h"
#include "ui/painter.h"
#include "ui/widgets/checkbox.h"
#include "styles/style_media_player.h"

namespace Menu {

void ItemWithCheck::init(bool checked) {
	enableMouseSelecting();

	ItemBase::setPreventClose(true);

	_checkView = std::make_unique<Ui::CheckView>(st::defaultCheck, false);
	_checkView->checkedChanges(
	) | rpl::on_next([=](bool checked) {
		setIcon(checked ? &st::mediaPlayerMenuCheck : nullptr);
	}, lifetime());

	_checkView->setChecked(checked, anim::type::normal);
	ItemBase::clicks() | rpl::on_next([=] {
		const auto checked = !_checkView->checked();
		if (_changeAllowed && !_changeAllowed(checked)) {
			shake();
			return;
		}
		_checkView->setChecked(checked, anim::type::normal);
	}, lifetime());
}

void ItemWithCheck::setChangeAllowed(Fn<bool(bool checked)> allowed) {
	_changeAllowed = std::move(allowed);
}

void ItemWithCheck::shake() {
	base::Platform::Haptic();
	if (_shakeAnimation.animating()) {
		return;
	}
	_shakeAnimation.start(Ui::DefaultShakeCallback([=](int shift) {
		_shakeShift = shift;
		update();
	}), 0., 1., st::shakeDuration);
}

void ItemWithCheck::paintEvent(QPaintEvent *e) {
	if (!_shakeShift) {
		Action::paintEvent(e);
		return;
	}
	auto p = Painter(this);
	const auto selected = isSelected();
	paintBackground(p, selected);
	paintRipple(p, 0, 0);
	p.translate(_shakeShift, 0);
	if (_checkView->checked()) {
		st::mediaPlayerMenuCheck.paint(p, st().itemIconPosition, width());
	}
	p.setPen(selected ? st().itemFgOver : st().itemFg);
	paintText(p);
}

not_null<Ui::CheckView*> ItemWithCheck::checkView() const {
	return _checkView.get();
}

} // namespace Menu
