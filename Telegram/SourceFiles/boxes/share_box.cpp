/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/share_box.h"

#include "dialogs/dialogs_indexed_list.h"
#include "lang/lang_keys.h"
#include "mainwindow.h"
#include "mainwidget.h"
#include "base/qthelp_url.h"
#include "storage/storage_account.h"
#include "boxes/confirm_box.h"
#include "apiwrap.h"
#include "ui/toast/toast.h"
#include "ui/widgets/multi_select.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/input_fields.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/text/text_options.h"
#include "chat_helpers/message_field.h"
#include "chat_helpers/send_context_menu.h"
#include "history/history.h"
#include "history/history_message.h"
#include "history/view/history_view_schedule_box.h"
#include "window/window_session_controller.h"
#include "boxes/peer_list_box.h"
#include "chat_helpers/emoji_suggestions_widget.h"
#include "data/data_channel.h"
#include "data/data_user.h"
#include "data/data_session.h"
#include "data/data_folder.h"
#include "data/data_changes.h"
#include "main/main_session.h"
#include "core/application.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"

class ShareBox::Inner final : public Ui::RpWidget {
public:
	Inner(QWidget *parent, const Descriptor &descriptor);

	void setPeerSelectedChangedCallback(
		Fn<void(PeerData *peer, bool selected)> callback);
	void peerUnselected(not_null<PeerData*> peer);

