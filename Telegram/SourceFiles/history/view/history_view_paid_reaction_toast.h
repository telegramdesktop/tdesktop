/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "styles/style_widgets.h"

namespace Calls {
class GroupCall;
} // namespace Calls

namespace Data {
class Session;
} // namespace Data

namespace Ui {
//class Show;
class RpWidget;
} // namespace Ui

namespace Ui::Toast {
class Instance;
} // namespace Ui::Toast

namespace HistoryView {

class Element;

class PaidReactionToast final {
public:
	PaidReactionToast(
		not_null<Ui::RpWidget*> parent,
		not_null<Data::Session*> owner,
		rpl::producer<int> topOffset,
		Fn<bool(not_null<const Element*> view)> mine);
	PaidReactionToast(
		not_null<Ui::RpWidget*> parent,
		not_null<Data::Session*> owner,
		rpl::producer<int> topOffset,
		Fn<bool(not_null<Calls::GroupCall*> call)> mine);
	~PaidReactionToast();

	[[nodiscard]] rpl::producer<FullMsgId> shownForId() const;
	[[nodiscard]] rpl::producer<Calls::GroupCall*> shownForCall() const;

private:
	bool maybeShowFor(not_null<HistoryItem*> item);
	bool maybeShowFor(not_null<Calls::GroupCall*> call);
	void showFor(
		FullMsgId itemId,
		int count,
		PeerId shownPeer,
		crl::time left,
		crl::time total);

	[[nodiscard]] FullMsgId idForCall(not_null<Calls::GroupCall*> call);

	void setupLottiePreview(not_null<Ui::RpWidget*> widget, int size);
	void clearHiddenHiding();

	const not_null<Ui::RpWidget*> _parent;
	const not_null<Data::Session*> _owner;
	const rpl::variable<int> _topOffset;

	base::weak_ptr<Ui::Toast::Instance> _weak;
	std::vector<base::weak_ptr<Ui::Toast::Instance>> _hiding;
	rpl::variable<int> _count;
	rpl::variable<PeerId> _shownPeer;
	rpl::variable<crl::time> _timeFinish;

	std::vector<FullMsgId> _stack;

	base::flat_map<FullMsgId, base::weak_ptr<Calls::GroupCall>> _idsForCalls;

	rpl::variable<FullMsgId> _shownForId;

	rpl::lifetime _lifetime;

};

} // namespace HistoryView
