/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/share_box.h"

#include "api/api_premium.h"
#include "base/random.h"
#include "lang/lang_keys.h"
#include "base/qthelp_url.h"
#include "storage/storage_account.h"
#include "ui/boxes/confirm_box.h"
#include "apiwrap.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/multi_select.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/text/text_options.h"
#include "ui/text/text_utilities.h"
#include "ui/painter.h"
#include "chat_helpers/message_field.h"
#include "menu/menu_check_item.h"
#include "menu/menu_send.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_helpers.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_context_menu.h" // CopyPostLink.
#include "settings/settings_premium.h"
#include "window/window_session_controller.h"
#include "boxes/peer_list_controllers.h"
#include "chat_helpers/emoji_suggestions_widget.h"
#include "chat_helpers/share_message_phrase_factory.h"
#include "data/business/data_shortcut_messages.h"
#include "data/data_channel.h"
#include "data/data_game.h"
#include "data/data_histories.h"
#include "data/data_user.h"
#include "data/data_peer_values.h"
#include "data/data_session.h"
#include "data/data_folder.h"
#include "data/data_forum.h"
#include "data/data_forum_topic.h"
#include "data/data_changes.h"
#include "main/main_session.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "styles/style_menu_icons.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QClipboard>

class ShareBox::Inner final : public Ui::RpWidget {
public:
	Inner(
		QWidget *parent,
		const Descriptor &descriptor,
		std::shared_ptr<Ui::Show> show);

	void setPeerSelectedChangedCallback(
		Fn<void(not_null<Data::Thread*> thread, bool selected)> callback);
	void peerUnselected(not_null<PeerData*> peer);

	[[nodiscard]] std::vector<not_null<Data::Thread*>> selected() const;
	[[nodiscard]] bool hasSelected() const;

	void peopleReceived(
		const QString &query,
		const QVector<MTPPeer> &my,
		const QVector<MTPPeer> &people);

	void activateSkipRow(int direction);
	void activateSkipColumn(int direction);
	void activateSkipPage(int pageHeight, int direction);
	void updateFilter(QString filter = QString());
	void selectActive();

	rpl::producer<Ui::ScrollToRequest> scrollToRequests() const;
	rpl::producer<> searchRequests() const;

protected:
	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;

