/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/generic_box.h"

namespace Api {
struct SendOptions;
} // namespace Api

enum class SendMenuType;

namespace HistoryView {

[[nodiscard]] TimeId DefaultScheduleTime();
void ScheduleBox(
	not_null<GenericBox*> box,
	SendMenuType type,
	FnMut<void(Api::SendOptions)> done,
	TimeId time);

template <typename Guard, typename Submit>
[[nodiscard]] object_ptr<GenericBox> PrepareScheduleBox(
		Guard &&guard,
		SendMenuType type,
		Submit &&submit) {
	return Box(
		ScheduleBox,
		type,
		crl::guard(std::forward<Guard>(guard), std::forward<Submit>(submit)),
		DefaultScheduleTime());
}

} // namespace HistoryView
