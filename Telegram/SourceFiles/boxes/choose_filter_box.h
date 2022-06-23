/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

class History;

class ChooseFilterValidator final {
public:
	ChooseFilterValidator(not_null<History*> history);
	struct LimitData {
		const bool reached = false;
		const int count = 0;
	};

	[[nodiscard]] bool canAdd() const;
	[[nodiscard]] bool canRemove(FilterId filterId) const;
	[[nodiscard]] LimitData limitReached(FilterId filterId) const;

	void add(FilterId filterId) const;
	void remove(FilterId filterId) const;

private:
	const not_null<History*> _history;

};

void FillChooseFilterMenu(
	not_null<Window::SessionController*> controller,
	not_null<Ui::PopupMenu*> menu,
	not_null<History*> history);
