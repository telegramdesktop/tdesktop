/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_columns.h"

#include "core/application.h"

namespace Window {

Columns::Columns(not_null<Controller*> window)
: _window(window)
, _state(Core::App().settings().windowColumns()) {
	_state.tabbedSelectorSectionEnabled = false;
	_state.thirdSectionInfoEnabled = false;
	_state.thirdSectionExtendedBy = 0;
}

void Columns::updateDialogsWidthRatio(float64 ratio, bool nochat) {
	const auto withChat = _state.dialogsWithChatWidthRatio;
	const auto changeWithChat = !nochat
		|| (withChat > 0)
		|| _dialogsWidthSetToZeroWithoutChat;
	const auto changedWithChat = changeWithChat && (withChat != ratio);

	const auto changeNoChat = nochat || (withChat != ratio);
	const auto changedNoChat = changeNoChat
		&& (_state.dialogsNoChatWidthRatio != ratio);

	if (!changedWithChat && !changedNoChat) {
		return;
	}
	if (changedWithChat) {
		_dialogsWidthSetToZeroWithoutChat = nochat && !(ratio > 0);
		_state.dialogsWithChatWidthRatio = ratio;
	}
	if (changedNoChat) {
		_state.dialogsNoChatWidthRatio = ratio;
	}
	writeToSettings();
	if (changedWithChat) {
		_dialogsWithChatWidthRatioChanges.fire_copy(ratio);
	}
	if (changedNoChat) {
		_dialogsNoChatWidthRatioChanges.fire_copy(ratio);
	}
}

float64 Columns::dialogsWidthRatio(bool nochat) const {
	const auto withChat = _state.dialogsWithChatWidthRatio;
	return (!nochat || withChat > 0)
		? withChat
		: _state.dialogsNoChatWidthRatio;
}

rpl::producer<float64> Columns::dialogsWithChatWidthRatioChanges() const {
	return _dialogsWithChatWidthRatioChanges.events();
}

rpl::producer<float64> Columns::dialogsNoChatWidthRatioChanges() const {
	return _dialogsNoChatWidthRatioChanges.events();
}

bool Columns::tabbedSelectorSectionEnabled() const {
	return _state.tabbedSelectorSectionEnabled;
}

void Columns::setTabbedSelectorSectionEnabled(bool enabled) {
	const auto infoChanged = enabled && _state.thirdSectionInfoEnabled;
	if (infoChanged || _state.tabbedSelectorSectionEnabled != enabled) {
		_state.tabbedSelectorSectionEnabled = enabled;
		if (infoChanged) {
			_state.thirdSectionInfoEnabled = false;
		}
		writeToSettings();
	}
	setTabbedReplacedWithInfo(false);
	if (infoChanged) {
		_thirdSectionInfoEnabledValue.fire_copy(false);
	}
}

bool Columns::thirdSectionInfoEnabled() const {
	return _state.thirdSectionInfoEnabled;
}

void Columns::setThirdSectionInfoEnabled(bool enabled) {
	if (_state.thirdSectionInfoEnabled != enabled) {
		_state.thirdSectionInfoEnabled = enabled;
		if (enabled) {
			_state.tabbedSelectorSectionEnabled = false;
		}
		writeToSettings();
		setTabbedReplacedWithInfo(false);
		_thirdSectionInfoEnabledValue.fire_copy(enabled);
	}
}

rpl::producer<bool> Columns::thirdSectionInfoEnabledValue() const {
	return _thirdSectionInfoEnabledValue.events_starting_with(
		thirdSectionInfoEnabled());
}

bool Columns::tabbedReplacedWithInfo() const {
	return _tabbedReplacedWithInfo;
}

void Columns::setTabbedReplacedWithInfo(bool enabled) {
	if (_tabbedReplacedWithInfo != enabled) {
		_tabbedReplacedWithInfo = enabled;
		_tabbedReplacedWithInfoValue.fire_copy(enabled);
	}
}

rpl::producer<bool> Columns::tabbedReplacedWithInfoValue() const {
	return _tabbedReplacedWithInfoValue.events_starting_with(
		tabbedReplacedWithInfo());
}

int Columns::thirdSectionExtendedBy() const {
	return _state.thirdSectionExtendedBy;
}

void Columns::setThirdSectionExtendedBy(int savedValue) {
	if (_state.thirdSectionExtendedBy != savedValue) {
		_state.thirdSectionExtendedBy = savedValue;
		writeToSettings();
	}
}

void Columns::setThirdColumnWidth(int width) {
	if (_state.thirdColumnWidth != width) {
		_state.thirdColumnWidth = width;
		writeToSettings();
		_thirdColumnWidthChanges.fire_copy(width);
	}
}

int Columns::thirdColumnWidth() const {
	return _state.thirdColumnWidth;
}

rpl::producer<int> Columns::thirdColumnWidthChanges() const {
	return _thirdColumnWidthChanges.events();
}

void Columns::seedFromSettings() {
	_state = Core::App().settings().windowColumns();
	_dialogsWidthSetToZeroWithoutChat = false;
	_tabbedReplacedWithInfo = false;
}

void Columns::writeToSettings() const {
	if (Core::App().isWindowInSettings(_window)) {
		Core::App().settings().setWindowColumns(_state);
	}
}

} // namespace Window