	void paintEvent(QPaintEvent *e) override;
	void enterEventHook(QEnterEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	struct Chat {
		Chat(
			not_null<History*> history,
			const style::PeerListItem &st,
			Fn<void()> updateCallback);

		not_null<History*> history;
		not_null<PeerData*> peer;
		Data::ForumTopic *topic = nullptr;
		rpl::lifetime topicLifetime;
		Ui::RoundImageCheckbox checkbox;
		Ui::Text::String name;
		Ui::Animations::Simple nameActive;
		bool locked = false;
	};

	void invalidateCache();
	bool showLockedError(not_null<Chat*> chat);
	void refreshLockedRows();

	[[nodiscard]] int displayedChatsCount() const;
	[[nodiscard]] not_null<Data::Thread*> chatThread(
		not_null<Chat*> chat) const;

	void paintChat(Painter &p, not_null<Chat*> chat, int index);
	void updateChat(not_null<PeerData*> peer);
	void updateChatName(not_null<Chat*> chat);
	void initChatLocked(not_null<Chat*> chat);
	void repaintChat(not_null<PeerData*> peer);
	int chatIndex(not_null<PeerData*> peer) const;
	void repaintChatAtIndex(int index);
	Chat *getChatAtIndex(int index);

	void loadProfilePhotos();
	void preloadUserpic(not_null<Dialogs::Entry*> entry);
	void changeCheckState(Chat *chat);
	void chooseForumTopic(not_null<Data::Forum*> forum);
	enum class ChangeStateWay {
		Default,
		SkipCallback,
	};
	void changePeerCheckState(
		not_null<Chat*> chat,
		bool checked,
		ChangeStateWay useCallback = ChangeStateWay::Default);

	not_null<Chat*> getChat(not_null<Dialogs::Row*> row);
	void setActive(int active);
	void updateUpon(const QPoint &pos);

	void refresh();

	const Descriptor &_descriptor;
	const std::shared_ptr<Ui::Show> _show;
	const style::PeerList &_st;

	float64 _columnSkip = 0.;
	float64 _rowWidthReal = 0.;
	int _rowsLeft = 0;
	int _rowsTop = 0;
	int _rowWidth = 0;
	int _rowHeight = 0;
	int _columnCount = 4;
	int _active = -1;
	int _upon = -1;
	int _visibleTop = 0;

	std::unique_ptr<Dialogs::IndexedList> _chatsIndexed;
	QString _filter;
	std::vector<not_null<Dialogs::Row*>> _filtered;

	std::map<not_null<PeerData*>, std::unique_ptr<Chat>> _dataMap;
	base::flat_set<not_null<Data::Thread*>> _selected;

	Fn<void(not_null<Data::Thread*>, bool)> _peerSelectedChangedCallback;

	bool _searching = false;
	QString _lastQuery;
	std::vector<PeerData*> _byUsernameFiltered;
	std::vector<std::unique_ptr<Chat>> d_byUsernameFiltered;

	rpl::event_stream<Ui::ScrollToRequest> _scrollToRequests;
	rpl::event_stream<> _searchRequests;

};

ShareBox::ShareBox(QWidget*, Descriptor &&descriptor)
: _descriptor(std::move(descriptor))
, _api(&_descriptor.session->mtp())
, _select(
	this,
	(_descriptor.stMultiSelect
		? *_descriptor.stMultiSelect
		: st::defaultMultiSelect),
	tr::lng_participant_filter())
, _comment(
	this,
	object_ptr<Ui::InputField>(
		this,
		(_descriptor.stComment
			? *_descriptor.stComment
			: st::shareComment),
		Ui::InputField::Mode::MultiLine,
		tr::lng_photos_comment()),
	st::shareCommentPadding)
, _bottomWidget(std::move(_descriptor.bottomWidget))
, _copyLinkText(_descriptor.copyLinkText
	? std::move(_descriptor.copyLinkText)
	: tr::lng_share_copy_link())
, _searchTimer([=] { searchByUsername(); }) {
	if (_bottomWidget) {
		_bottomWidget->setParent(this);
		_bottomWidget->resizeToWidth(st::boxWideWidth);
		_bottomWidget->show();
	}
}

void ShareBox::prepareCommentField() {
	_comment->hide(anim::type::instant);

	rpl::combine(
		heightValue(),
		_comment->heightValue(),
		(_bottomWidget
			? _bottomWidget->heightValue()
			: (rpl::single(0) | rpl::type_erased()))
	) | rpl::start_with_next([=](int height, int comment, int bottom) {
		_comment->moveToLeft(0, height - bottom - comment);
		if (_bottomWidget) {
			_bottomWidget->moveToLeft(0, height - bottom);
		}
	}, _comment->lifetime());

	const auto field = _comment->entity();

	field->submits(
	) | rpl::start_with_next([=] { submit({}); }, field->lifetime());
	if (const auto show = uiShow(); show->valid()) {
		InitMessageFieldHandlers(
			_descriptor.session,
			Main::MakeSessionShow(show, _descriptor.session),
			field,
			nullptr,
			nullptr,
			_descriptor.stLabel);
	}
	field->setSubmitSettings(Core::App().settings().sendSubmitWay());

	Ui::SendPendingMoveResizeEvents(_comment);
	if (_bottomWidget) {
		Ui::SendPendingMoveResizeEvents(_bottomWidget);
	}
}

void ShareBox::prepare() {
	prepareCommentField();

	setCloseByOutsideClick(false);

	_select->resizeToWidth(st::boxWideWidth);
	Ui::SendPendingMoveResizeEvents(_select);

	setTitle(tr::lng_share_title());

	_inner = setInnerWidget(
		object_ptr<Inner>(this, _descriptor, uiShow()),
		getTopScrollSkip(),
		getBottomScrollSkip());

	createButtons();

	setDimensions(st::boxWideWidth, st::boxMaxListHeight);

	_select->setQueryChangedCallback([=](const QString &query) {
		applyFilterUpdate(query);
	});
	_select->setItemRemovedCallback([=](uint64 itemId) {
		if (const auto peer = _descriptor.session->data().peerLoaded(PeerId(itemId))) {
			_inner->peerUnselected(peer);
			selectedChanged();
			update();
		}
	});
	_select->setResizedCallback([=] { updateScrollSkips(); });
	_select->setSubmittedCallback([=](Qt::KeyboardModifiers modifiers) {
		if (modifiers.testFlag(Qt::ControlModifier)
			|| modifiers.testFlag(Qt::MetaModifier)) {
			submit({});
		} else {
			_inner->selectActive();
		}
	});
	rpl::combine(
		_comment->heightValue(),
		(_bottomWidget
			? _bottomWidget->heightValue()
			: rpl::single(0) | rpl::type_erased())
	) | rpl::start_with_next([=] {
		updateScrollSkips();
	}, _comment->lifetime());

	_inner->searchRequests(
	) | rpl::start_with_next([=] {
		needSearchByUsername();
	}, _inner->lifetime());

	_inner->scrollToRequests(
	) | rpl::start_with_next([=](const Ui::ScrollToRequest &request) {
		scrollTo(request);
	}, _inner->lifetime());

	_inner->setPeerSelectedChangedCallback([=](
			not_null<Data::Thread*> thread,
			bool checked) {
		innerSelectedChanged(thread, checked);
	});

	Ui::Emoji::SuggestionsController::Init(
		getDelegate()->outerContainer(),
		_comment->entity(),
		_descriptor.session,
		{ .suggestCustomEmoji = true });

	_select->raise();
}

int ShareBox::getTopScrollSkip() const {
	return _select->isHidden() ? 0 : _select->height();
}

int ShareBox::getBottomScrollSkip() const {
	return (_comment->isHidden() ? 0 : _comment->height())
		+ (_bottomWidget ? _bottomWidget->height() : 0);
}

int ShareBox::contentHeight() const {
	return height() - getTopScrollSkip() - getBottomScrollSkip();
}

void ShareBox::updateScrollSkips() {
	setInnerTopSkip(getTopScrollSkip(), true);
	setInnerBottomSkip(getBottomScrollSkip());
}

bool ShareBox::searchByUsername(bool searchCache) {
	auto query = _select->getQuery();
	if (query.isEmpty()) {
		if (_peopleRequest) {
			_peopleRequest = 0;
		}
		return true;
	}
	if (!query.isEmpty()) {
		if (searchCache) {
			auto i = _peopleCache.constFind(query);
			if (i != _peopleCache.cend()) {
				_peopleQuery = query;
				_peopleRequest = 0;
				peopleDone(i.value(), 0);
				return true;
			}
		} else if (_peopleQuery != query) {
			_peopleQuery = query;
			_peopleFull = false;
			_peopleRequest = _api.request(MTPcontacts_Search(
				MTP_string(_peopleQuery),
				MTP_int(SearchPeopleLimit)
			)).done([=](const MTPcontacts_Found &result, mtpRequestId requestId) {
				peopleDone(result, requestId);
			}).fail([=](const MTP::Error &error, mtpRequestId requestId) {
				peopleFail(error, requestId);
			}).send();
			_peopleQueries.insert(_peopleRequest, _peopleQuery);
		}
	}
	return false;
}

void ShareBox::needSearchByUsername() {
	if (!searchByUsername(true)) {
		_searchTimer.callOnce(AutoSearchTimeout);
	}
}

void ShareBox::peopleDone(
		const MTPcontacts_Found &result,
		mtpRequestId requestId) {
	Expects(result.type() == mtpc_contacts_found);

	auto query = _peopleQuery;

	auto i = _peopleQueries.find(requestId);
	if (i != _peopleQueries.cend()) {
		query = i.value();
		_peopleCache[query] = result;
		_peopleQueries.erase(i);
	}

	if (_peopleRequest == requestId) {
		switch (result.type()) {
		case mtpc_contacts_found: {
			auto &found = result.c_contacts_found();
			_descriptor.session->data().processUsers(found.vusers());
			_descriptor.session->data().processChats(found.vchats());
			_inner->peopleReceived(
				query,
				found.vmy_results().v,
				found.vresults().v);
		} break;
		}

		_peopleRequest = 0;
	}
}

void ShareBox::peopleFail(const MTP::Error &error, mtpRequestId requestId) {
	if (_peopleRequest == requestId) {
		_peopleRequest = 0;
		_peopleFull = true;
	}
}

void ShareBox::setInnerFocus() {
	if (_comment->isHidden()) {
		_select->setInnerFocus();
	} else {
		_comment->entity()->setFocusFast();
	}
}

void ShareBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	_select->resizeToWidth(width());
	_select->moveToLeft(0, 0);

	updateScrollSkips();

	_inner->resizeToWidth(width());
}

void ShareBox::keyPressEvent(QKeyEvent *e) {
	auto focused = focusWidget();
	if (_select == focused || _select->isAncestorOf(focusWidget())) {
		if (e->key() == Qt::Key_Up) {
			_inner->activateSkipColumn(-1);
		} else if (e->key() == Qt::Key_Down) {
			_inner->activateSkipColumn(1);
		} else if (e->key() == Qt::Key_PageUp) {
			_inner->activateSkipPage(contentHeight(), -1);
		} else if (e->key() == Qt::Key_PageDown) {
			_inner->activateSkipPage(contentHeight(), 1);
		} else {
			BoxContent::keyPressEvent(e);
		}
	} else {
		BoxContent::keyPressEvent(e);
	}
}

SendMenu::Type ShareBox::sendMenuType() const {
	const auto selected = _inner->selected();
	return ranges::all_of(
		selected | ranges::views::transform(&Data::Thread::peer),
		HistoryView::CanScheduleUntilOnline)
		? SendMenu::Type::ScheduledToUser
		: (selected.size() == 1 && selected.front()->peer()->isSelf())
		? SendMenu::Type::Reminder
		: SendMenu::Type::Scheduled;
}

