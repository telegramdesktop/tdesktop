/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "export/export_settings.h"
#include "ui/rp_widget.h"

namespace Export {
namespace View {

class SettingsWidget : public Ui::RpWidget {
public:
	SettingsWidget(QWidget *parent);

	rpl::producer<Settings> startClicks() const;
	rpl::producer<> cancelClicks() const;

private:
	using Type = Settings::Type;
	using Types = Settings::Types;
	using MediaType = MediaSettings::Type;
	using MediaTypes = MediaSettings::Types;

	void setupContent();
	void refreshButtons(not_null<Ui::RpWidget*> container);

	Settings _data;
	struct Wrap {
		Wrap(rpl::producer<> value = rpl::never<>())
		: value(std::move(value)) {
		}

		rpl::producer<> value;

	};
	rpl::variable<Wrap> _startClicks;
	rpl::variable<Wrap> _cancelClicks;

};

} // namespace View
} // namespace Export
