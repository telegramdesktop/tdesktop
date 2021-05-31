/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Calls {
class GroupCall;
} // namespace Calls

namespace Calls::Group {

class Panel;

class Toasts final {
public:
	explicit Toasts(not_null<Panel*> panel);

private:
	void setup();
	void setupJoinAsChanged();
	void setupTitleChanged();
	void setupRequestedToSpeak();
	void setupAllowedToSpeak();
	void setupPinnedVideo();
	void setupError();

	const not_null<Panel*> _panel;
	const not_null<GroupCall*> _call;

	rpl::lifetime _lifetime;

};

} // namespace Calls::Group
