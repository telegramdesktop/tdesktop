/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "export/export_settings.h"
#include "ui/rp_widget.h"

enum LangKey : int;

namespace Ui {
class VerticalLayout;
class Checkbox;
class ScrollArea;
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
	not_null<Ui::RpWidget*> setupButtons(
		not_null<Ui::ScrollArea*> scroll,
		not_null<Ui::RpWidget*> wrap);
	void setupOptions(not_null<Ui::VerticalLayout*> container);
	void setupMediaOptions(not_null<Ui::VerticalLayout*> container);
	void setupPathAndFormat(not_null<Ui::VerticalLayout*> container);
	void addHeader(
		not_null<Ui::VerticalLayout*> container,
		LangKey key);
	not_null<Ui::Checkbox*> addOption(
		not_null<Ui::VerticalLayout*> container,
		LangKey key,
		Types types);
	void addChatOption(
		not_null<Ui::VerticalLayout*> container,
		LangKey key,
		Types types);
	void addMediaOption(
		not_null<Ui::VerticalLayout*> container,
		LangKey key,
		MediaType type);
	void addSizeSlider(not_null<Ui::VerticalLayout*> container);
	void addLocationLabel(
		not_null<Ui::VerticalLayout*> container);
	void chooseFolder();
	void refreshButtons(not_null<Ui::RpWidget*> container);

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
	rpl::event_stream<> _refreshButtons;
	rpl::event_stream<QString> _locationChanges;

};

} // namespace View
} // namespace Export
