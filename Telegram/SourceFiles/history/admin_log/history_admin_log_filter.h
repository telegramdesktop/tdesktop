/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/admin_log/history_admin_log_filter_value.h"
#include "ui/layers/box_content.h"

template <typename Flags>
struct EditFlagsDescriptor;

namespace Ui {
class VerticalLayout;
} // namespace Ui

namespace AdminLog {

struct FilterValue;

[[nodiscard]] Fn<FilterValue::Flags()> FillFilterValueList(
	not_null<Ui::VerticalLayout*> container,
	bool isChannel,
	const FilterValue &filter);

EditFlagsDescriptor<FilterValue::Flags> FilterValueLabels(bool isChannel);

} // namespace AdminLog
