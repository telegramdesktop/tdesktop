/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class RpWidget;
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
	[[nodiscard]] bool canAdd(FilterId filterId) const;
	[[nodiscard]] bool canRemove(FilterId filterId) const;
	[[nodiscard]] LimitData limitReached(
		FilterId filterId,
		bool always) const;

	void add(FilterId filterId) const;
	void remove(FilterId filterId) const;

private:
	const not_null<History*> _history;

};

void FillChooseFilterMenu(
	not_null<Window::SessionController*> controller,
	not_null<Ui::PopupMenu*> menu,
	not_null<History*> history);

bool FillChooseFilterWithAdminedGroupsMenu(
	not_null<Window::SessionController*> controller,
	not_null<Ui::PopupMenu*> menu,
	not_null<UserData*> user,
	std::shared_ptr<rpl::event_stream<>> listUpdates,
	std::vector<not_null<PeerData*>> common,
	std::shared_ptr<std::vector<PeerId>> collectCommon);

void SetupFilterDragAndDrop(
	not_null<Ui::RpWidget*> outer,
	not_null<Main::Session*> session,
	Fn<std::optional<FilterId>(QPoint)> filterIdAtPosition,
	Fn<FilterId()> activeFilterId);