void ShareBox::showMenu(not_null<Ui::RpWidget*> parent) {
	if (_menu) {
		_menu = nullptr;
		return;
	}
	_menu.emplace(parent, st::popupMenuWithIcons);

	if (_descriptor.forwardOptions.show) {
		auto createView = [&](rpl::producer<QString> &&text, bool checked) {
			auto item = base::make_unique_q<Menu::ItemWithCheck>(
				_menu->menu(),
				st::popupMenuWithIcons.menu,
				Ui::CreateChild<QAction>(_menu->menu().get()),
				nullptr,
				nullptr);
			std::move(
				text
			) | rpl::start_with_next([action = item->action()](QString text) {
				action->setText(text);
			}, item->lifetime());
			item->init(checked);
			const auto view = item->checkView();
			_menu->addAction(std::move(item));
			return view;
		};
		Ui::FillForwardOptions(
			std::move(createView),
			_forwardOptions,
			[=](Ui::ForwardOptions value) { _forwardOptions = value; },
			_menu->lifetime());

		_menu->addSeparator();
	}

	const auto result = SendMenu::FillSendMenu(
		_menu.get(),
		sendMenuType(),
		[=] { submitSilent(); },
		[=] { submitScheduled(); },
		[=] { submitWhenOnline(); });
	const auto success = (result == SendMenu::FillMenuResult::Success);
	if (_descriptor.forwardOptions.show || success) {
		_menu->setForcedVerticalOrigin(Ui::PopupMenu::VerticalOrigin::Bottom);
		_menu->popup(QCursor::pos());
	}
}

void ShareBox::createButtons() {
	clearButtons();
	if (_hasSelected) {
		const auto send = addButton(tr::lng_share_confirm(), [=] {
			submit({});
		});
		_forwardOptions.sendersCount
			= _descriptor.forwardOptions.sendersCount;
		_forwardOptions.captionsCount
			= _descriptor.forwardOptions.captionsCount;

		send->setAcceptBoth();
		send->clicks(
		) | rpl::start_with_next([=](Qt::MouseButton button) {
			if (button == Qt::RightButton) {
				showMenu(send);
			}
		}, send->lifetime());
	} else if (_descriptor.copyCallback) {
		addButton(_copyLinkText.value(), [=] { copyLink(); });
	}
	addButton(tr::lng_cancel(), [=] { closeBox(); });
}

void ShareBox::applyFilterUpdate(const QString &query) {
	scrollToY(0);
	_inner->updateFilter(query);
}

void ShareBox::addPeerToMultiSelect(not_null<Data::Thread*> thread) {
	auto addItemWay = Ui::MultiSelect::AddItemWay::Default;
	const auto peer = thread->peer();
	const auto topic = thread->asTopic();
	_select->addItem(
		peer->id.value,
		(topic
			? topic->title()
			: peer->isSelf()
			? tr::lng_saved_short(tr::now)
			: peer->shortName()),
		st::activeButtonBg,
		(topic
			? ForceRoundUserpicCallback(peer)
			: PaintUserpicCallback(peer, true)),
		addItemWay);
}

void ShareBox::innerSelectedChanged(
		not_null<Data::Thread*> thread,
		bool checked) {
	if (checked) {
		addPeerToMultiSelect(thread);
		_select->clearQuery();
	} else {
		_select->removeItem(thread->peer()->id.value);
	}
	selectedChanged();
	update();
}

void ShareBox::submit(Api::SendOptions options) {
	if (const auto onstack = _descriptor.submitCallback) {
		const auto forwardOptions = (_forwardOptions.captionsCount
			&& _forwardOptions.dropCaptions)
			? Data::ForwardOptions::NoNamesAndCaptions
			: _forwardOptions.dropNames
			? Data::ForwardOptions::NoSenderNames
			: Data::ForwardOptions::PreserveInfo;
		onstack(
			_inner->selected(),
			_comment->entity()->getTextWithAppliedMarkdown(),
			options,
			forwardOptions);
	}
}

void ShareBox::submitSilent() {
	submit({ .silent = true });
}

void ShareBox::submitScheduled() {
	const auto callback = [=](Api::SendOptions options) { submit(options); };
	uiShow()->showBox(
		HistoryView::PrepareScheduleBox(
			this,
			sendMenuType(),
			callback,
			HistoryView::DefaultScheduleTime(),
			_descriptor.scheduleBoxStyle));
}

void ShareBox::submitWhenOnline() {
	submit(Api::DefaultSendWhenOnlineOptions());
}

void ShareBox::copyLink() const {
	if (const auto onstack = _descriptor.copyCallback) {
		onstack();
	}
}

void ShareBox::selectedChanged() {
	auto hasSelected = _inner->hasSelected();
	if (_hasSelected != hasSelected) {
		_hasSelected = hasSelected;
		createButtons();
		_comment->toggle(_hasSelected, anim::type::normal);
		_comment->resizeToWidth(st::boxWideWidth);
	}
	update();
}

void ShareBox::scrollTo(Ui::ScrollToRequest request) {
	scrollToY(request.ymin, request.ymax);
	//auto scrollTop = scrollArea()->scrollTop(), scrollBottom = scrollTop + scrollArea()->height();
	//auto from = scrollTop, to = scrollTop;
	//if (scrollTop > top) {
	//	to = top;
	//} else if (scrollBottom < bottom) {
	//	to = bottom - (scrollBottom - scrollTop);
	//}
	//if (from != to) {
	//	_scrollAnimation.start([this]() { scrollAnimationCallback(); }, from, to, st::shareScrollDuration, anim::sineInOut);
	//}
}

void ShareBox::scrollAnimationCallback() {
	//auto scrollTop = qRound(_scrollAnimation.current(scrollArea()->scrollTop()));
	//scrollArea()->scrollToY(scrollTop);
}

ShareBox::Inner::Inner(
	QWidget *parent,
	const Descriptor &descriptor,
	std::shared_ptr<Ui::Show> show)
: RpWidget(parent)
, _descriptor(descriptor)
, _show(std::move(show))
, _st(_descriptor.st ? *_descriptor.st : st::shareBoxList)
, _chatsIndexed(
	std::make_unique<Dialogs::IndexedList>(
		Dialogs::SortMode::Add)) {
	_rowsTop = st::shareRowsTop;
	_rowHeight = st::shareRowHeight;
	setAttribute(Qt::WA_OpaquePaintEvent);

	if (_descriptor.premiumRequiredError) {
		const auto session = _descriptor.session;
		rpl::merge(
			Data::AmPremiumValue(session) | rpl::to_empty,
			session->api().premium().somePremiumRequiredResolved()
		) | rpl::start_with_next([=] {
			refreshLockedRows();
		}, lifetime());
	}

	const auto self = _descriptor.session->user();
	const auto selfHistory = self->owner().history(self);
	if (_descriptor.filterCallback(selfHistory)) {
		_chatsIndexed->addToEnd(selfHistory);
	}
	const auto addList = [&](not_null<Dialogs::IndexedList*> list) {
		for (const auto &row : list->all()) {
			if (const auto history = row->history()) {
				if (!history->peer->isSelf()
					&& (history->asForum()
						|| _descriptor.filterCallback(history))) {
					_chatsIndexed->addToEnd(history);
				}
			}
		}
	};
	addList(_descriptor.session->data().chatsList()->indexed());
	const auto id = Data::Folder::kId;
	if (const auto folder = _descriptor.session->data().folderLoaded(id)) {
		addList(folder->chatsList()->indexed());
	}
	addList(_descriptor.session->data().contactsNoChatsList());

	_filter = u"a"_q;
	updateFilter();

	_descriptor.session->changes().peerUpdates(
		Data::PeerUpdate::Flag::Photo
	) | rpl::start_with_next([=](const Data::PeerUpdate &update) {
		updateChat(update.peer);
	}, lifetime());

	_descriptor.session->changes().realtimeNameUpdates(
	) | rpl::start_with_next([=](const Data::NameUpdate &update) {
		_chatsIndexed->peerNameChanged(
			update.peer,
			update.oldFirstLetters);
	}, lifetime());

	_descriptor.session->downloaderTaskFinished(
	) | rpl::start_with_next([=] {
		update();
	}, lifetime());

	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		invalidateCache();
	}, lifetime());
}

