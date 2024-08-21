/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class PeerData;

using Participants = std::vector<not_null<PeerData*>>;

namespace Ui {

class Checkbox;
class VerticalLayout;

struct ExpandablePeerListController final {
	struct Data final {
		Participants participants;
		std::vector<PeerId> checked;
		bool skipSingle = false;
		bool hideRightButton = false;
		bool checkTopOnAllInner = false;
		bool bold = true;
	};
	ExpandablePeerListController(Data &&data) : data(std::move(data)) {
	}
	const Data data;
	rpl::event_stream<bool> toggleRequestsFromTop;
	rpl::event_stream<bool> toggleRequestsFromInner;
	rpl::event_stream<bool> checkAllRequests;
	Fn<Participants()> collectRequests;
};

void AddExpandablePeerList(
	not_null<Ui::Checkbox*> checkbox,
	not_null<ExpandablePeerListController*> controller,
	not_null<Ui::VerticalLayout*> inner);

} // namespace Ui
