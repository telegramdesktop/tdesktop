/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "export/export_settings.h"
#include "ui/rp_widget.h"
#include "base/object_ptr.h"

namespace Ui {
class VerticalLayout;
class Checkbox;
class ScrollArea;
class BoxContent;
} // namespace Ui

namespace Main {
class Session;
} // namespace Main

namespace Export {
namespace View {

constexpr auto kSizeValueCount = 90;
int SizeLimitByIndex(int index);

class SettingsWidget : public Ui::RpWidget {
public:
	SettingsWidget(
		QWidget *parent,
		not_null<Main::Session*> session,
		Settings data);

	rpl::producer<Settings> value() const;
	rpl::producer<Settings> changes() const;
	rpl::producer<> startClicks() const;
	rpl::producer<> cancelClicks() const;

	void setShowBoxCallback(Fn<void(object_ptr<Ui::BoxContent>)> callback) {
		_showBoxCallback = std::move(callback);
	}

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
	void setupFullExportOptions(not_null<Ui::VerticalLayout*> container);
	void setupMediaOptions(not_null<Ui::VerticalLayout*> container);
	void setupOtherOptions(not_null<Ui::VerticalLayout*> container);
	void setupPathAndFormat(not_null<Ui::VerticalLayout*> container);
	void addHeader(
		not_null<Ui::VerticalLayout*> container,
		const QString &text);
	not_null<Ui::Checkbox*> addOption(
		not_null<Ui::VerticalLayout*> container,
		const QString &text,
		Types types);
	not_null<Ui::Checkbox*> addOptionWithAbout(
		not_null<Ui::VerticalLayout*> container,
		const QString &text,
		Types types,
		const QString &about);
	void addChatOption(
		not_null<Ui::VerticalLayout*> container,
		const QString &text,
		Types types);
	void addMediaOptions(not_null<Ui::VerticalLayout*> container);
	void addMediaOption(
		not_null<Ui::VerticalLayout*> container,
		const QString &text,
		MediaType type);
	void addSizeSlider(not_null<Ui::VerticalLayout*> container);
	void addLocationLabel(
		not_null<Ui::VerticalLayout*> container);
	void addFormatAndLocationLabel(
		not_null<Ui::VerticalLayout*> container);
	void addLimitsLabel(
		not_null<Ui::VerticalLayout*> container);
	void chooseFolder();
	void chooseFormat();
	void refreshButtons(
		not_null<Ui::RpWidget*> container,
		bool canStart);

	void editDateLimit(
		TimeId current,
		TimeId min,
		TimeId max,
		rpl::producer<QString> resetLabel,
		Fn<void(TimeId)> done);

	const Settings &readData() const;
	template <typename Callback>
	void changeData(Callback &&callback);

	const not_null<Main::Session*> _session;
	PeerId _singlePeerId = 0;
	Fn<void(object_ptr<Ui::BoxContent>)> _showBoxCallback;

	// Use through readData / changeData wrappers.
	Settings _internal_data;

	struct Wrap {
		Wrap(rpl::producer<> value = nullptr)
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
