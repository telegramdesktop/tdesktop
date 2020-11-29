/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

namespace Ui {
struct GroupCallBarContent;
} // namespace Ui

namespace Data {
class GroupCall;
class CloudImageView;
} // namespace Data

namespace HistoryView {

struct UserpicInRow {
	not_null<PeerData*> peer;
	mutable std::shared_ptr<Data::CloudImageView> view;
	mutable InMemoryKey uniqueKey;
};

struct UserpicsInRowStyle {
	int size = 0;
	int shift = 0;
	int stroke = 0;
};

void GenerateUserpicsInRow(
	QImage &result,
	const std::vector<UserpicInRow> &list,
	const UserpicsInRowStyle &st,
	int maxElements = 0);

class GroupCallTracker final {
public:
	GroupCallTracker(not_null<ChannelData*> channel);

	[[nodiscard]] rpl::producer<Ui::GroupCallBarContent> content() const;
	[[nodiscard]] rpl::producer<> joinClicks() const;

	[[nodiscard]] static rpl::producer<Ui::GroupCallBarContent> ContentByCall(
		not_null<Data::GroupCall*> call,
		const UserpicsInRowStyle &st);

private:
	not_null<ChannelData*> _channel;

	rpl::event_stream<> _joinClicks;

};

} // namespace HistoryView
