/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "ui/userpic_view.h"

namespace Ui {
struct GroupCallBarContent;
} // namespace Ui

namespace Data {
class GroupCall;
} // namespace Data

namespace style {
struct GroupCallUserpics;
} // namespace style

namespace HistoryView {

struct UserpicInRow {
	not_null<PeerData*> peer;
	bool speaking = false;
	mutable Ui::PeerUserpicView view;
	mutable InMemoryKey uniqueKey;
};

void GenerateUserpicsInRow(
	QImage &result,
	const std::vector<UserpicInRow> &list,
	const style::GroupCallUserpics &st,
	int maxElements = 0);

[[nodiscard]] bool NeedRegenerateUserpics(
	const QImage &image,
	const std::vector<UserpicInRow> &list);

struct PreparedUserpicsInRow {
	QImage image;
	int width = 0;
};

[[nodiscard]] PreparedUserpicsInRow PrepareUserpicsInRow(
	const std::vector<not_null<PeerData*>> &peers,
	const style::GroupCallUserpics &st,
	int limit = 0);

[[nodiscard]] auto GroupCallBarContentByCall(
	not_null<Data::GroupCall*> call,
	int userpicSize)
-> rpl::producer<Ui::GroupCallBarContent>;

[[nodiscard]] auto GroupCallBarContentByPeer(
	not_null<PeerData*> peer,
	int userpicSize,
	bool showInForum)
-> rpl::producer<Ui::GroupCallBarContent>;

} // namespace HistoryView