void ShareBox::Inner::invalidateCache() {
	for (const auto &[peer, data] : _dataMap) {
		data->checkbox.invalidateCache();
	}
}

bool ShareBox::Inner::showLockedError(not_null<Chat*> chat) {
	if (!chat->locked) {
		return false;
	}
	::Settings::ShowPremiumPromoToast(
		Main::MakeSessionShow(_show, _descriptor.session),
		ChatHelpers::ResolveWindowDefault(),
		_descriptor.premiumRequiredError(chat->peer->asUser()).text,
		u"require_premium"_q);
	return true;
}

void ShareBox::Inner::refreshLockedRows() {
	auto changed = false;
	for (const auto &[peer, data] : _dataMap) {
		const auto history = data->history;
		const auto locked = (Api::ResolveRequiresPremiumToWrite(
			history->peer,
			history
		) == Api::RequirePremiumState::Yes);
		if (data->locked != locked) {
			data->locked = locked;
			changed = true;
		}
	}
	for (const auto &data : d_byUsernameFiltered) {
		const auto history = data->history;
		const auto locked = (Api::ResolveRequiresPremiumToWrite(
			history->peer,
			history
		) == Api::RequirePremiumState::Yes);
		if (data->locked != locked) {
			data->locked = locked;
			changed = true;
		}
	}
	if (changed) {
		update();
	}
}

void ShareBox::Inner::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	_visibleTop = visibleTop;
	loadProfilePhotos();
}

void ShareBox::Inner::activateSkipRow(int direction) {
	activateSkipColumn(direction * _columnCount);
}

int ShareBox::Inner::displayedChatsCount() const {
	return _filter.isEmpty() ? _chatsIndexed->size() : (_filtered.size() + d_byUsernameFiltered.size());
}

void ShareBox::Inner::activateSkipColumn(int direction) {
	if (_active < 0) {
		if (direction > 0) {
			setActive(0);
		}
		return;
	}
	auto count = displayedChatsCount();
	auto active = _active + direction;
	if (active < 0) {
		active = (_active > 0) ? 0 : -1;
	}
	if (active >= count) {
		active = count - 1;
	}
	setActive(active);
}

void ShareBox::Inner::activateSkipPage(int pageHeight, int direction) {
	activateSkipRow(direction * (pageHeight / _rowHeight));
}

void ShareBox::Inner::updateChat(not_null<PeerData*> peer) {
	if (const auto i = _dataMap.find(peer); i != end(_dataMap)) {
		updateChatName(i->second.get());
		repaintChat(peer);
	}
}

void ShareBox::Inner::updateChatName(not_null<Chat*> chat) {
	const auto peer = chat->peer;
	const auto text = chat->topic
		? chat->topic->title()
		: peer->isSelf()
		? tr::lng_saved_messages(tr::now)
		: peer->isRepliesChat()
		? tr::lng_replies_messages(tr::now)
		: peer->name();
	chat->name.setText(_st.item.nameStyle, text, Ui::NameTextOptions());
}

void ShareBox::Inner::initChatLocked(not_null<Chat*> chat) {
	if (_descriptor.premiumRequiredError) {
		const auto history = chat->history;
		if (Api::ResolveRequiresPremiumToWrite(
			history->peer,
			history
		) == Api::RequirePremiumState::Yes) {
			chat->locked = true;
		}
	}
}

void ShareBox::Inner::repaintChatAtIndex(int index) {
	if (index < 0) return;

	auto row = index / _columnCount;
	auto column = index % _columnCount;
	update(style::rtlrect(_rowsLeft + qFloor(column * _rowWidthReal), row * _rowHeight, _rowWidth, _rowHeight, width()));
}

ShareBox::Inner::Chat *ShareBox::Inner::getChatAtIndex(int index) {
	if (index < 0) {
		return nullptr;
	}
	const auto row = [=] {
		if (_filter.isEmpty()) {
			return (index < _chatsIndexed->size())
				? (_chatsIndexed->begin() + index)->get()
				: nullptr;
		}
		return (index < _filtered.size())
			? _filtered[index].get()
			: nullptr;
	}();
	if (row) {
		return static_cast<Chat*>(row->attached);
	}

	if (!_filter.isEmpty()) {
		index -= _filtered.size();
		if (index >= 0 && index < d_byUsernameFiltered.size()) {
			return d_byUsernameFiltered[index].get();
		}
	}
	return nullptr;
}

void ShareBox::Inner::repaintChat(not_null<PeerData*> peer) {
	repaintChatAtIndex(chatIndex(peer));
}

int ShareBox::Inner::chatIndex(not_null<PeerData*> peer) const {
	int index = 0;
	if (_filter.isEmpty()) {
		for (const auto &row : _chatsIndexed->all()) {
			if (const auto history = row->history()) {
				if (history->peer == peer) {
					return index;
				}
			}
			++index;
		}
	} else {
		for (const auto &row : _filtered) {
			if (const auto history = row->history()) {
				if (history->peer == peer) {
					return index;
				}
			}
			++index;
		}
		for (const auto &row : d_byUsernameFiltered) {
			if (row->peer == peer) {
				return index;
			}
			++index;
		}
	}
	return -1;
}

void ShareBox::Inner::loadProfilePhotos() {
	if (!parentWidget()) {
		return;
	}
	auto yFrom = std::max(_visibleTop, 0);
	if (auto part = (yFrom % _rowHeight)) {
		yFrom -= part;
	}
	int yTo = yFrom + parentWidget()->height() * 5 * _columnCount;
	if (!yTo) {
		return;
	}
	yFrom *= _columnCount;
	yTo *= _columnCount;

	if (_filter.isEmpty()) {
		if (!_chatsIndexed->empty()) {
			const auto index = yFrom / _rowHeight;
			auto i = _chatsIndexed->begin()
				+ std::min(index, _chatsIndexed->size());
			for (auto end = _chatsIndexed->cend(); i != end; ++i) {
				if (((*i)->index() * _rowHeight) >= yTo) {
					break;
				}
				preloadUserpic((*i)->entry());
			}
		}
	} else {
		const auto from = std::max(yFrom / _rowHeight, 0);
		const auto to = std::max((yTo / _rowHeight) + 1, from);

		const auto fto = std::min(to, int(_filtered.size()));
		const auto ffrom = std::min(from, fto);
		for (auto i = ffrom; i != fto; ++i) {
			preloadUserpic(_filtered[i]->entry());
		}

		const auto uto = std::min(
			to - int(_filtered.size()),
			int(d_byUsernameFiltered.size()));
		const auto ufrom = std::min(
			std::max(from - int(_filtered.size()), 0),
			uto);
		for (auto i = ufrom; i != uto; ++i) {
			preloadUserpic(d_byUsernameFiltered[i]->history);
		}
	}
}

void ShareBox::Inner::preloadUserpic(not_null<Dialogs::Entry*> entry) {
	entry->chatListPreloadData();
	const auto history = entry->asHistory();
	if (!_descriptor.premiumRequiredError || !history) {
		return;
	} else if (Api::ResolveRequiresPremiumToWrite(
		history->peer,
		history
	) == Api::RequirePremiumState::Unknown) {
		const auto user = history->peer->asUser();
		_descriptor.session->api().premium().resolvePremiumRequired(user);
	}
}

