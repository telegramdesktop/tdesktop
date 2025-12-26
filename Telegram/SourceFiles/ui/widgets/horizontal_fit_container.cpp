/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/widgets/horizontal_fit_container.h"

#include "ui/widgets/buttons.h"

namespace Ui {

HorizontalFitContainer::HorizontalFitContainer(
	not_null<RpWidget*> parent,
	int spacing)
: RpWidget(parent)
, _spacing(spacing) {
	sizeValue() | rpl::on_next([=](QSize size) {
		updateLayout(size);
	}, lifetime());
}

int HorizontalFitContainer::add(not_null<AbstractButton*> button) {
	const auto id = _nextId++;
	_buttons.emplace(id, base::unique_qptr<AbstractButton>{ button.get() });
	button->setParent(this);
	button->show();
	updateLayout(size());
	return id;
}

void HorizontalFitContainer::remove(int id) {
	if (const auto it = _buttons.find(id); it != _buttons.end()) {
		_buttons.erase(it);
		updateLayout(size());
	}
}

void HorizontalFitContainer::updateLayout(QSize size) {
	_layoutLifetime.destroy();
	if (_buttons.empty()) {
		return;
	}
	const auto count = int(_buttons.size());
	const auto totalSpacing = _spacing * (count - 1);
	const auto buttonWidth = (size.width() - totalSpacing) / count;
	const auto h = size.height();
	auto x = 0;
	for (const auto &[id, button] : _buttons) {
		button->setGeometry(x, 0, buttonWidth, h);
		x += buttonWidth + _spacing;
	}
}

} // namespace Ui
