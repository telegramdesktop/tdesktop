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

namespace style {
struct GroupCallUserpics;
} // namespace style

namespace HistoryView {

struct UserpicInRow {
	not_null<PeerData*> peer;
	bool speaking = false;
	mutable std::shared_ptr<Data::CloudImageView> view;
	mutable InMemoryKey uniqueKey;
};

void GenerateUserpicsInRow(
	QImage &result,
	const std::vector<UserpicInRow> &list,
	const style::GroupCallUserpics &st,
	int maxElements = 0);

[[nodiscard]] auto GroupCallBarContentByCall(
	not_null<Data::GroupCall*> call,
	int userpicSize)
-> rpl::producer<Ui::GroupCallBarContent>;

[[nodiscard]] auto GroupCallBarContentByPeer(
	not_null<PeerData*> peer,
	int userpicSize)
-> rpl::producer<Ui::GroupCallBarContent>;

} // namespace HistoryView