auto ShareBox::Inner::getChat(not_null<Dialogs::Row*> row)
-> not_null<Chat*> {
	Expects(row->history() != nullptr);

	if (const auto data = static_cast<Chat*>(row->attached)) {
		return data;
	}
	const auto history = row->history();
	const auto peer = history->peer;
	if (const auto i = _dataMap.find(peer); i != end(_dataMap)) {
		row->attached = i->second.get();
		return i->second.get();
	}
	const auto &[i, ok] = _dataMap.emplace(
		peer,
		std::make_unique<Chat>(history, _st.item, [=] {
			repaintChat(peer);
		}));
	updateChatName(i->second.get());
	initChatLocked(i->second.get());
	row->attached = i->second.get();
	return i->second.get();
}

void ShareBox::Inner::setActive(int active) {
	if (active != _active) {
		auto changeNameFg = [this](int index, float64 from, float64 to) {
			if (auto chat = getChatAtIndex(index)) {
				chat->nameActive.start([this, peer = chat->peer] {
					repaintChat(peer);
				}, from, to, st::shareActivateDuration);
			}
		};
		changeNameFg(_active, 1., 0.);
		_active = active;
		changeNameFg(_active, 0., 1.);
	}
	auto y = (_active < _columnCount) ? 0 : (_rowsTop + ((_active / _columnCount) * _rowHeight));
	_scrollToRequests.fire({ y, y + _rowHeight });
}

void ShareBox::Inner::paintChat(
		Painter &p,
		not_null<Chat*> chat,
		int index) {
	auto x = _rowsLeft + qFloor((index % _columnCount) * _rowWidthReal);
	auto y = _rowsTop + (index / _columnCount) * _rowHeight;

	auto outerWidth = width();
	auto photoLeft = (_rowWidth - (_st.item.checkbox.imageRadius * 2)) / 2;
	auto photoTop = st::sharePhotoTop;
	chat->checkbox.paint(p, x + photoLeft, y + photoTop, outerWidth);

	if (chat->locked) {
		PaintPremiumRequiredLock(
			p,
			&_st.item,
			x + photoLeft,
			y + photoTop,
			outerWidth,
			_st.item.checkbox.imageRadius * 2);
	}

	auto nameActive = chat->nameActive.value((index == _active) ? 1. : 0.);
	p.setPen(anim::pen(_st.item.nameFg, _st.item.nameFgChecked, nameActive));

	auto nameWidth = (_rowWidth - st::shareColumnSkip);
	auto nameLeft = st::shareColumnSkip / 2;
	auto nameTop = photoTop + _st.item.checkbox.imageRadius * 2 + st::shareNameTop;
	chat->name.drawLeftElided(p, x + nameLeft, y + nameTop, nameWidth, outerWidth, 2, style::al_top, 0, -1, 0, true);
}

ShareBox::Inner::Chat::Chat(
	not_null<History*> history,
	const style::PeerListItem &st,
	Fn<void()> updateCallback)
: history(history)
, peer(history->peer)
, checkbox(
	st.checkbox,
	updateCallback,
	PaintUserpicCallback(peer, true),
	[=](int size) { return peer->isForum()
		? int(size * Ui::ForumUserpicRadiusMultiplier())
		: std::optional<int>(); })
, name(st.checkbox.imageRadius * 2) {
}

void ShareBox::Inner::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto r = e->rect();
	p.setClipRect(r);
	p.fillRect(r, _st.bg);
	auto yFrom = r.y(), yTo = r.y() + r.height();
	auto rowFrom = yFrom / _rowHeight;
	auto rowTo = (yTo + _rowHeight - 1) / _rowHeight;
	auto indexFrom = rowFrom * _columnCount;
	auto indexTo = rowTo * _columnCount;
	if (_filter.isEmpty()) {
		if (!_chatsIndexed->empty()) {
			auto i = _chatsIndexed->begin()
				+ std::min(indexFrom, _chatsIndexed->size());
			for (auto end = _chatsIndexed->cend(); i != end; ++i) {
				if (indexFrom >= indexTo) {
					break;
				}
				paintChat(p, getChat(*i), indexFrom);
				++indexFrom;
			}
		} else {
			p.setFont(st::noContactsFont);
			p.setPen(_st.about.textFg);
			p.drawText(
				rect().marginsRemoved(st::boxPadding),
				tr::lng_bot_no_chats(tr::now),
				style::al_center);
		}
	} else {
		if (_filtered.empty()
			&& _byUsernameFiltered.empty()
			&& !_searching) {
			p.setFont(st::noContactsFont);
			p.setPen(_st.about.textFg);
			p.drawText(
				rect().marginsRemoved(st::boxPadding),
				tr::lng_bot_chats_not_found(tr::now),
				style::al_center);
		} else {
			auto filteredSize = _filtered.size();
			if (filteredSize) {
				if (indexFrom < 0) indexFrom = 0;
				while (indexFrom < indexTo) {
					if (indexFrom >= _filtered.size()) {
						break;
					}
					paintChat(p, getChat(_filtered[indexFrom]), indexFrom);
					++indexFrom;
				}
				indexFrom -= filteredSize;
				indexTo -= filteredSize;
			}
			if (!_byUsernameFiltered.empty()) {
				if (indexFrom < 0) indexFrom = 0;
				while (indexFrom < indexTo) {
					if (indexFrom >= d_byUsernameFiltered.size()) {
						break;
					}
					paintChat(
						p,
						d_byUsernameFiltered[indexFrom].get(),
						filteredSize + indexFrom);
					++indexFrom;
				}
			}
		}
	}
}

void ShareBox::Inner::enterEventHook(QEnterEvent *e) {
	setMouseTracking(true);
}

void ShareBox::Inner::leaveEventHook(QEvent *e) {
	setMouseTracking(false);
}

void ShareBox::Inner::mouseMoveEvent(QMouseEvent *e) {
	updateUpon(e->pos());
	setCursor((_upon >= 0) ? style::cur_pointer : style::cur_default);
}

void ShareBox::Inner::updateUpon(const QPoint &pos) {
	auto x = pos.x(), y = pos.y();
	auto row = (y - _rowsTop) / _rowHeight;
	auto column = qFloor((x - _rowsLeft) / _rowWidthReal);
	auto left = _rowsLeft + qFloor(column * _rowWidthReal) + st::shareColumnSkip / 2;
	auto top = _rowsTop + row * _rowHeight + st::sharePhotoTop;
	auto xupon = (x >= left) && (x < left + (_rowWidth - st::shareColumnSkip));
	auto yupon = (y >= top) && (y < top + _st.item.checkbox.imageRadius * 2 + st::shareNameTop + _st.item.nameStyle.font->height * 2);
	auto upon = (xupon && yupon) ? (row * _columnCount + column) : -1;
	if (upon >= displayedChatsCount()) {
		upon = -1;
	}
	_upon = upon;
}

void ShareBox::Inner::mousePressEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton) {
		updateUpon(e->pos());
		changeCheckState(getChatAtIndex(_upon));
	}
}

void ShareBox::Inner::selectActive() {
	changeCheckState(getChatAtIndex(_active > 0 ? _active : 0));
}

