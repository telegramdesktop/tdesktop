/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "boxes/abstract_box.h"
#include "history/history_admin_log_section.h"

namespace AdminLog {

class FilterBox : public BoxContent {
public:
	FilterBox(QWidget*, not_null<ChannelData*> channel, const std::vector<not_null<UserData*>> &admins, const FilterValue &filter, base::lambda<void(FilterValue &&filter)> saveCallback);

protected:
	void prepare() override;

private:
	void resizeToContent();
	void refreshButtons();

	not_null<ChannelData*> _channel;
	std::vector<not_null<UserData*>> _admins;
	FilterValue _initialFilter;
	base::lambda<void(FilterValue &&filter)> _saveCallback;

	class Inner;
	QPointer<Inner> _inner;

};

} // namespace AdminLog
