/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/layers/generic_box.h"

namespace Webrtc {
class AudioInputTester;
} // namespace Webrtc

namespace Calls {
class GroupCall;
} // namespace Calls

namespace Calls::Group {

void SettingsBox(
	not_null<Ui::GenericBox*> box,
	not_null<GroupCall*> call);

[[nodiscard]] std::pair<Fn<void()>, rpl::lifetime> ShareInviteLinkAction(
	not_null<PeerData*> peer,
	Fn<void(object_ptr<Ui::BoxContent>)> showBox,
	Fn<void(QString)> showToast);

class MicLevelTester final {
public:
	explicit MicLevelTester(Fn<void()> show);

	[[nodiscard]] bool showTooltip() const;

private:
	void check();

	Fn<void()> _show;
	base::Timer _timer;
	std::unique_ptr<Webrtc::AudioInputTester> _tester;
	int _loudCount = 0;
	int _quietCount = 0;

};

} // namespace Calls::Group