void ShareBox::Inner::resizeEvent(QResizeEvent *e) {
	_columnSkip = (width() - _columnCount * _st.item.checkbox.imageRadius * 2) / float64(_columnCount + 1);
	_rowWidthReal = _st.item.checkbox.imageRadius * 2 + _columnSkip;
	_rowsLeft = qFloor(_columnSkip / 2);
	_rowWidth = qFloor(_rowWidthReal);
	update();
}

void ShareBox::Inner::changeCheckState(Chat *chat) {
	if (!chat || showLockedError(chat)) {
		return;
	} else if (!_filter.isEmpty()) {
		const auto history = chat->peer->owner().history(chat->peer);
		auto row = _chatsIndexed->getRow(history);
		if (!row) {
			row = _chatsIndexed->addToEnd(history).main;
		}
		chat = getChat(row);
		if (!chat->checkbox.checked()) {
			_chatsIndexed->moveToTop(history);
		}
	}

	const auto checked = chat->checkbox.checked();
	const auto forum = chat->peer->forum();
	if (checked || !forum) {
		changePeerCheckState(chat, !checked);
	} else {
		chooseForumTopic(chat->peer->forum());
	}
}

void ShareBox::Inner::chooseForumTopic(not_null<Data::Forum*> forum) {
	const auto guard = Ui::MakeWeak(this);
	const auto weak = std::make_shared<QPointer<Ui::BoxContent>>();
	auto chosen = [=](not_null<Data::ForumTopic*> topic) {
		if (const auto strong = *weak) {
			strong->closeBox();
		}
		if (!guard) {
			return;
		}
		const auto row = _chatsIndexed->getRow(topic->owningHistory());
		if (!row) {
			return;
		}
		const auto chat = getChat(row);
		Assert(!chat->topic);
		chat->topic = topic;
		chat->topic->destroyed(
		) | rpl::start_with_next([=] {
			changePeerCheckState(chat, false);
		}, chat->topicLifetime);
		updateChatName(chat);
		changePeerCheckState(chat, true);
	};
	auto initBox = [=](not_null<PeerListBox*> box) {
		box->addButton(tr::lng_cancel(), [=] {
			box->closeBox();
		});

		forum->destroyed(
		) | rpl::start_with_next([=] {
			box->closeBox();
		}, box->lifetime());
	};
	auto filter = [=](not_null<Data::ForumTopic*> topic) {
		return guard && _descriptor.filterCallback(topic);
	};
	auto box = Box<PeerListBox>(
		std::make_unique<ChooseTopicBoxController>(
			forum,
			std::move(chosen),
			std::move(filter)),
		std::move(initBox));
	*weak = box.data();
	_show->showBox(std::move(box));
}

void ShareBox::Inner::peerUnselected(not_null<PeerData*> peer) {
	if (const auto i = _dataMap.find(peer); i != end(_dataMap)) {
		changePeerCheckState(
			i->second.get(),
			false,
			ChangeStateWay::SkipCallback);
	}
}

void ShareBox::Inner::setPeerSelectedChangedCallback(
		Fn<void(not_null<Data::Thread*> thread, bool selected)> callback) {
	_peerSelectedChangedCallback = std::move(callback);
}

void ShareBox::Inner::changePeerCheckState(
		not_null<Chat*> chat,
		bool checked,
		ChangeStateWay useCallback) {
	chat->checkbox.setChecked(checked);
	const auto thread = chatThread(chat);
	if (checked) {
		_selected.emplace(thread);
		setActive(chatIndex(chat->peer));
	} else {
		_selected.remove(thread);
		if (chat->topic) {
			chat->topicLifetime.destroy();
			chat->topic = nullptr;
			updateChatName(chat);
		}
	}
	if (useCallback != ChangeStateWay::SkipCallback
		&& _peerSelectedChangedCallback) {
		_peerSelectedChangedCallback(thread, checked);
	}
}

bool ShareBox::Inner::hasSelected() const {
	return _selected.size();
}

void ShareBox::Inner::updateFilter(QString filter) {
	_lastQuery = filter.toLower().trimmed();

	auto words = TextUtilities::PrepareSearchWords(_lastQuery);
	filter = words.isEmpty() ? QString() : words.join(' ');
	if (_filter != filter) {
		_filter = filter;

		_byUsernameFiltered.clear();
		d_byUsernameFiltered.clear();

		if (_filter.isEmpty()) {
			refresh();
		} else {
			_filtered = _chatsIndexed->filtered(words);
			refresh();

			_searching = true;
			_searchRequests.fire({});
		}
		setActive(-1);
		loadProfilePhotos();
		update();
	}
}

rpl::producer<Ui::ScrollToRequest> ShareBox::Inner::scrollToRequests() const {
	return _scrollToRequests.events();
}

rpl::producer<> ShareBox::Inner::searchRequests() const {
	return _searchRequests.events();
}

void ShareBox::Inner::peopleReceived(
		const QString &query,
		const QVector<MTPPeer> &my,
		const QVector<MTPPeer> &people) {
	_lastQuery = query.toLower().trimmed();
	if (_lastQuery.at(0) == '@') {
		_lastQuery = _lastQuery.mid(1);
	}
	int32 already = _byUsernameFiltered.size();
	_byUsernameFiltered.reserve(already + my.size() + people.size());
	d_byUsernameFiltered.reserve(already + my.size() + people.size());
	const auto feedList = [&](const QVector<MTPPeer> &list) {
		for (const auto &data : list) {
			if (const auto peer = _descriptor.session->data().peerLoaded(
					peerFromMTP(data))) {
				const auto history = _descriptor.session->data().history(
					peer);
				if (!history->asForum()
					&& !_descriptor.filterCallback(history)) {
					continue;
				} else if (history && _chatsIndexed->getRow(history)) {
					continue;
				} else if (base::contains(_byUsernameFiltered, peer)) {
					continue;
				}
				_byUsernameFiltered.push_back(peer);
				d_byUsernameFiltered.push_back(std::make_unique<Chat>(
					history,
					_st.item,
					[=] { repaintChat(peer); }));
				updateChatName(d_byUsernameFiltered.back().get());
				initChatLocked(d_byUsernameFiltered.back().get());
			}
		}
	};
	feedList(my);
	feedList(people);

	_searching = false;
	refresh();
}

void ShareBox::Inner::refresh() {
	auto count = displayedChatsCount();
	if (count) {
		auto rows = (count / _columnCount) + (count % _columnCount ? 1 : 0);
		resize(width(), _rowsTop + rows * _rowHeight);
	} else {
		resize(width(), st::noContactsHeight);
	}
	loadProfilePhotos();
	update();
}

not_null<Data::Thread*> ShareBox::Inner::chatThread(
		not_null<Chat*> chat) const {
	return chat->topic
		? (Data::Thread*)chat->topic
		: chat->peer->owner().history(chat->peer).get();
}

std::vector<not_null<Data::Thread*>> ShareBox::Inner::selected() const {
	auto result = std::vector<not_null<Data::Thread*>>();
	result.reserve(_dataMap.size());
	for (const auto &[peer, chat] : _dataMap) {
		if (chat->checkbox.checked()) {
			result.push_back(chatThread(chat.get()));
		}
	}
	return result;
}

