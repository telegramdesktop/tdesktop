/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"
#include "history/admin_log/history_admin_log_section.h"

namespace AdminLog {

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

} // namespace AdminLog
