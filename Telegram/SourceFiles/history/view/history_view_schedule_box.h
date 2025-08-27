/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "api/api_common.h"
#include "ui/boxes/choose_date_time.h"

namespace style {
struct IconButton;
struct PopupMenu;
} // namespace style

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace SendMenu {
struct Details;
} // namespace SendMenu

namespace HistoryView {

[[nodiscard]] TimeId DefaultScheduleTime();
[[nodiscard]] bool CanScheduleUntilOnline(not_null<PeerData*> peer);

struct ScheduleBoxStyleArgs {
	ScheduleBoxStyleArgs();
	const style::IconButton *topButtonStyle;
	const style::PopupMenu *popupMenuStyle;
	Ui::ChooseDateTimeStyleArgs chooseDateTimeArgs;
};

void ScheduleBox(
	not_null<Ui::GenericBox*> box,
	std::shared_ptr<ChatHelpers::Show> show,
	const Api::SendOptions &initialOptions,
	const SendMenu::Details &details,
	Fn<void(Api::SendOptions)> done,
	TimeId time,
	ScheduleBoxStyleArgs style);

template <typename Guard, typename Submit>
[[nodiscard]] object_ptr<Ui::GenericBox> PrepareScheduleBox(
		Guard &&guard,
		std::shared_ptr<ChatHelpers::Show> show,
		const SendMenu::Details &details,
		Submit &&submit,
		const Api::SendOptions &initialOptions = {},
		TimeId scheduleTime = DefaultScheduleTime(),
		ScheduleBoxStyleArgs style = ScheduleBoxStyleArgs()) {
	return Box(
		ScheduleBox,
		std::move(show),
		initialOptions,
		details,
		crl::guard(std::forward<Guard>(guard), std::forward<Submit>(submit)),
		scheduleTime,
		std::move(style));
}

} // namespace HistoryView
