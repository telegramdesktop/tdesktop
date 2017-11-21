/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "info/common_groups/info_common_groups_inner_widget.h"

#include "info/common_groups/info_common_groups_widget.h"
#include "info/info_controller.h"
#include "lang/lang_keys.h"
#include "styles/style_info.h"
#include "styles/style_widgets.h"
#include "mtproto/sender.h"
#include "window/window_controller.h"
#include "ui/widgets/scroll_area.h"
#include "ui/search_field_controller.h"
#include "apiwrap.h"

namespace Info {
namespace CommonGroups {
namespace {

constexpr auto kCommonGroupsPerPage = 40;
constexpr auto kCommonGroupsSearchAfter = 20;

class ListController
	: public PeerListController
	, private base::Subscriber
	, private MTP::Sender {
public:
	ListController(
		not_null<Controller*> controller,
		not_null<UserData*> user);

	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	void loadMoreRows() override;

	std::unique_ptr<PeerListRow> createRestoredRow(
			not_null<PeerData*> peer) override {
		return createRow(peer);
	}

	std::unique_ptr<PeerListState> saveState() override;
	void restoreState(std::unique_ptr<PeerListState> state) override;

private:
	std::unique_ptr<PeerListRow> createRow(not_null<PeerData*> peer);

	struct SavedState : SavedStateBase {
		int32 preloadGroupId = 0;
		bool allLoaded = false;
		bool wasLoading = false;
	};
	const not_null<Controller*> _controller;
	not_null<UserData*> _user;
	mtpRequestId _preloadRequestId = 0;
	bool _allLoaded = false;
	int32 _preloadGroupId = 0;

};

ListController::ListController(
	not_null<Controller*> controller,
	not_null<UserData*> user)
: PeerListController()
, _controller(controller)
, _user(user) {
	_controller->setSearchEnabledByContent(false);
}

std::unique_ptr<PeerListRow> ListController::createRow(
		not_null<PeerData*> peer) {
	auto result = std::make_unique<PeerListRow>(peer);
	result->setCustomStatus(QString());
	return result;
}

void ListController::prepare() {
	setSearchNoResultsText(lang(lng_bot_groups_not_found));
	delegate()->peerListSetSearchMode(PeerListSearchMode::Enabled);
	delegate()->peerListSetTitle(langFactory(lng_profile_common_groups_section));
}

void ListController::loadMoreRows() {
	if (_preloadRequestId || _allLoaded) {
		return;
	}
	_preloadRequestId = request(MTPmessages_GetCommonChats(
		_user->inputUser,
		MTP_int(_preloadGroupId),
		MTP_int(kCommonGroupsPerPage)
	)).done([this](const MTPmessages_Chats &result) {
		_preloadRequestId = 0;
		_preloadGroupId = 0;
		_allLoaded = true;
		if (auto chats = Api::getChatsFromMessagesChats(result)) {
			auto &list = chats->v;
			if (!list.empty()) {
				for_const (auto &chatData, list) {
					if (auto chat = App::feedChat(chatData)) {
						if (!chat->migrateTo()) {
							delegate()->peerListAppendRow(
								createRow(chat));
						}
						_preloadGroupId = chat->bareId();
						_allLoaded = false;
					}
				}
				delegate()->peerListRefreshRows();
			}
		}
		auto fullCount = delegate()->peerListFullRowsCount();
		if (fullCount > kCommonGroupsSearchAfter) {
			_controller->setSearchEnabledByContent(true);
		}
	}).send();
}

std::unique_ptr<PeerListState> ListController::saveState() {
	auto result = PeerListController::saveState();
	auto my = std::make_unique<SavedState>();
	my->preloadGroupId = _preloadGroupId;
	my->allLoaded = _allLoaded;
	if (auto requestId = base::take(_preloadRequestId)) {
		request(requestId).cancel();
		my->wasLoading = true;
	}
	result->controllerState = std::move(my);
	return result;
}

void ListController::restoreState(
		std::unique_ptr<PeerListState> state) {
	auto typeErasedState = state
		? state->controllerState.get()
		: nullptr;
	if (auto my = dynamic_cast<SavedState*>(typeErasedState)) {
		if (auto requestId = base::take(_preloadRequestId)) {
			request(requestId).cancel();
		}
		_allLoaded = my->allLoaded;
		_preloadGroupId = my->preloadGroupId;
		if (my->wasLoading) {
			loadMoreRows();
		}
		PeerListController::restoreState(std::move(state));
		auto fullCount = delegate()->peerListFullRowsCount();
		if (fullCount > kCommonGroupsSearchAfter) {
			_controller->setSearchEnabledByContent(true);
		}
	}
}

void ListController::rowClicked(not_null<PeerListRow*> row) {
	_controller->window()->showPeerHistory(
		row->peer(),
		Window::SectionShow::Way::Forward);
}

} // namespace

InnerWidget::InnerWidget(
	QWidget *parent,
	not_null<Controller*> controller,
	not_null<UserData*> user)
: RpWidget(parent)
, _controller(controller)
, _user(user)
, _listController(std::make_unique<ListController>(controller, _user))
, _list(setupList(this, _listController.get())) {
	setContent(_list.data());
	_listController->setDelegate(static_cast<PeerListDelegate*>(this));

	_controller->searchFieldController()->queryValue()
		| rpl::start_with_next([this](QString &&query) {
			peerListScrollToTop();
			content()->searchQueryChanged(std::move(query));
		}, lifetime());
}

void InnerWidget::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	setChildVisibleTopBottom(_list, visibleTop, visibleBottom);
}

void InnerWidget::saveState(not_null<Memento*> memento) {
	memento->setListState(_listController->saveState());
}

void InnerWidget::restoreState(not_null<Memento*> memento) {
	_listController->restoreState(memento->listState());
}

rpl::producer<Ui::ScrollToRequest> InnerWidget::scrollToRequests() const {
	return _scrollToRequests.events();
}

int InnerWidget::desiredHeight() const {
	auto desired = 0;
	auto count = qMax(_user->commonChatsCount(), 1);
	desired += qMax(count, _list->fullRowsCount())
		* st::infoCommonGroupsList.item.height;
	return qMax(height(), desired);
}

object_ptr<InnerWidget::ListWidget> InnerWidget::setupList(
		RpWidget *parent,
		not_null<PeerListController*> controller) const {
	auto result = object_ptr<ListWidget>(
		parent,
		controller,
		st::infoCommonGroupsList);
	result->scrollToRequests()
		| rpl::start_with_next([this](Ui::ScrollToRequest request) {
			auto addmin = (request.ymin < 0)
				? 0
				: st::infoCommonGroupsMargin.top();
			auto addmax = (request.ymax < 0)
				? 0
				: st::infoCommonGroupsMargin.top();
			_scrollToRequests.fire({
				request.ymin + addmin,
				request.ymax + addmax });
		}, result->lifetime());
	result->moveToLeft(0, st::infoCommonGroupsMargin.top());
	parent->widthValue()
		| rpl::start_with_next([list = result.data()](int newWidth) {
			list->resizeToWidth(newWidth);
		}, result->lifetime());
	result->heightValue()
		| rpl::start_with_next([parent](int listHeight) {
			auto newHeight = st::infoCommonGroupsMargin.top()
				+ listHeight
				+ st::infoCommonGroupsMargin.bottom();
			parent->resize(parent->width(), newHeight);
		}, result->lifetime());
	return result;
}

void InnerWidget::peerListSetTitle(base::lambda<QString()> title) {
}

void InnerWidget::peerListSetAdditionalTitle(
		base::lambda<QString()> title) {
}

bool InnerWidget::peerListIsRowSelected(not_null<PeerData*> peer) {
	return false;
}

int InnerWidget::peerListSelectedRowsCount() {
	return 0;
}

std::vector<not_null<PeerData*>> InnerWidget::peerListCollectSelectedRows() {
	return {};
}

void InnerWidget::peerListScrollToTop() {
	_scrollToRequests.fire({ -1, -1 });
}

void InnerWidget::peerListAddSelectedRowInBunch(not_null<PeerData*> peer) {
	Unexpected("Item selection in Info::Profile::Members.");
}

void InnerWidget::peerListFinishSelectedRowsBunch() {
}

void InnerWidget::peerListSetDescription(
		object_ptr<Ui::FlatLabel> description) {
	description.destroy();
}

} // namespace CommonGroups
} // namespace Info

