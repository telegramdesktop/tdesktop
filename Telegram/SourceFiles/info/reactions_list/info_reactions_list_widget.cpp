/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/reactions_list/info_reactions_list_widget.h"

#include "api/api_who_reacted.h"
#include "boxes/peer_list_box.h"
#include "data/data_channel.h"
#include "history/view/reactions/history_view_reactions_list.h"
#include "history/view/reactions/history_view_reactions_tabs.h"
#include "info/info_controller.h"
#include "ui/controls/who_reacted_context_action.h"
#include "ui/widgets/scroll_area.h"
#include "ui/ui_utility.h"
#include "lang/lang_keys.h"
#include "styles/style_info.h"

namespace Info::ReactionsList {
namespace {

} // namespace

class InnerWidget final
	: public Ui::RpWidget
	, private PeerListContentDelegate {
public:
	InnerWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		std::shared_ptr<Api::WhoReadList> whoReadIds,
		FullMsgId contextId,
		Data::ReactionId selected);

	[[nodiscard]] std::shared_ptr<Api::WhoReadList> whoReadIds() const;
	[[nodiscard]] FullMsgId contextId() const;
	[[nodiscard]] Data::ReactionId selected() const;

	rpl::producer<Ui::ScrollToRequest> scrollToRequests() const;

	int desiredHeight() const;

	void saveState(not_null<Memento*> memento);
	void restoreState(not_null<Memento*> memento);

protected:
	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;

private:
	using ListWidget = PeerListContent;

	// PeerListContentDelegate interface
	void peerListSetTitle(rpl::producer<QString> title) override;
	void peerListSetAdditionalTitle(rpl::producer<QString> title) override;
	bool peerListIsRowChecked(not_null<PeerListRow*> row) override;
	int peerListSelectedRowsCount() override;
	void peerListScrollToTop() override;
	void peerListAddSelectedPeerInBunch(not_null<PeerData*> peer) override;
	void peerListAddSelectedRowInBunch(not_null<PeerListRow*> row) override;
	void peerListFinishSelectedRowsBunch() override;
	void peerListSetDescription(object_ptr<Ui::FlatLabel> description) override;
	std::shared_ptr<Main::SessionShow> peerListUiShow() override;

	object_ptr<ListWidget> setupList(
		RpWidget *parent,
		not_null<PeerListController*> controller);

	const std::shared_ptr<Main::SessionShow> _show;
	not_null<Controller*> _controller;
	Data::ReactionId _selected;
	not_null<HistoryView::Reactions::Tabs*> _tabs;
	rpl::variable<int> _tabsHeight;
	HistoryView::Reactions::PreparedFullList _full;
	object_ptr<ListWidget> _list;

	rpl::event_stream<Ui::ScrollToRequest> _scrollToRequests;
};

InnerWidget::InnerWidget(
	QWidget *parent,
	not_null<Controller*> controller,
	std::shared_ptr<Api::WhoReadList> whoReadIds,
	FullMsgId contextId,
	Data::ReactionId selected)
: RpWidget(parent)
, _show(controller->uiShow())
, _controller(controller)
, _selected(selected)
, _tabs(HistoryView::Reactions::CreateReactionsTabs(
	this,
	controller,
	controller->reactionsContextId(),
	_selected,
	controller->reactionsWhoReadIds()))
, _tabsHeight(_tabs->heightValue())
, _full(HistoryView::Reactions::FullListController(
	controller,
	controller->reactionsContextId(),
	_selected,
	controller->reactionsWhoReadIds()))
, _list(setupList(this, _full.controller.get())) {
	setContent(_list.data());
	_full.controller->setDelegate(static_cast<PeerListDelegate*>(this));
	_tabs->changes(
	) | rpl::start_with_next([=](Data::ReactionId reaction) {
		_selected = reaction;
		_full.switchTab(reaction);
	}, _list->lifetime());
}

std::shared_ptr<Api::WhoReadList> InnerWidget::whoReadIds() const {
	return _controller->reactionsWhoReadIds();
}

FullMsgId InnerWidget::contextId() const {
	return _controller->reactionsContextId();
}

Data::ReactionId InnerWidget::selected() const {
	return _selected;
}

void InnerWidget::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	setChildVisibleTopBottom(_list, visibleTop, visibleBottom);
}

void InnerWidget::saveState(not_null<Memento*> memento) {
	memento->setListState(_full.controller->saveState());
}

void InnerWidget::restoreState(not_null<Memento*> memento) {
	_full.controller->restoreState(memento->listState());
}

rpl::producer<Ui::ScrollToRequest> InnerWidget::scrollToRequests() const {
	return _scrollToRequests.events();
}

int InnerWidget::desiredHeight() const {
	auto desired = 0;
	desired += _list->fullRowsCount() * st::infoMembersList.item.height;
	return qMax(height(), desired);
}