QString AppendShareGameScoreUrl(
		not_null<Main::Session*> session,
		const QString &url,
		const FullMsgId &fullId) {
	auto shareHashData = QByteArray(0x20, Qt::Uninitialized);
	auto shareHashDataInts = reinterpret_cast<uint64*>(shareHashData.data());
	const auto peer = fullId.peer
		? session->data().peerLoaded(fullId.peer)
		: static_cast<PeerData*>(nullptr);
	const auto channelAccessHash = uint64((peer && peer->isChannel())
		? peer->asChannel()->access
		: 0);
	shareHashDataInts[0] = session->userId().bare;
	shareHashDataInts[1] = fullId.peer.value;
	shareHashDataInts[2] = uint64(fullId.msg.bare);
	shareHashDataInts[3] = channelAccessHash;

	// Count SHA1() of data.
	auto key128Size = 0x10;
	auto shareHashEncrypted = QByteArray(key128Size + shareHashData.size(), Qt::Uninitialized);
	hashSha1(shareHashData.constData(), shareHashData.size(), shareHashEncrypted.data());

	//// Mix in channel access hash to the first 64 bits of SHA1 of data.
	//*reinterpret_cast<uint64*>(shareHashEncrypted.data()) ^= channelAccessHash;

	// Encrypt data.
	if (!session->local().encrypt(shareHashData.constData(), shareHashEncrypted.data() + key128Size, shareHashData.size(), shareHashEncrypted.constData())) {
		return url;
	}

	auto shareHash = shareHashEncrypted.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
	auto shareUrl = u"tg://share_game_score?hash="_q + QString::fromLatin1(shareHash);

	auto shareComponent = u"tgShareScoreUrl="_q + qthelp::url_encode(shareUrl);

	auto hashPosition = url.indexOf('#');
	if (hashPosition < 0) {
		return url + '#' + shareComponent;
	}
	auto hash = url.mid(hashPosition + 1);
	if (hash.indexOf('=') >= 0 || hash.indexOf('?') >= 0) {
		return url + '&' + shareComponent;
	}
	if (!hash.isEmpty()) {
		return url + '?' + shareComponent;
	}
	return url + shareComponent;
}

ChatHelpers::ForwardedMessagePhraseArgs CreateForwardedMessagePhraseArgs(
		const std::vector<not_null<Data::Thread*>> &result,
		const MessageIdsList &msgIds) {
	const auto toCount = result.size();
	return {
		.toCount = result.size(),
		.singleMessage = (msgIds.size() <= 1),
		.to1 = (toCount > 0) ? result.front()->peer().get() : nullptr,
		.to2 = (toCount > 1) ? result[1]->peer().get() : nullptr,
	};
}

ShareBox::SubmitCallback ShareBox::DefaultForwardCallback(
		std::shared_ptr<Ui::Show> show,
		not_null<History*> history,
		MessageIdsList msgIds) {
	struct State final {
		base::flat_set<mtpRequestId> requests;
	};
	const auto state = std::make_shared<State>();
	return [=](
			std::vector<not_null<Data::Thread*>> &&result,
			TextWithTags &&comment,
			Api::SendOptions options,
			Data::ForwardOptions forwardOptions) {
		if (!state->requests.empty()) {
			return; // Share clicked already.
		}
		const auto items = history->owner().idsToItems(msgIds);
		const auto existingIds = history->owner().itemsToIds(items);
		if (existingIds.empty() || result.empty()) {
			return;
		}

		const auto error = [&] {
			for (const auto thread : result) {
				const auto error = GetErrorTextForSending(
					thread,
					{ .forward = &items, .text = &comment });
				if (!error.isEmpty()) {
					return std::make_pair(error, thread);
				}
			}
			return std::make_pair(QString(), result.front());
		}();
		if (!error.first.isEmpty()) {
			auto text = TextWithEntities();
			if (result.size() > 1) {
				text.append(
					Ui::Text::Bold(error.second->chatListName())
				).append("\n\n");
			}
			text.append(error.first);
			show->showBox(Ui::MakeInformBox(text));
			return;
		}

		using Flag = MTPmessages_ForwardMessages::Flag;
		const auto commonSendFlags = Flag(0)
			| Flag::f_with_my_score
			| (options.scheduled ? Flag::f_schedule_date : Flag(0))
			| ((forwardOptions != Data::ForwardOptions::PreserveInfo)
				? Flag::f_drop_author
				: Flag(0))
			| ((forwardOptions == Data::ForwardOptions::NoNamesAndCaptions)
				? Flag::f_drop_media_captions
				: Flag(0));
		auto mtpMsgIds = QVector<MTPint>();
		mtpMsgIds.reserve(existingIds.size());
		for (const auto &fullId : existingIds) {
			mtpMsgIds.push_back(MTP_int(fullId.msg));
		}
		const auto generateRandom = [&] {
			auto result = QVector<MTPlong>(existingIds.size());
			for (auto &value : result) {
				value = base::RandomValue<MTPlong>();
			}
			return result;
		};
		auto &api = history->owner().session().api();
		auto &histories = history->owner().histories();
		const auto donePhraseArgs = CreateForwardedMessagePhraseArgs(
			result,
			msgIds);
		const auto requestType = Data::Histories::RequestType::Send;
		for (const auto thread : result) {
			if (!comment.text.isEmpty()) {
				auto message = Api::MessageToSend(
					Api::SendAction(thread, options));
				message.textWithTags = comment;
				message.action.clearDraft = false;
				api.sendMessage(std::move(message));
			}
			const auto topicRootId = thread->topicRootId();
			const auto kGeneralId = Data::ForumTopic::kGeneralId;
			const auto topMsgId = (topicRootId == kGeneralId)
				? MsgId(0)
				: topicRootId;
			const auto peer = thread->peer();
			const auto threadHistory = thread->owningHistory();
			histories.sendRequest(threadHistory, requestType, [=](
					Fn<void()> finish) {
				const auto session = &threadHistory->session();
				auto &api = session->api();
				const auto sendFlags = commonSendFlags
					| (topMsgId ? Flag::f_top_msg_id : Flag(0))
					| (ShouldSendSilent(peer, options)
						? Flag::f_silent
						: Flag(0))
					| (options.shortcutId
						? Flag::f_quick_reply_shortcut
						: Flag(0));
				threadHistory->sendRequestId = api.request(
					MTPmessages_ForwardMessages(
						MTP_flags(sendFlags),
						history->peer->input,
						MTP_vector<MTPint>(mtpMsgIds),
						MTP_vector<MTPlong>(generateRandom()),
						peer->input,
						MTP_int(topMsgId),
						MTP_int(options.scheduled),
						MTP_inputPeerEmpty(), // send_as
						Data::ShortcutIdToMTP(session, options.shortcutId)
				)).done([=](const MTPUpdates &updates, mtpRequestId reqId) {
					threadHistory->session().api().applyUpdates(updates);
					state->requests.remove(reqId);
					if (state->requests.empty()) {
						if (show->valid()) {
							auto phrase = rpl::variable<TextWithEntities>(
								ChatHelpers::ForwardedMessagePhrase(
									donePhraseArgs)).current();
							show->showToast(std::move(phrase));
							show->hideLayer();
						}
					}
					finish();
				}).fail([=](const MTP::Error &error) {
					if (error.type() == u"VOICE_MESSAGES_FORBIDDEN"_q) {
						show->showToast(
							tr::lng_restricted_send_voice_messages(
								tr::now,
								lt_user,
								peer->name()));
					}
					finish();
				}).afterRequest(threadHistory->sendRequestId).send();
				return threadHistory->sendRequestId;
			});
			state->requests.insert(threadHistory->sendRequestId);
		}
	};
}