	std::vector<not_null<PeerData*>> selected() const;
	bool hasSelected() const;

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
	void enterEventHook(QEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	struct Chat {
		Chat(
			PeerData *peer,
			const style::PeerListItem &st,
			Fn<void()> updateCallback);

		PeerData *peer;
		Ui::RoundImageCheckbox checkbox;
		Ui::Text::String name;
		Ui::Animations::Simple nameActive;
	};

	void invalidateCache();

	int displayedChatsCount() const;

	void paintChat(Painter &p, not_null<Chat*> chat, int index);
	void updateChat(not_null<PeerData*> peer);
	void updateChatName(not_null<Chat*> chat, not_null<PeerData*> peer);
	void repaintChat(not_null<PeerData*> peer);
	int chatIndex(not_null<PeerData*> peer) const;
	void repaintChatAtIndex(int index);
	Chat *getChatAtIndex(int index);

	void loadProfilePhotos(int yFrom);
	void changeCheckState(Chat *chat);
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

	std::unique_ptr<Dialogs::IndexedList> _chatsIndexed;
	QString _filter;
	std::vector<not_null<Dialogs::Row*>> _filtered;

	std::map<not_null<PeerData*>, std::unique_ptr<Chat>> _dataMap;
	base::flat_set<not_null<PeerData*>> _selected;

	Fn<void(PeerData *peer, bool selected)> _peerSelectedChangedCallback;

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
	using namespace rpl::mappers;

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

	connect(field, &Ui::InputField::submitted, [=] {
		submit({});
	});

	field->setInstantReplaces(Ui::InstantReplaces::Default());
	field->setInstantReplacesEnabled(
		Core::App().settings().replaceEmojiValue());
	field->setMarkdownReplacesEnabled(rpl::single(true));
	if (_descriptor.initEditLink) {
		_descriptor.initEditLink(field);
	} else if (_descriptor.navigation) {
		field->setEditLinkCallback(
			DefaultEditLinkCallback(
				_descriptor.navigation->parentController(),
				field));
	}
	field->setSubmitSettings(Core::App().settings().sendSubmitWay());

	if (_descriptor.initSpellchecker) {
		_descriptor.initSpellchecker(field);
	} else if (_descriptor.navigation) {
		InitSpellchecker(_descriptor.navigation->parentController(), field);
	}
	Ui::SendPendingMoveResizeEvents(_comment);
	if (_bottomWidget) {
		Ui::SendPendingMoveResizeEvents(_bottomWidget);
	}
}

void ShareBox::prepare() {
	prepareCommentField();

	_select->resizeToWidth(st::boxWideWidth);
	Ui::SendPendingMoveResizeEvents(_select);

	setTitle(tr::lng_share_title());

	_inner = setInnerWidget(
		object_ptr<Inner>(this, _descriptor),
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

	_inner->setPeerSelectedChangedCallback([=](PeerData *peer, bool checked) {
		innerSelectedChanged(peer, checked);
	});

	Ui::Emoji::SuggestionsController::Init(
		getDelegate()->outerContainer(),
		_comment->entity(),
		_descriptor.session);

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
	return ranges::all_of(selected, HistoryView::CanScheduleUntilOnline)
		? SendMenu::Type::ScheduledToUser
		: (selected.size() == 1 && selected.front()->isSelf())
		? SendMenu::Type::Reminder
		: SendMenu::Type::Scheduled;
}

void ShareBox::createButtons() {
	clearButtons();
	if (_hasSelected) {
		const auto send = addButton(tr::lng_share_confirm(), [=] {
			submit({});
		});
		SendMenu::SetupMenuAndShortcuts(
			send,
			[=] { return sendMenuType(); },
			[=] { submitSilent(); },
			[=] { submitScheduled(); });
	} else if (_descriptor.copyCallback) {
		addButton(_copyLinkText.value(), [=] { copyLink(); });
	}
	addButton(tr::lng_cancel(), [=] { closeBox(); });
}

void ShareBox::applyFilterUpdate(const QString &query) {
	onScrollToY(0);
	_inner->updateFilter(query);
}

void ShareBox::addPeerToMultiSelect(PeerData *peer, bool skipAnimation) {
	using AddItemWay = Ui::MultiSelect::AddItemWay;
	auto addItemWay = skipAnimation ? AddItemWay::SkipAnimation : AddItemWay::Default;
	_select->addItem(
		peer->id.value,
		peer->isSelf() ? tr::lng_saved_short(tr::now) : peer->shortName(),
		st::activeButtonBg,
		PaintUserpicCallback(peer, true),
		addItemWay);
}

void ShareBox::innerSelectedChanged(PeerData *peer, bool checked) {
	if (checked) {
		addPeerToMultiSelect(peer);
		_select->clearQuery();
	} else {
		_select->removeItem(peer->id.value);
	}
	selectedChanged();
	update();
}

void ShareBox::submit(Api::SendOptions options) {
	if (const auto onstack = _descriptor.submitCallback) {
		onstack(
			_inner->selected(),
			_comment->entity()->getTextWithAppliedMarkdown(),
			options);
	}
}

void ShareBox::submitSilent() {
	auto options = Api::SendOptions();
	options.silent = true;
	submit(options);
}

void ShareBox::submitScheduled() {
	const auto callback = [=](Api::SendOptions options) { submit(options); };
	Ui::show(
		HistoryView::PrepareScheduleBox(this, sendMenuType(), callback),
		Ui::LayerOption::KeepOther);
}

void ShareBox::copyLink() {
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
	onScrollToY(request.ymin, request.ymax);
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

ShareBox::Inner::Inner(QWidget *parent, const Descriptor &descriptor)
: RpWidget(parent)
, _descriptor(descriptor)
, _st(_descriptor.st ? *_descriptor.st : st::shareBoxList)
, _chatsIndexed(
	std::make_unique<Dialogs::IndexedList>(
		Dialogs::SortMode::Add)) {
	_rowsTop = st::shareRowsTop;
	_rowHeight = st::shareRowHeight;
	setAttribute(Qt::WA_OpaquePaintEvent);

	const auto self = _descriptor.session->user();
	if (_descriptor.filterCallback(self)) {
		_chatsIndexed->addToEnd(self->owner().history(self));
	}
	const auto addList = [&](not_null<Dialogs::IndexedList*> list) {
		for (const auto row : list->all()) {
			if (const auto history = row->history()) {
				if (!history->peer->isSelf()
					&& _descriptor.filterCallback(history->peer)) {
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

	_filter = qsl("a");
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

void ShareBox::Inner::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	loadProfilePhotos(visibleTop);
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
		updateChatName(i->second.get(), peer);
		repaintChat(peer);
	}
}

void ShareBox::Inner::updateChatName(
		not_null<Chat*> chat,
		not_null<PeerData*> peer) {
	const auto text = peer->isSelf()
		? tr::lng_saved_messages(tr::now)
		: peer->isRepliesChat()
		? tr::lng_replies_messages(tr::now)
		: peer->name;
	chat->name.setText(_st.item.nameStyle, text, Ui::NameTextOptions());
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
			return _chatsIndexed->rowAtY(index, 1);
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
		for (const auto row : _chatsIndexed->all()) {
			if (const auto history = row->history()) {
				if (history->peer == peer) {
					return index;
				}
			}
			++index;
		}
	} else {
		for (const auto row : _filtered) {
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

void ShareBox::Inner::loadProfilePhotos(int yFrom) {
	if (!parentWidget()) return;
	if (yFrom < 0) {
		yFrom = 0;
	}
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
			auto i = _chatsIndexed->cfind(yFrom, _rowHeight);
			for (auto end = _chatsIndexed->cend(); i != end; ++i) {
				if (((*i)->pos() * _rowHeight) >= yTo) {
					break;
				}
				(*i)->entry()->loadUserpic();
			}
		}
	} else if (!_filtered.empty()) {
		int from = yFrom / _rowHeight;
		if (from < 0) from = 0;
		if (from < _filtered.size()) {
			int to = (yTo / _rowHeight) + 1;
			if (to > _filtered.size()) to = _filtered.size();

			for (; from < to; ++from) {
				_filtered[from]->entry()->loadUserpic();
			}
		}
	}
}

auto ShareBox::Inner::getChat(not_null<Dialogs::Row*> row)
-> not_null<Chat*> {
	Expects(row->history() != nullptr);

	if (const auto data = static_cast<Chat*>(row->attached)) {
		return data;
	}
	const auto peer = row->history()->peer;
	if (const auto i = _dataMap.find(peer); i != end(_dataMap)) {
		row->attached = i->second.get();
		return i->second.get();
	}
	const auto [i, ok] = _dataMap.emplace(
		peer,
		std::make_unique<Chat>(peer, _st.item, [=] { repaintChat(peer); }));
	updateChatName(i->second.get(), peer);
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

	auto nameActive = chat->nameActive.value((index == _active) ? 1. : 0.);
	p.setPen(anim::pen(_st.item.nameFg, _st.item.nameFgChecked, nameActive));

	auto nameWidth = (_rowWidth - st::shareColumnSkip);
	auto nameLeft = st::shareColumnSkip / 2;
	auto nameTop = photoTop + _st.item.checkbox.imageRadius * 2 + st::shareNameTop;
	chat->name.drawLeftElided(p, x + nameLeft, y + nameTop, nameWidth, outerWidth, 2, style::al_top, 0, -1, 0, true);
}

ShareBox::Inner::Chat::Chat(
	PeerData *peer,
	const style::PeerListItem &st,
	Fn<void()> updateCallback)
: peer(peer)
, checkbox(st.checkbox, updateCallback, PaintUserpicCallback(peer, true))
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
			auto i = _chatsIndexed->cfind(indexFrom, 1);
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

void ShareBox::Inner::enterEventHook(QEvent *e) {
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
	if (!chat) return;

	if (!_filter.isEmpty()) {
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

	changePeerCheckState(chat, !chat->checkbox.checked());
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
		Fn<void(PeerData *peer, bool selected)> callback) {
	_peerSelectedChangedCallback = std::move(callback);
}

void ShareBox::Inner::changePeerCheckState(
		not_null<Chat*> chat,
		bool checked,
		ChangeStateWay useCallback) {
	chat->checkbox.setChecked(checked);
	if (checked) {
		_selected.insert(chat->peer);
		setActive(chatIndex(chat->peer));
	} else {
		_selected.remove(chat->peer);
	}
	if (useCallback != ChangeStateWay::SkipCallback
		&& _peerSelectedChangedCallback) {
		_peerSelectedChangedCallback(chat->peer, checked);
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
		update();
		loadProfilePhotos(0);
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
				const auto history = _descriptor.session->data().historyLoaded(peer);
				if (!_descriptor.filterCallback(peer)) {
					continue;
				} else if (history && _chatsIndexed->getRow(history)) {
					continue;
				} else if (base::contains(_byUsernameFiltered, peer)) {
					continue;
				}
				_byUsernameFiltered.push_back(peer);
				d_byUsernameFiltered.push_back(std::make_unique<Chat>(
					peer,
					_st.item,
					[=] { repaintChat(peer); }));
				updateChatName(d_byUsernameFiltered.back().get(), peer);
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
	update();
}

std::vector<not_null<PeerData*>> ShareBox::Inner::selected() const {
	auto result = std::vector<not_null<PeerData*>>();
	result.reserve(_dataMap.size());
	for (const auto &[peer, chat] : _dataMap) {
		if (chat->checkbox.checked()) {
			result.push_back(peer);
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
	auto channel = fullId.channel
		? session->data().channelLoaded(fullId.channel)
		: static_cast<ChannelData*>(nullptr);
	auto channelAccessHash = uint64(channel ? channel->access : 0);
	shareHashDataInts[0] = session->userId().bare;
	shareHashDataInts[1] = fullId.channel.bare;
	shareHashDataInts[2] = fullId.msg;
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
	auto shareUrl = qsl("tg://share_game_score?hash=") + QString::fromLatin1(shareHash);

	auto shareComponent = qsl("tgShareScoreUrl=") + qthelp::url_encode(shareUrl);

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

void ShareGameScoreByHash(
		not_null<Main::Session*> session,
		const QString &hash) {
	auto key128Size = 0x10;

	auto hashEncrypted = QByteArray::fromBase64(hash.toLatin1(), QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
	if (hashEncrypted.size() <= key128Size || (hashEncrypted.size() != key128Size + 0x20)) {
		Ui::show(Box<InformBox>(tr::lng_confirm_phone_link_invalid(tr::now)));
		return;
	}

	// Decrypt data.
	auto hashData = QByteArray(hashEncrypted.size() - key128Size, Qt::Uninitialized);
	if (!session->local().decrypt(hashEncrypted.constData() + key128Size, hashData.data(), hashEncrypted.size() - key128Size, hashEncrypted.constData())) {
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
	//	Ui::show(Box<InformBox>(tr::lng_share_wrong_user(tr::now)));
	//	return;
	//}

	// Check 128 bits of SHA1() of data.
	if (memcmp(dataSha1, hashEncrypted.constData(), key128Size) != 0) {
		Ui::show(Box<InformBox>(tr::lng_share_wrong_user(tr::now)));
		return;
	}

	auto hashDataInts = reinterpret_cast<uint64*>(hashData.data());
	if (hashDataInts[0] != session->userId().bare) {
		Ui::show(Box<InformBox>(tr::lng_share_wrong_user(tr::now)));
		return;
	}

	// Check first 32 bits of channel access hash.
	auto channelAccessHash = hashDataInts[3];
	//auto channelAccessHashInts = reinterpret_cast<int32*>(&channelAccessHash);
	//if (channelAccessHashInts[0] != hashDataInts[3]) {
	//	Ui::show(Box<InformBox>(tr::lng_share_wrong_user(tr::now)));
	//	return;
	//}

	if (((hashDataInts[1] >> 40) != 0)
		|| ((hashDataInts[2] >> 32) != 0)
		|| (!hashDataInts[1] && channelAccessHash)) {
		// If there is no channel id, there should be no channel access_hash.
		Ui::show(Box<InformBox>(tr::lng_share_wrong_user(tr::now)));
		return;
	}

	auto channelId = ChannelId(hashDataInts[1]);
	auto msgId = MsgId(hashDataInts[2]);
	if (const auto item = session->data().message(channelId, msgId)) {
		FastShareMessage(item);
	} else {
		auto resolveMessageAndShareScore = [=](ChannelData *channel) {
			session->api().requestMessageData(channel, msgId, [=](
					ChannelData *channel,
					MsgId msgId) {
				if (const auto item = session->data().message(channel, msgId)) {
					FastShareMessage(item);
				} else {
					Ui::show(Box<InformBox>(tr::lng_edit_deleted(tr::now)));
				}
			});
		};

		const auto channel = channelId
			? session->data().channelLoaded(channelId)
			: nullptr;
		if (channel || !channelId) {
			resolveMessageAndShareScore(channel);
		} else {
			session->api().request(MTPchannels_GetChannels(
				MTP_vector<MTPInputChannel>(
					1,
					MTP_inputChannel(
						MTP_int(channelId.bare), // #TODO ids
						MTP_long(channelAccessHash)))
			)).done([=](const MTPmessages_Chats &result) {
				result.match([&](const auto &data) {
					session->data().processChats(data.vchats());
				});
				if (const auto channel = session->data().channelLoaded(channelId)) {
					resolveMessageAndShareScore(channel);
				}
			}).send();
		}
	}
}
