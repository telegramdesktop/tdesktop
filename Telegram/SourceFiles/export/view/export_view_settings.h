/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "export/export_settings.h"
#include "ui/rp_widget.h"

namespace Ui {
class VerticalLayout;
} // namespace Ui

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
	using Format = Output::Format;

	void setupContent();
	void chooseFolder();
	void refreshButtons(not_null<Ui::RpWidget*> container);
	void createSizeSlider(not_null<Ui::VerticalLayout*> container);

	Settings _data;
	struct Wrap {
		Wrap(rpl::producer<> value = rpl::never<>())
		: value(std::move(value)) {
		}

		rpl::producer<> value;

	};
	rpl::event_stream<Settings> _startClicks;
	rpl::variable<Wrap> _cancelClicks;
	rpl::event_stream<Settings::Types> _dataTypesChanges;

};

} // namespace View
} // namespace Export
