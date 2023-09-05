/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
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
	std::shared_ptr<Ui::Show> show);

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
