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
	SettingsWidget(QWidget *parent, Settings data);

	rpl::producer<Settings> value() const;
	rpl::producer<Settings> changes() const;
	rpl::producer<> startClicks() const;
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
	not_null<Ui::Checkbox*> addOptionWithAbout(
		not_null<Ui::VerticalLayout*> container,
		LangKey key,
		Types types,
		LangKey about);
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
	void refreshButtons(
		not_null<Ui::RpWidget*> container,
		bool canStart);

	const Settings &readData() const;
	template <typename Callback>
	void changeData(Callback &&callback);

	// Use through readData / changeData wrappers.
	Settings _internal_data;

	struct Wrap {
		Wrap(rpl::producer<> value = rpl::never<>())
		: value(std::move(value)) {
		}

		rpl::producer<> value;
	};
	rpl::event_stream<Settings> _changes;
	rpl::variable<Wrap> _startClicks;
	rpl::variable<Wrap> _cancelClicks;

};

} // namespace View
} // namespace Export
