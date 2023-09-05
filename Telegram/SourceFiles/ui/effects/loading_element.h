/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

template <typename Object>
class object_ptr;

namespace style {
struct FlatLabel;
struct PeerListItem;
} // namespace style

namespace Ui {

class RpWidget;

object_ptr<Ui::RpWidget> CreateLoadingTextWidget(
	not_null<Ui::RpWidget*> parent,
	const style::FlatLabel &st,
	int lines,
	rpl::producer<bool> rtl);

object_ptr<Ui::RpWidget> CreateLoadingPeerListItemWidget(
	not_null<Ui::RpWidget*> parent,
	const style::PeerListItem &st,
	int lines);

} // namespace Ui
