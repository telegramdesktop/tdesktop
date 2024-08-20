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

namespace AdminLog {

struct FilterValue;

class FilterBox : public Ui::BoxContent {
public:
	FilterBox(
		QWidget*,
		not_null<ChannelData*> channel,
		const std::vector<not_null<UserData*>> &admins,
		const FilterValue &filter,
		Fn<void(FilterValue &&filter)> saveCallback);

protected:
	void prepare() override;

private:
	void resizeToContent();
	void refreshButtons();

	not_null<ChannelData*> _channel;
	std::vector<not_null<UserData*>> _admins;
	FilterValue _initialFilter;
	Fn<void(FilterValue &&filter)> _saveCallback;

	class Inner;
	QPointer<Inner> _inner;

};

EditFlagsDescriptor<FilterValue::Flags> FilterValueLabels(bool isChannel);

} // namespace AdminLog
