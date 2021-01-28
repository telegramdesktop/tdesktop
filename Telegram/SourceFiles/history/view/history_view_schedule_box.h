/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/layers/generic_box.h"

namespace Api {
struct SendOptions;
} // namespace Api

namespace SendMenu {
enum class Type;
} // namespace SendMenu

namespace HistoryView {

[[nodiscard]] TimeId DefaultScheduleTime();
[[nodiscard]] bool CanScheduleUntilOnline(not_null<PeerData*> peer);

void ScheduleBox(
	not_null<Ui::GenericBox*> box,
	SendMenu::Type type,
	Fn<void(Api::SendOptions)> done,
	TimeId time);

template <typename Guard, typename Submit>
[[nodiscard]] object_ptr<Ui::GenericBox> PrepareScheduleBox(
		Guard &&guard,
		SendMenu::Type type,
		Submit &&submit,
		TimeId scheduleTime = DefaultScheduleTime()) {
	return Box(
		ScheduleBox,
		type,
		crl::guard(std::forward<Guard>(guard), std::forward<Submit>(submit)),
		scheduleTime);
}

} // namespace HistoryView
