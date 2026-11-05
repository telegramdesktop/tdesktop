/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "core/core_settings.h"

namespace Window {

class Controller;

class Columns final {
public:
	explicit Columns(not_null<Controller*> window);

	void updateDialogsWidthRatio(float64 ratio, bool nochat);
	[[nodiscard]] float64 dialogsWidthRatio(bool nochat) const;
	[[nodiscard]] auto dialogsWithChatWidthRatioChanges() const
		-> rpl::producer<float64>;
	[[nodiscard]] auto dialogsNoChatWidthRatioChanges() const
		-> rpl::producer<float64>;

	[[nodiscard]] bool tabbedSelectorSectionEnabled() const;
	void setTabbedSelectorSectionEnabled(bool enabled);
	[[nodiscard]] bool thirdSectionInfoEnabled() const;
	void setThirdSectionInfoEnabled(bool enabled);
	[[nodiscard]] rpl::producer<bool> thirdSectionInfoEnabledValue() const;

	[[nodiscard]] bool tabbedReplacedWithInfo() const;
	void setTabbedReplacedWithInfo(bool enabled);
	[[nodiscard]] rpl::producer<bool> tabbedReplacedWithInfoValue() const;

	[[nodiscard]] int thirdSectionExtendedBy() const;
	void setThirdSectionExtendedBy(int savedValue);

	void setThirdColumnWidth(int width);
	[[nodiscard]] int thirdColumnWidth() const;
	[[nodiscard]] rpl::producer<int> thirdColumnWidthChanges() const;

	[[nodiscard]] const Core::WindowColumns &state() const;
	bool applySaved(const Core::WindowColumns &state);

	void seedFromSettings();
	void seedFirstWindow();
	void writeToSettings() const;

private:
	void seed(const Core::WindowColumns &state);

	const not_null<Controller*> _window;
	Core::WindowColumns _state;
	bool _dialogsWidthSetToZeroWithoutChat = false;
	bool _tabbedReplacedWithInfo = false;
	rpl::event_stream<float64> _dialogsWithChatWidthRatioChanges;
	rpl::event_stream<float64> _dialogsNoChatWidthRatioChanges;
	rpl::event_stream<bool> _thirdSectionInfoEnabledValue;
	rpl::event_stream<bool> _tabbedReplacedWithInfoValue;
	rpl::event_stream<int> _thirdColumnWidthChanges;

};

} // namespace Window
