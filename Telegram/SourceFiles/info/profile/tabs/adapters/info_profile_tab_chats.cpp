/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/tabs/adapters/info_profile_tab_chats.h"

#include "api/api_messages_search.h"
#include "base/timer.h"
#include "data/data_saved_messages.h"
#include "data/data_saved_sublist.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "dialogs/ui/chat_search_in.h"
#include "dialogs/dialogs_inner_widget.h"
#include "history/view/history_view_chat_section.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/ui_utility.h"

namespace Info::Profile {
namespace {

constexpr auto kSearchRequestDelay = crl::time(900);
constexpr auto kSearchPreloadHeight = 500;

class ChatsTabAdapter final : public MediaTabContent {
public:
	explicit ChatsTabAdapter(MediaTabContext context)
	: _controller(context.controller)
	, _host(context.parent)
	, _messagesSearch(context.controller->session().data().history(
		context.controller->session().user()))
	, _searchTimer([=] { startSearch(); }) {
		const auto host = _host.data();
		_list = Ui::CreateChild<Dialogs::InnerWidget>(
			host,
			_controller->parentController(),
			rpl::single(Dialogs::InnerWidget::ChildListShown()));
		_list->setObjectName(u"profileTabChatsList"_q);
		_list->showSavedSublists();
		_list->setNarrowRatio(0.);
		_list->show();
		_list->heightValue(
		) | rpl::on_next([this](int height) {
			_host->resize(_host->width(), height);
		}, host->lifetime());

		_list->chosenRow() | rpl::on_next([=](Dialogs::ChosenRow row) {
			if (const auto sublist = row.key.sublist()) {
				using namespace Window;
				using namespace HistoryView;
				auto params = SectionShow(SectionShow::Way::Forward);
				params.dropSameFromStack = true;
				_controller->showSection(
					std::make_shared<ChatMemento>(ChatViewId{
						.history = sublist->owningHistory(),
						.sublist = sublist,
					}),
					params);
			}
		}, _list->lifetime());

		const auto saved = &_controller->session().data().savedMessages();
		_list->heightValue() | rpl::on_next([=] {
			if (!saved->supported()) {
				crl::on_main(_controller, [controller = _controller] {
					controller->showSection(
						Memento::Default(controller->session().user()),
						Window::SectionShow::Way::Backward);
				});
			}
		}, _list->lifetime());

		_list->setLoadMoreCallback([=] {
			saved->loadMore();
		});

		_list->searchRequests(
		) | rpl::on_next([this](Dialogs::SearchRequestDelay delay) {
			if (_searchQuery.isEmpty()) {
				return;
			} else if (delay == Dialogs::SearchRequestDelay::Delayed) {
				_searchTimer.callOnce(kSearchRequestDelay);
			} else {
				_searchTimer.cancel();
				startSearch();
			}
		}, _list->lifetime());

		_messagesSearch.messagesFounds(
		) | rpl::on_next([this](const Api::FoundMessages &found) {
			searchReceived(found);
		}, _list->lifetime());
	}

	not_null<Ui::RpWidget*> widget() override {
		return _host.data();
	}
	void resizeToWidth(int newWidth) override {
		if (_host->width() != newWidth) {
			_list->resizeToWidth(newWidth);
		}
		_host->resize(newWidth, _list->height());
	}
	TabTopBarBindings topBarBindings() override {
		return {
			.title = tr::lng_filters_edit_chats(tr::marked),
			.subtitle = SavedChatsCountStatus(&_controller->session()),
			.searchEnabledByContent = rpl::single(true),
			.applySearchQuery = crl::guard(
				base::make_weak(_list),
				[this](const QString &query) {
					applySearch(query);
				}),
		};
	}

	void deactivated() override {
		applySearch(QString());
	}

	void setVisibleRegion(int top, int bottom) override {
		_list->setVisibleTopBottom(top, bottom);
		if (_searchStarted
			&& !_searchFirstPage
			&& !_searchFull
			&& (bottom + kSearchPreloadHeight >= _list->height())) {
			_messagesSearch.searchMore();
		}
	}

private:
	void applySearch(const QString &query) {
		if (_searchQuery == query) {
			return;
		}
		_searchQuery = query;
		_searchTimer.cancel();
		_searchStarted = false;
		_searchFirstPage = false;
		_searchFull = false;
		auto state = Dialogs::SearchState();
		state.tab = Dialogs::ChatSearchTab::MyMessages;
		state.query = query;
		_list->applySearchState(std::move(state));
	}

	void startSearch() {
		if (_searchQuery.isEmpty()) {
			return;
		}
		_searchStarted = true;
		_searchFirstPage = true;
		_list->searchRequested(true);
		_messagesSearch.searchMessages({ .query = _searchQuery });
	}

	void searchReceived(const Api::FoundMessages &found) {
		const auto type = Dialogs::SearchRequestType{
			.start = base::take(_searchFirstPage),
			.peer = true,
		};
		if (found.messages.empty()) {
			_searchFull = true;
		}
		const auto owner = &_controller->session().data();
		auto items = std::vector<not_null<HistoryItem*>>();
		items.reserve(found.messages.size());
		for (const auto &fullId : found.messages) {
			if (const auto item = owner->message(fullId)) {
				items.push_back(item);
			}
		}
		_list->searchReceived(std::move(items), nullptr, type, found.total);
	}

	const not_null<Controller*> _controller;
	object_ptr<Ui::RpWidget> _host;
	Dialogs::InnerWidget *_list = nullptr;
	Api::MessagesSearch _messagesSearch;
	QString _searchQuery;
	base::Timer _searchTimer;
	bool _searchStarted = false;
	bool _searchFirstPage = false;
	bool _searchFull = false;

};

} // namespace

rpl::producer<TextWithEntities> SavedChatsCountStatus(
		not_null<Main::Session*> session) {
	const auto saved = &session->data().savedMessages();
	return saved->chatsList()->fullSize().value(
	) | rpl::map([=](int value) {
		return TextWithEntities{ (value || saved->chatsList()->loaded())
			? tr::lng_filters_chats_count(tr::now, lt_count, value)
			: tr::lng_contacts_loading(tr::now) };
	});
}

MediaTabDescriptor MakeChatsTabDescriptor() {
	return {
		.id = u"chats"_q,
		.title = tr::lng_filters_edit_chats(tr::marked),
		.shown = rpl::single(true),
		.factory = [](MediaTabContext context) {
			return std::make_unique<ChatsTabAdapter>(std::move(context));
		},
	};
}

} // namespace Info::Profile
