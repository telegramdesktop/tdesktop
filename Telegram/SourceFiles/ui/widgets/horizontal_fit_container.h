/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "base/unique_qptr.h"

namespace Ui {

class AbstractButton;

class HorizontalFitContainer final : public RpWidget {
public:
	HorizontalFitContainer(
		not_null<RpWidget*> parent,
		int spacing);

	int add(not_null<AbstractButton*> button);
	void remove(int id);

private:
	void updateLayout(QSize size);

	const int _spacing;
	std::map<int, base::unique_qptr<AbstractButton>> _buttons;
	int _nextId = 0;
	rpl::lifetime _layoutLifetime;

};

} // namespace Ui