object_ptr<InnerWidget::ListWidget> InnerWidget::setupList(
		RpWidget *parent,
		not_null<PeerListController*> controller) {
	auto result = object_ptr<ListWidget>(parent, controller);
	const auto raw = result.data();

	raw->scrollToRequests(
	) | rpl::start_with_next([this](Ui::ScrollToRequest request) {
		const auto skip = _tabsHeight.current()
			+ st::infoCommonGroupsMargin.top();
		auto addmin = (request.ymin < 0) ? 0 : skip;
		auto addmax = (request.ymax < 0) ? 0 : skip;
		_scrollToRequests.fire({
			request.ymin + addmin,
			request.ymax + addmax });
	}, raw->lifetime());

	_tabs->move(0, 0);
	_tabsHeight.value() | rpl::start_with_next([=](int tabs) {
		raw->moveToLeft(0, tabs + st::infoCommonGroupsMargin.top());
	}, raw->lifetime());

	parent->widthValue(
	) | rpl::start_with_next([=](int newWidth) {
		_tabs->resizeToWidth(newWidth);
		raw->resizeToWidth(newWidth);
	}, raw->lifetime());

	rpl::combine(
		_tabsHeight.value(),
		raw->heightValue()
	) | rpl::start_with_next([parent](int tabsHeight, int listHeight) {
		const auto newHeight = tabsHeight
			+ st::infoCommonGroupsMargin.top()
			+ listHeight
			+ st::infoCommonGroupsMargin.bottom();
		parent->resize(parent->width(), newHeight);
	}, result->lifetime());

	return result;
}

void InnerWidget::peerListSetTitle(rpl::producer<QString> title) {
}

void InnerWidget::peerListSetAdditionalTitle(rpl::producer<QString> title) {
}

bool InnerWidget::peerListIsRowChecked(not_null<PeerListRow*> row) {
	return false;
}

int InnerWidget::peerListSelectedRowsCount() {
	return 0;
}

void InnerWidget::peerListScrollToTop() {
	_scrollToRequests.fire({ -1, -1 });
}

void InnerWidget::peerListAddSelectedPeerInBunch(not_null<PeerData*> peer) {
	Unexpected("Item selection in Info::Profile::Members.");
}

void InnerWidget::peerListAddSelectedRowInBunch(not_null<PeerListRow*> row) {
	Unexpected("Item selection in Info::Profile::Members.");
}

void InnerWidget::peerListFinishSelectedRowsBunch() {
}

void InnerWidget::peerListSetDescription(
		object_ptr<Ui::FlatLabel> description) {
	description.destroy();
}

std::shared_ptr<Main::SessionShow> InnerWidget::peerListUiShow() {
	return _show;
}

Memento::Memento(
	std::shared_ptr<Api::WhoReadList> whoReadIds,
	FullMsgId contextId,
	Data::ReactionId selected)
: ContentMemento(std::move(whoReadIds), contextId, selected) {
}

Section Memento::section() const {
	return Section(Section::Type::ReactionsList);
}

std::shared_ptr<Api::WhoReadList> Memento::whoReadIds() const {
	return reactionsWhoReadIds();
}

FullMsgId Memento::contextId() const {
	return reactionsContextId();
}

Data::ReactionId Memento::selected() const {
	return reactionsSelected();
}

object_ptr<ContentWidget> Memento::createWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		const QRect &geometry) {
	auto result = object_ptr<Widget>(
		parent,
		controller,
		whoReadIds(),
		contextId(),
		selected());
	result->setInternalState(geometry, this);
	return result;
}

void Memento::setListState(std::unique_ptr<PeerListState> state) {
	_listState = std::move(state);
}

std::unique_ptr<PeerListState> Memento::listState() {
	return std::move(_listState);
}

Memento::~Memento() = default;

Widget::Widget(
	QWidget *parent,
	not_null<Controller*> controller,
	std::shared_ptr<Api::WhoReadList> whoReadIds,
	FullMsgId contextId,
	Data::ReactionId selected)
: ContentWidget(parent, controller) {
	_inner = setInnerWidget(object_ptr<InnerWidget>(
		this,
		controller,
		std::move(whoReadIds),
		contextId,
		selected));
}

rpl::producer<QString> Widget::title() {
	const auto ids = whoReadIds();
	const auto count = ids ? int(ids->list.size()) : 0;
	return !count
		? tr::lng_manage_peer_reactions()
		: (ids->type == Ui::WhoReadType::Seen)
		? tr::lng_context_seen_text(lt_count, rpl::single(1. * count))
		: (ids->type == Ui::WhoReadType::Listened)
		? tr::lng_context_seen_listened(lt_count, rpl::single(1. * count))
		: (ids->type == Ui::WhoReadType::Watched)
		? tr::lng_context_seen_watched(lt_count, rpl::single(1. * count))
		: tr::lng_manage_peer_reactions();
}

std::shared_ptr<Api::WhoReadList> Widget::whoReadIds() const {
	return _inner->whoReadIds();
}

FullMsgId Widget::contextId() const {
	return _inner->contextId();
}

Data::ReactionId Widget::selected() const {
	return _inner->selected();
}

bool Widget::showInternal(not_null<ContentMemento*> memento) {
	return false;
}

void Widget::setInternalState(
		const QRect &geometry,
		not_null<Memento*> memento) {
	setGeometry(geometry);
	Ui::SendPendingMoveResizeEvents(this);
	restoreState(memento);
}

std::shared_ptr<ContentMemento> Widget::doCreateMemento() {
	auto result = std::make_shared<Memento>(
		whoReadIds(),
		contextId(),
		selected());
	saveState(result.get());
	return result;
}

void Widget::saveState(not_null<Memento*> memento) {
	memento->setScrollTop(scrollTopSave());
	_inner->saveState(memento);
}

void Widget::restoreState(not_null<Memento*> memento) {
	_inner->restoreState(memento);
	scrollTopRestore(memento->scrollTop());
}

} // namespace Info::ReactionsList
