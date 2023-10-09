/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class PeerData;

namespace Ui {
class VerticalLayout;
} // namespace Ui

namespace Api {
class MessageStatistics;
} // namespace Api

namespace Info::Statistics {

void AddPublicForwards(
	const Api::MessageStatistics &firstSliceHolder,
	not_null<Ui::VerticalLayout*> container,
	Fn<void(FullMsgId)> showPeerHistory,
	not_null<PeerData*> peer,
	FullMsgId contextId);

} // namespace Info::Statistics