void FastShareMessage(
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item) {
	const auto show = controller->uiShow();
	const auto history = item->history();
	const auto owner = &history->owner();
	const auto session = &history->session();
	const auto msgIds = owner->itemOrItsGroup(item);
	const auto isGame = item->getMessageBot()
		&& item->media()
		&& (item->media()->game() != nullptr);
	const auto canCopyLink = item->hasDirectLink() || isGame;

	const auto items = owner->idsToItems(msgIds);
	const auto hasCaptions = ranges::any_of(items, [](auto item) {
		return item->media()
			&& !item->originalText().text.isEmpty()
			&& item->media()->allowsEditCaption();
	});
	const auto hasOnlyForcedForwardedInfo = hasCaptions
		? false
		: ranges::all_of(items, [](auto item) {
			return item->media() && item->media()->forceForwardedInfo();
		});

	auto copyCallback = [=] {
		const auto item = owner->message(msgIds[0]);
		if (!item) {
			return;
		}
		if (item->hasDirectLink()) {
			using namespace HistoryView;
			CopyPostLink(controller, item->fullId(), Context::History);
		} else if (const auto bot = item->getMessageBot()) {
			if (const auto media = item->media()) {
				if (const auto game = media->game()) {
					const auto link = session->createInternalLinkFull(
						bot->username() + u"?game="_q + game->shortName);

					QGuiApplication::clipboard()->setText(link);

					show->showToast(
						tr::lng_share_game_link_copied(tr::now));
				}
			}
		}
	};

	const auto requiredRight = item->requiredSendRight();
	const auto requiresInline = item->requiresSendInlineRight();
	auto filterCallback = [=](not_null<Data::Thread*> thread) {
		if (const auto user = thread->peer()->asUser()) {
			if (user->canSendIgnoreRequirePremium()) {
				return true;
			}
		}
		return Data::CanSend(thread, requiredRight)
			&& (!requiresInline
				|| Data::CanSend(thread, ChatRestriction::SendInline))
			&& (!isGame || !thread->peer()->isBroadcast());
	};
	auto copyLinkCallback = canCopyLink
		? Fn<void()>(std::move(copyCallback))
		: Fn<void()>();
	controller->show(
		Box<ShareBox>(ShareBox::Descriptor{
			.session = session,
			.copyCallback = std::move(copyLinkCallback),
			.submitCallback = ShareBox::DefaultForwardCallback(
				show,
				history,
				msgIds),
			.filterCallback = std::move(filterCallback),
			.forwardOptions = {
				.sendersCount = ItemsForwardSendersCount(items),
				.captionsCount = ItemsForwardCaptionsCount(items),
				.show = !hasOnlyForcedForwardedInfo,
			},
			.premiumRequiredError = SharePremiumRequiredError(),
		}),
		Ui::LayerOption::CloseOther);
}

auto SharePremiumRequiredError()
-> Fn<RecipientPremiumRequiredError(not_null<UserData*>)> {
	return WritePremiumRequiredError;
}

void ShareGameScoreByHash(
		not_null<Window::SessionController*> controller,
		const QString &hash) {
	auto &session = controller->session();
	auto key128Size = 0x10;

	auto hashEncrypted = QByteArray::fromBase64(hash.toLatin1(), QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
	if (hashEncrypted.size() <= key128Size || (hashEncrypted.size() != key128Size + 0x20)) {
		controller->show(
			Ui::MakeInformBox(tr::lng_confirm_phone_link_invalid()),
			Ui::LayerOption::CloseOther);
		return;
	}

	// Decrypt data.
	auto hashData = QByteArray(hashEncrypted.size() - key128Size, Qt::Uninitialized);
	if (!session.local().decrypt(hashEncrypted.constData() + key128Size, hashData.data(), hashEncrypted.size() - key128Size, hashEncrypted.constData())) {
		return;
	}

	// Count SHA1() of data.
	char dataSha1[20] = { 0 };
	hashSha1(hashData.constData(), hashData.size(), dataSha1);

	//// Mix out channel access hash from the first 64 bits of SHA1 of data.
	//auto channelAccessHash = *reinterpret_cast<uint64*>(hashEncrypted.data()) ^ *reinterpret_cast<uint64*>(dataSha1);

	//// Check next 64 bits of SHA1() of data.
	//auto skipSha1Part = sizeof(channelAccessHash);
	//if (memcmp(dataSha1 + skipSha1Part, hashEncrypted.constData() + skipSha1Part, key128Size - skipSha1Part) != 0) {
	//	Ui::show(Box<Ui::InformBox>(tr::lng_share_wrong_user(tr::now)));
	//	return;
	//}

	// Check 128 bits of SHA1() of data.
	if (memcmp(dataSha1, hashEncrypted.constData(), key128Size) != 0) {
		controller->show(
			Ui::MakeInformBox(tr::lng_share_wrong_user()),
			Ui::LayerOption::CloseOther);
		return;
	}

	auto hashDataInts = reinterpret_cast<uint64*>(hashData.data());
	if (hashDataInts[0] != session.userId().bare) {
		controller->show(
			Ui::MakeInformBox(tr::lng_share_wrong_user()),
			Ui::LayerOption::CloseOther);
		return;
	}

	const auto peerId = PeerId(hashDataInts[1]);
	const auto channelAccessHash = hashDataInts[3];
	if (!peerIsChannel(peerId) && channelAccessHash) {
		// If there is no channel id, there should be no channel access_hash.
		controller->show(
			Ui::MakeInformBox(tr::lng_share_wrong_user()),
			Ui::LayerOption::CloseOther);
		return;
	}

	const auto msgId = MsgId(int64(hashDataInts[2]));
	if (const auto item = session.data().message(peerId, msgId)) {
		FastShareMessage(controller, item);
	} else {
		const auto weak = base::make_weak(controller);
		const auto resolveMessageAndShareScore = crl::guard(weak, [=](
				PeerData *peer) {
			auto done = crl::guard(weak, [=] {
				const auto item = weak->session().data().message(
					peerId,
					msgId);
				if (item) {
					FastShareMessage(weak.get(), item);
				} else {
					weak->show(
						Ui::MakeInformBox(tr::lng_edit_deleted()),
						Ui::LayerOption::CloseOther);
				}
			});
			auto &api = weak->session().api();
			api.requestMessageData(peer, msgId, std::move(done));
		});

		const auto peer = peerIsChannel(peerId)
			? controller->session().data().peerLoaded(peerId)
			: nullptr;
		if (peer || !peerIsChannel(peerId)) {
			resolveMessageAndShareScore(peer);
		} else {
			const auto owner = &controller->session().data();
			controller->session().api().request(MTPchannels_GetChannels(
				MTP_vector<MTPInputChannel>(
					1,
					MTP_inputChannel(
						MTP_long(peerToChannel(peerId).bare),
						MTP_long(channelAccessHash)))
			)).done([=](const MTPmessages_Chats &result) {
				result.match([&](const auto &data) {
					owner->processChats(data.vchats());
				});
				if (const auto peer = owner->peerLoaded(peerId)) {
					resolveMessageAndShareScore(peer);
				}
			}).send();
		}
	}
}
