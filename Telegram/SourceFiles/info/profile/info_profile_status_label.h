/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"

namespace Ui {
class FlatLabel;
} // namespace Ui

namespace Info::Profile {

class StatusLabel final {
public:
	StatusLabel(
		not_null<Ui::FlatLabel*> label,
		not_null<PeerData*> peer);

	void refresh();
	void setMembersLinkCallback(Fn<void()> callback);
	[[nodiscard]] Fn<void()> membersLinkCallback() const;
	void setOnlineCount(int count);
	void setColorized(bool enabled);

private:
	const not_null<Ui::FlatLabel*> _label;
	const not_null<PeerData*> _peer;
	int _onlineCount = 0;
	bool _colorized = true;
	Fn<void()> _membersLinkCallback;
	base::Timer _refreshTimer;

};

} // namespace Info::Profile
