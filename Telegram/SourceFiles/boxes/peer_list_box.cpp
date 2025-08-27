/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peer_list_box.h"

#include "history/history.h" // chatListNameSortKey.
#include "main/session/session_show.h"
#include "main/main_session.h"
#include "mainwidget.h"
#include "ui/effects/loading_element.h"
#include "ui/effects/outline_segments.h"
#include "ui/effects/round_checkbox.h"
#include "ui/effects/ripple_animation.h"
#include "ui/widgets/multi_select.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/popup_menu.h"
#include "ui/empty_userpic.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/text/text_options.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "lang/lang_keys.h"
#include "storage/file_download.h"
#include "data/data_peer_values.h"
#include "data/data_chat.h"
#include "data/data_session.h"
#include "data/data_changes.h"
#include "base/unixtime.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "styles/style_dialogs.h"
#include "styles/style_widgets.h"

#include <xxhash.h> // XXH64.
#include <QtWidgets/QApplication>

[[nodiscard]] PeerListRowId UniqueRowIdFromString(const QString &d) {
	return XXH64(d.data(), d.size() * sizeof(ushort), 0);
}

PaintRoundImageCallback PaintUserpicCallback(
		not_null<PeerData*> peer,
		bool respectSavedMessagesChat) {
	if (respectSavedMessagesChat) {
		if (peer->isSelf()) {
			return [](QPainter &p, int x, int y, int outerWidth, int size) {
				Ui::EmptyUserpic::PaintSavedMessages(p, x, y, outerWidth, size);
			};
		} else if (peer->isRepliesChat()) {
			return [](QPainter &p, int x, int y, int outerWidth, int size) {
				Ui::EmptyUserpic::PaintRepliesMessages(p, x, y, outerWidth, size);
			};
		}
	}
	auto userpic = Ui::PeerUserpicView();
	return [=](Painter &p, int x, int y, int outerWidth, int size) mutable {
		peer->paintUserpicLeft(p, userpic, x, y, outerWidth, size);
	};
}

PaintRoundImageCallback ForceRoundUserpicCallback(not_null<PeerData*> peer) {
	auto userpic = Ui::PeerUserpicView();
	auto cache = std::make_shared<QImage>();
	return [=](Painter &p, int x, int y, int outerWidth, int size) mutable {
		const auto ratio = style::DevicePixelRatio();
		const auto cacheSize = QSize(size, size) * ratio;
		if (cache->size() != cacheSize) {
			*cache = QImage(cacheSize, QImage::Format_ARGB32_Premultiplied);
			cache->setDevicePixelRatio(ratio);
		}
		auto q = Painter(cache.get());
		peer->paintUserpicLeft(q, userpic, 0, 0, outerWidth, size);
		q.end();

		*cache = Images::Circle(std::move(*cache));
		p.drawImage(x, y, *cache);
	};
}

PeerListContentDelegateShow::PeerListContentDelegateShow(
	std::shared_ptr<Main::SessionShow> show)
: _show(show) {
}

auto PeerListContentDelegateShow::peerListUiShow()
-> std::shared_ptr<Main::SessionShow>{
	return _show;
}

PeerListBox::PeerListBox(
	QWidget*,
	std::unique_ptr<PeerListController> controller,
	Fn<void(not_null<PeerListBox*>)> init)
: _show(Main::MakeSessionShow(uiShow(), &controller->session()))
, _controller(std::move(controller))
, _init(std::move(init)) {
	Expects(_controller != nullptr);
}

void PeerListBox::createMultiSelect() {
	Expects(_select == nullptr);

	auto entity = object_ptr<Ui::MultiSelect>(
		this,
		(_controller->selectSt()
			? *_controller->selectSt()
			: st::defaultMultiSelect),
		tr::lng_participant_filter());
	_select.create(this, std::move(entity));
	_select->heightValue(
	) | rpl::start_with_next(
		[this] { updateScrollSkips(); },
		lifetime());
	_select->entity()->setSubmittedCallback([=](Qt::KeyboardModifiers) {
		content()->submitted();
	});
	_select->entity()->setQueryChangedCallback([=](const QString &query) {
		if (_customQueryChangedCallback) {
			_customQueryChangedCallback(query);
		}
		searchQueryChanged(query);
	});
	_select->entity()->setItemRemovedCallback([=](uint64 itemId) {
		if (_controller->handleDeselectForeignRow(itemId)) {
			return;
		}
		if (const auto peer = _controller->session().data().peerLoaded(PeerId(itemId))) {
			if (const auto row = peerListFindRow(itemId)) {
				content()->changeCheckState(row, false, anim::type::normal);
				update();
			}
			_controller->itemDeselectedHook(peer);
		}
	});
	_select->resizeToWidth(_controller->contentWidth());
	_select->moveToLeft(0, topSelectSkip());
}

void PeerListBox::appendQueryChangedCallback(Fn<void(QString)> callback) {
	_customQueryChangedCallback = std::move(callback);
}

void PeerListBox::setAddedTopScrollSkip(int skip, bool aboveSearch) {
	_addedTopScrollSkip = skip;
	_addedTopScrollAboveSearch = aboveSearch;
	_scrollBottomFixed = false;
	updateScrollSkips();
}

void PeerListBox::showFinished() {
	_controller->showFinished();
}

int PeerListBox::topScrollSkip() const {
	auto result = _addedTopScrollSkip;
	if (_select && !_select->isHidden()) {
		result += _select->height();
	}
	return result;
}

int PeerListBox::topSelectSkip() const {
	return _addedTopScrollAboveSearch ? _addedTopScrollSkip : 0;
}

void PeerListBox::updateScrollSkips() {
	// If we show / hide the search field scroll top is fixed.
	// If we resize search field by bubbles scroll bottom is fixed.
	setInnerTopSkip(topScrollSkip(), _scrollBottomFixed);
	if (_select) {
		_select->moveToLeft(0, topSelectSkip());
		if (!_select->animating()) {
			_scrollBottomFixed = true;
		}
	}
}

void PeerListBox::prepare() {
	setContent(setInnerWidget(
		object_ptr<PeerListContent>(
			this,
			_controller.get()),
		st::boxScroll));
	content()->resizeToWidth(_controller->contentWidth());

	_controller->setDelegate(this);

	_controller->boxHeightValue(
	) | rpl::start_with_next([=](int height) {
		setDimensions(_controller->contentWidth(), height);
	}, lifetime());

	if (_select) {
		_select->finishAnimating();
		Ui::SendPendingMoveResizeEvents(_select);
		_scrollBottomFixed = true;
		scrollToY(0);
	}

	content()->scrollToRequests(
	) | rpl::start_with_next([this](Ui::ScrollToRequest request) {
		scrollToY(request.ymin, request.ymax);
	}, lifetime());

	if (_init) {
		_init(this);
	}
}

void PeerListBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Down) {
		content()->selectSkip(1);
	} else if (e->key() == Qt::Key_Up) {
		content()->selectSkip(-1);
	} else if (e->key() == Qt::Key_PageDown) {
		content()->selectSkipPage(height(), 1);
	} else if (e->key() == Qt::Key_PageUp) {
		content()->selectSkipPage(height(), -1);
	} else if (e->key() == Qt::Key_Escape
			&& _select
			&& !_select->entity()->getQuery().isEmpty()) {
		_select->entity()->clearQuery();
	} else {
		BoxContent::keyPressEvent(e);
	}
}

void PeerListBox::searchQueryChanged(const QString &query) {
	scrollToY(0);
	content()->searchQueryChanged(query);
}

void PeerListBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	if (_select) {
		_select->resizeToWidth(width());
		updateScrollSkips();
	}

	content()->resizeToWidth(width());
}

void PeerListBox::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	const auto &bg = _controller->computeListSt().bg;
	const auto fill = QRect(
		0,
		_addedTopScrollSkip,
		width(),
		height() - _addedTopScrollSkip);
	for (const auto &rect : e->region()) {
		if (const auto part = rect.intersected(fill); !part.isEmpty()) {
			p.fillRect(part, bg);
		}
	}
}

void PeerListBox::setInnerFocus() {
	if (!_select || !_select->toggled()) {
		content()->setFocus();
	} else {
		_select->entity()->setInnerFocus();
	}
}

void PeerListBox::peerListSetRowChecked(
		not_null<PeerListRow*> row,
		bool checked) {
	if (checked) {
		if (_controller->trackSelectedList()) {
			addSelectItem(row, anim::type::normal);
		}
		PeerListContentDelegate::peerListSetRowChecked(row, checked);
		peerListUpdateRow(row);

		// This call deletes row from _searchRows.
		if (_select) {
			_select->entity()->clearQuery();
		}
	} else {
		// The itemRemovedCallback will call changeCheckState() here.
		if (_select) {
			_select->entity()->removeItem(row->id());
		} else {
			PeerListContentDelegate::peerListSetRowChecked(row, checked);
		}
		peerListUpdateRow(row);
	}
}

void PeerListBox::peerListSetForeignRowChecked(
		not_null<PeerListRow*> row,
		bool checked,
		anim::type animated) {
	if (checked) {
		addSelectItem(row, animated);

		// This call deletes row from _searchRows.
		_select->entity()->clearQuery();
	} else {
		// The itemRemovedCallback will call changeCheckState() here.
		_select->entity()->removeItem(row->id());
	}
}

void PeerListBox::peerListScrollToTop() {
	scrollToY(0);
}

void PeerListBox::peerListSetSearchMode(PeerListSearchMode mode) {
	PeerListContentDelegate::peerListSetSearchMode(mode);

	auto selectVisible = (mode != PeerListSearchMode::Disabled);
	if (selectVisible && !_select) {
		createMultiSelect();
		_select->toggle(!selectVisible, anim::type::instant);
	}
	if (_select) {
		_select->toggle(selectVisible, anim::type::normal);
		_scrollBottomFixed = false;
		setInnerFocus();
	}
}

std::shared_ptr<Main::SessionShow> PeerListBox::peerListUiShow() {
	return _show;
}

PeerListController::PeerListController(
	std::unique_ptr<PeerListSearchController> searchController)
: _searchController(std::move(searchController)) {
	if (_searchController) {
		_searchController->setDelegate(this);
	}
}

const style::PeerList &PeerListController::computeListSt() const {
	return _listSt ? *_listSt : st::peerListBox;
}

const style::MultiSelect &PeerListController::computeSelectSt() const {
	return _selectSt ? *_selectSt : st::defaultMultiSelect;
}

bool PeerListController::hasComplexSearch() const {
	return (_searchController != nullptr);
}

void PeerListController::search(const QString &query) {
	Expects(hasComplexSearch());

	_searchController->searchQuery(query);
}

void PeerListController::peerListSearchAddRow(not_null<PeerData*> peer) {
	if (auto row = delegate()->peerListFindRow(peer->id.value)) {
		Assert(row->id() == row->peer()->id.value);
		delegate()->peerListAppendFoundRow(row);
	} else if (auto row = createSearchRow(peer)) {
		Assert(row->id() == row->peer()->id.value);
		delegate()->peerListAppendSearchRow(std::move(row));
	}
}

void PeerListController::peerListSearchAddRow(PeerListRowId id) {
	if (auto row = delegate()->peerListFindRow(id)) {
		delegate()->peerListAppendFoundRow(row);
	} else if (auto row = createSearchRow(id)) {
		delegate()->peerListAppendSearchRow(std::move(row));
	}
}

void PeerListController::peerListSearchRefreshRows() {
	delegate()->peerListRefreshRows();
}

void PeerListController::setDescriptionText(const QString &text) {
	if (text.isEmpty()) {
		setDescription(nullptr);
	} else {
		setDescription(object_ptr<Ui::FlatLabel>(nullptr, text, computeListSt().about));
	}
}

void PeerListController::setSearchNoResultsText(const QString &text) {
	if (text.isEmpty()) {
		setSearchNoResults(nullptr);
	} else {
		setSearchNoResults(
			object_ptr<Ui::FlatLabel>(nullptr, text, st::membersAbout));
	}
}

void PeerListController::sortByName() {
	auto keys = base::flat_map<PeerListRowId, QString>();
	keys.reserve(delegate()->peerListFullRowsCount());
	const auto key = [&](const PeerListRow &row) {
		const auto id = row.id();
		const auto i = keys.find(id);
		if (i != end(keys)) {
			return i->second;
		}
		const auto peer = row.peer();
		const auto history = peer->owner().history(peer);
		return keys.emplace(
			id,
			history->chatListNameSortKey()).first->second;
	};
	const auto predicate = [&](const PeerListRow &a, const PeerListRow &b) {
		return (key(a).compare(key(b)) < 0);
	};
	delegate()->peerListSortRows(predicate);
}

base::unique_qptr<Ui::PopupMenu> PeerListController::rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) {
	return nullptr;
}

std::unique_ptr<PeerListRow> PeerListController::createSearchRow(
		PeerListRowId id) {
	if (const auto peer = session().data().peerLoaded(PeerId(id))) {
		return createSearchRow(peer);
	}
	return nullptr;
}

std::unique_ptr<PeerListState> PeerListController::saveState() const {
	return delegate()->peerListSaveState();
}

void PeerListController::restoreState(
		std::unique_ptr<PeerListState> state) {
	delegate()->peerListRestoreState(std::move(state));
}

int PeerListController::contentWidth() const {
	return st::boxWideWidth;
}

rpl::producer<int> PeerListController::boxHeightValue() const {
	return rpl::single(st::boxMaxListHeight);
}

int PeerListController::descriptionTopSkipMin() const {
	return computeListSt().item.height;
}

void PeerListBox::addSelectItem(
		not_null<PeerData*> peer,
		anim::type animated) {
	const auto respect = !_controller->savedMessagesChatStatus().isEmpty();
	const auto text = (respect && peer->isSelf())
		? tr::lng_saved_short(tr::now)
		: (respect && peer->isRepliesChat())
		? tr::lng_replies_messages(tr::now)
		: (respect && peer->isVerifyCodes())
		? tr::lng_verification_codes(tr::now)
		: peer->shortName();
	addSelectItem(
		peer->id.value,
		text,
		(peer->isForum()
			? ForceRoundUserpicCallback(peer)
			: PaintUserpicCallback(peer, respect)),
		animated);
}

void PeerListBox::addSelectItem(
		not_null<PeerListRow*> row,
		anim::type animated) {
	addSelectItem(
		row->id(),
		row->generateShortName(),
		row->generatePaintUserpicCallback(true),
		animated);
}

void PeerListBox::addSelectItem(
		uint64 itemId,
		const QString &text,
		PaintRoundImageCallback paintUserpic,
		anim::type animated) {
	if (!_select) {
		createMultiSelect();
		_select->hide(anim::type::instant);
	}
	const auto &activeBg = (_controller->selectSt()
		? *_controller->selectSt()
		: st::defaultMultiSelect).item.textActiveBg;
	if (animated == anim::type::instant) {
		_select->entity()->addItemInBunch(
			itemId,
			text,
			activeBg,
			std::move(paintUserpic));
	} else {
		_select->entity()->addItem(
			itemId,
			text,
			activeBg,
			std::move(paintUserpic));
	}
}

void PeerListBox::peerListFinishSelectedRowsBunch() {
	Expects(_select != nullptr);

	_select->entity()->finishItemsBunch();
}

bool PeerListBox::peerListIsRowChecked(not_null<PeerListRow*> row) {
	return _select ? _select->entity()->hasItem(row->id()) : false;
}

int PeerListBox::peerListSelectedRowsCount() {
	return _select ? _select->entity()->getItemsCount() : 0;
}

std::vector<PeerListRowId> PeerListBox::collectSelectedIds() {
	auto result = std::vector<PeerListRowId>();
	auto items = _select
		? _select->entity()->getItems()
		: QVector<uint64>();
	if (!items.empty()) {
		result.reserve(items.size());
		for (const auto itemId : items) {
			if (!_controller->isForeignRow(itemId)) {
				result.push_back(itemId);
			}
		}
	}
	return result;
}

auto PeerListBox::collectSelectedRows()
-> std::vector<not_null<PeerData*>> {
	auto result = std::vector<not_null<PeerData*>>();
	auto items = _select
		? _select->entity()->getItems()
		: QVector<uint64>();
	if (!items.empty()) {
		result.reserve(items.size());
		for (const auto itemId : items) {
			if (!_controller->isForeignRow(itemId)) {
				result.push_back(_controller->session().data().peer(PeerId(itemId)));
			}
		}
	}
	return result;
}

rpl::producer<int> PeerListBox::multiSelectHeightValue() const {
	return _select ? _select->heightValue() : rpl::single(0);
}

rpl::producer<> PeerListBox::noSearchSubmits() const {
	return content()->noSearchSubmits();
}

PeerListRow::PeerListRow(not_null<PeerData*> peer)
: PeerListRow(peer, peer->id.value) {
}

PeerListRow::PeerListRow(not_null<PeerData*> peer, PeerListRowId id)
: _id(id)
, _peer(peer) {
}

PeerListRow::PeerListRow(PeerListRowId id)
: _id(id) {
}

PeerListRow::~PeerListRow() = default;

bool PeerListRow::checked() const {
	return _checkbox && _checkbox->checked();
}

void PeerListRow::preloadUserpic() {
	if (_peer) {
		_peer->loadUserpic();
	}
}

void PeerListRow::setCustomStatus(const QString &status, bool active) {
	setStatusText(status);
	_statusType = active ? StatusType::CustomActive : StatusType::Custom;
	_statusValidTill = 0;
}

void PeerListRow::clearCustomStatus() {
	_statusType = StatusType::Online;
	refreshStatus();
}

void PeerListRow::refreshStatus() {
	if (!_initialized
		|| special()
		|| _statusType == StatusType::Custom
		|| _statusType == StatusType::CustomActive) {
		return;
	}
	_statusType = StatusType::LastSeen;
	_statusValidTill = 0;
	if (auto user = peer()->asUser()) {
		if (!_savedMessagesStatus.isEmpty()) {
			setStatusText(_savedMessagesStatus);
		} else {
			auto time = base::unixtime::now();
			setStatusText(Data::OnlineText(user, time));
			if (Data::OnlineTextActive(user, time)) {
				_statusType = StatusType::Online;
			}
			_statusValidTill = crl::now()
				+ Data::OnlineChangeTimeout(user, time);
		}
	} else if (auto chat = peer()->asChat()) {
		if (!chat->amIn()) {
			setStatusText(tr::lng_chat_status_unaccessible(tr::now));
		} else if (chat->count > 0) {
			setStatusText(tr::lng_chat_status_members(tr::now, lt_count_decimal, chat->count));
		} else {
			setStatusText(tr::lng_group_status(tr::now));
		}
	} else if (peer()->isMegagroup()) {
		setStatusText(tr::lng_group_status(tr::now));
	} else if (peer()->isChannel()) {
		setStatusText(tr::lng_channel_status(tr::now));
	}
}

crl::time PeerListRow::refreshStatusTime() const {
	return _statusValidTill;
}

void PeerListRow::refreshName(const style::PeerListItem &st) {
	if (!_initialized) {
		return;
	}
	const auto text = !_savedMessagesStatus.isEmpty()
		? tr::lng_saved_messages(tr::now)
		: _isRepliesMessagesChat
		? tr::lng_replies_messages(tr::now)
		: _isVerifyCodesChat
		? tr::lng_verification_codes(tr::now)
		: generateName();
	_name.setText(st.nameStyle, text, Ui::NameTextOptions());
}

int PeerListRow::elementsCount() const {
	return 1;
}

QRect PeerListRow::elementGeometry(int element, int outerWidth) const {
	if (element != 1) {
		return QRect();
	}
	const auto size = rightActionSize();
	if (size.isEmpty()) {
		return QRect();
	}
	const auto margins = rightActionMargins();
	const auto right = margins.right();
	const auto top = margins.top();
	const auto left = outerWidth - right - size.width();
	return QRect(QPoint(left, top), size);
}

bool PeerListRow::elementDisabled(int element) const {
	return (element == 1) && rightActionDisabled();
}

bool PeerListRow::elementOnlySelect(int element) const {
	return false;
}

void PeerListRow::elementAddRipple(
		int element,
		QPoint point,
		Fn<void()> updateCallback) {
	if (element == 1) {
		rightActionAddRipple(point, std::move(updateCallback));
	}
}

void PeerListRow::elementsStopLastRipple() {
	rightActionStopLastRipple();
}

void PeerListRow::elementsPaint(
		Painter &p,
		int outerWidth,
		bool selected,
		int selectedElement) {
	const auto geometry = elementGeometry(1, outerWidth);
	if (!geometry.isEmpty()) {
		rightActionPaint(
			p,
			geometry.x(),
			geometry.y(),
			outerWidth,
			selected,
			(selectedElement == 1));
	}
}

QString PeerListRow::generateName() {
	return peer()->userpicPaintingPeer()->name();
}

QString PeerListRow::generateShortName() {
	return !_savedMessagesStatus.isEmpty()
		? tr::lng_saved_short(tr::now)
		: _isRepliesMessagesChat
		? tr::lng_replies_messages(tr::now)
		: _isVerifyCodesChat
		? tr::lng_verification_codes(tr::now)
		: peer()->userpicPaintingPeer()->shortName();
}

Ui::PeerUserpicView &PeerListRow::ensureUserpicView() {
	if (!_userpic.cloud && peer()->userpicPaintingPeer()->hasUserpic()) {
		_userpic = peer()->userpicPaintingPeer()->createUserpicView();
	}
	return _userpic;
}

PaintRoundImageCallback PeerListRow::generatePaintUserpicCallback(
		bool forceRound) {
	const auto saved = !_savedMessagesStatus.isEmpty();
	const auto replies = _isRepliesMessagesChat;
	const auto peer = this->peer()->userpicPaintingPeer();
	auto userpic = saved ? Ui::PeerUserpicView() : ensureUserpicView();
	if (forceRound && peer->isForum()) {
		return ForceRoundUserpicCallback(peer);
	}
	return [=](Painter &p, int x, int y, int outerWidth, int size) mutable {
		using namespace Ui;
		if (saved) {
			EmptyUserpic::PaintSavedMessages(p, x, y, outerWidth, size);
		} else if (replies) {
			EmptyUserpic::PaintRepliesMessages(p, x, y, outerWidth, size);
		} else {
			peer->paintUserpicLeft(p, userpic, x, y, outerWidth, size);
		}
	};
}


auto PeerListRow::generateNameFirstLetters() const
-> const base::flat_set<QChar> & {
	return peer()->nameFirstLetters();
}

auto PeerListRow::generateNameWords() const
-> const base::flat_set<QString> & {
	return peer()->nameWords();
}

const style::PeerListItem &PeerListRow::computeSt(
		const style::PeerListItem &st) const {
	return st;
}

void PeerListRow::invalidatePixmapsCache() {
	if (_checkbox) {
		_checkbox->invalidateCache();
	}
}

int PeerListRow::paintNameIconGetWidth(
		Painter &p,
		Fn<void()> repaint,
		crl::time now,
		int nameLeft,
		int nameTop,
		int nameWidth,
		int availableWidth,
		int outerWidth,
		bool selected) {
	if (_skipPeerBadge
		|| special()
		|| !_savedMessagesStatus.isEmpty()
		|| _isRepliesMessagesChat
		|| _isVerifyCodesChat) {
		return 0;
	}
	return _badge.drawGetWidth(p, {
		.peer = peer(),
		.rectForName = QRect(
			nameLeft,
			nameTop,
			availableWidth,
			st::semiboldFont->height),
		.nameWidth = nameWidth,
		.outerWidth = outerWidth,
		.verified = &(selected
			? st::dialogsVerifiedIconOver
			: st::dialogsVerifiedIcon),
		.premium = &(selected
			? st::dialogsPremiumIcon.over
			: st::dialogsPremiumIcon.icon),
		.scam = &(selected ? st::dialogsScamFgOver : st::dialogsScamFg),
		.direct = &(selected
			? st::windowSubTextFgOver
			: st::windowSubTextFg),
		.premiumFg = &(selected
			? st::dialogsVerifiedIconBgOver
			: st::dialogsVerifiedIconBg),
		.customEmojiRepaint = repaint,
		.now = now,
		.paused = false,
	});
}

void PeerListRow::paintStatusText(
		Painter &p,
		const style::PeerListItem &st,
		int x,
		int y,
		int availableWidth,
		int outerWidth,
		bool selected) {
	auto statusHasOnlineColor = (_statusType == PeerListRow::StatusType::Online)
		|| (_statusType == PeerListRow::StatusType::CustomActive);
	p.setFont(st::contactsStatusFont);
	p.setPen(statusHasOnlineColor ? st.statusFgActive : (selected ? st.statusFgOver : st.statusFg));
	_status.drawLeftElided(p, x, y, availableWidth, outerWidth);
}

template <typename MaskGenerator, typename UpdateCallback>
void PeerListRow::addRipple(const style::PeerListItem &st, MaskGenerator &&maskGenerator, QPoint point, UpdateCallback &&updateCallback) {
	if (!_ripple) {
		auto mask = maskGenerator();
		if (mask.isNull()) {
			return;
		}
		_ripple = std::make_unique<Ui::RippleAnimation>(st.button.ripple, std::move(mask), std::forward<UpdateCallback>(updateCallback));
	}
	_ripple->add(point);
}

void PeerListRow::stopLastRipple() {
	if (_ripple) {
		_ripple->lastStop();
	}
}

void PeerListRow::paintRipple(
		Painter &p,
		const style::PeerListItem &st,
		int x,
		int y,
		int outerWidth) {
	if (_ripple) {
		_ripple->paint(p, x, y, outerWidth, &st.button.ripple.color->c);
		if (_ripple->empty()) {
			_ripple.reset();
		}
	}
}

void PeerListRow::paintUserpic(
		Painter &p,
		const style::PeerListItem &st,
		int x,
		int y,
		int outerWidth) {
	if (_disabledState == State::DisabledChecked) {
		paintDisabledCheckUserpic(p, st, x, y, outerWidth);
	} else if (_checkbox) {
		_checkbox->paint(p, x, y, outerWidth);
	} else if (const auto callback = generatePaintUserpicCallback(false)) {
		callback(p, x, y, outerWidth, st.photoSize);
	}
	paintUserpicOverlay(p, st, x, y, outerWidth);
}

// Emulates Ui::RoundImageCheckbox::paint() in a checked state.
void PeerListRow::paintDisabledCheckUserpic(
		Painter &p,
		const style::PeerListItem &st,
		int x,
		int y,
		int outerWidth) const {
	auto userpicRadius = st.checkbox.imageSmallRadius;
	auto userpicShift = st.checkbox.imageRadius - userpicRadius;
	auto userpicDiameter = st.checkbox.imageRadius * 2;
	auto userpicLeft = x + userpicShift;
	auto userpicTop = y + userpicShift;
	auto userpicEllipse = style::rtlrect(x, y, userpicDiameter, userpicDiameter, outerWidth);
	auto userpicBorderPen = st.disabledCheckFg->p;
	userpicBorderPen.setWidth(st.checkbox.selectWidth);

	auto iconDiameter = st.checkbox.check.size;
	auto iconLeft = x + userpicDiameter + st.checkbox.selectWidth - iconDiameter;
	auto iconTop = y + userpicDiameter + st.checkbox.selectWidth - iconDiameter;
	auto iconEllipse = style::rtlrect(iconLeft, iconTop, iconDiameter, iconDiameter, outerWidth);
	auto iconBorderPen = st.checkbox.check.border->p;
	iconBorderPen.setWidth(st.checkbox.selectWidth);

	const auto size = userpicRadius * 2;
	if (!_savedMessagesStatus.isEmpty()) {
		Ui::EmptyUserpic::PaintSavedMessages(p, userpicLeft, userpicTop, outerWidth, size);
	} else if (_isRepliesMessagesChat) {
		Ui::EmptyUserpic::PaintRepliesMessages(p, userpicLeft, userpicTop, outerWidth, size);
	} else {
		peer()->paintUserpicLeft(p, _userpic, userpicLeft, userpicTop, outerWidth, size);
	}

	{
		PainterHighQualityEnabler hq(p);

		p.setPen(userpicBorderPen);
		p.setBrush(Qt::NoBrush);
		if (peer()->forum()) {
			const auto radius = userpicDiameter
				* Ui::ForumUserpicRadiusMultiplier();
			p.drawRoundedRect(userpicEllipse, radius, radius);
		} else {
			p.drawEllipse(userpicEllipse);
		}

		p.setPen(iconBorderPen);
		p.setBrush(st.disabledCheckFg);
		p.drawEllipse(iconEllipse);
	}

	st.checkbox.check.check.paint(p, iconEllipse.topLeft(), outerWidth);
}

void PeerListRow::setStatusText(const QString &text) {
	_status.setText(st::defaultTextStyle, text, Ui::NameTextOptions());
}

float64 PeerListRow::checkedRatio() {
	return _checkbox ? _checkbox->checkedAnimationRatio() : 0.;
}

void PeerListRow::lazyInitialize(const style::PeerListItem &st) {
	if (_initialized) {
		return;
	}
	_initialized = true;
	refreshName(st);
	refreshStatus();
}

bool PeerListRow::useForumLikeUserpic() const {
	return !special() && peer()->isForum();
}

void PeerListRow::createCheckbox(
		const style::RoundImageCheckbox &st,
		Fn<void()> updateCallback) {
	const auto generateRadius = [=](int size) {
		return useForumLikeUserpic()
			? int(size * Ui::ForumUserpicRadiusMultiplier())
			: std::optional<int>();
	};
	_checkbox = std::make_unique<Ui::RoundImageCheckbox>(
		st,
		std::move(updateCallback),
		generatePaintUserpicCallback(false),
		generateRadius);
}

void PeerListRow::setCheckedInternal(bool checked, anim::type animated) {
	Expects(!checked || _checkbox != nullptr);

	if (_checkbox) {
		_checkbox->setChecked(checked, animated);
	}
}

void PeerListRow::setCustomizedCheckSegments(
		std::vector<Ui::OutlineSegment> segments) {
	Expects(_checkbox != nullptr);

	_checkbox->setCustomizedSegments(std::move(segments));
}

void PeerListRow::finishCheckedAnimation() {
	_checkbox->setChecked(_checkbox->checked(), anim::type::instant);
}

PeerListContent::PeerListContent(
	QWidget *parent,
	not_null<PeerListController*> controller)
: RpWidget(parent)
, _st(controller->computeListSt())
, _controller(controller)
, _rowHeight(_st.item.height) {
	_controller->session().downloaderTaskFinished(
	) | rpl::start_with_next([=] {
		update();
	}, lifetime());

	using UpdateFlag = Data::PeerUpdate::Flag;
	_controller->session().changes().peerUpdates(
		UpdateFlag::Name | UpdateFlag::Photo | UpdateFlag::EmojiStatus
	) | rpl::start_with_next([=](const Data::PeerUpdate &update) {
		if (update.flags & UpdateFlag::Name) {
			handleNameChanged(update.peer);
		}
		if (update.flags & UpdateFlag::Photo) {
			this->update();
		}
	}, lifetime());

	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		invalidatePixmapsCache();
	}, lifetime());

	_repaintByStatus.setCallback([this] { update(); });
}

void PeerListContent::setMode(Mode mode) {
	if (mode == Mode::Default && _mode == Mode::Default) {
		return;
	}
	_mode = mode;
	switch (_mode) {
	case Mode::Default:
		_rowHeight = _st.item.height;
		break;
	case Mode::Custom:
		_rowHeight = _controller->customRowHeight();
		break;
	}
	const auto wasMouseSelection = _mouseSelection;
	const auto wasLastMousePosition = _lastMousePosition;
	_contextMenu = nullptr;
	if (wasMouseSelection) {
		setSelected(Selected());
	}
	setPressed(Selected());
	refreshRows();
	if (wasMouseSelection && wasLastMousePosition) {
		selectByMouse(*wasLastMousePosition);
	}
}

void PeerListContent::appendRow(std::unique_ptr<PeerListRow> row) {
	Expects(row != nullptr);

	if (_rowsById.find(row->id()) == _rowsById.cend()) {
		row->setAbsoluteIndex(_rows.size());
		addRowEntry(row.get());
		if (!_hiddenRows.empty()) {
			Assert(!row->hidden());
			_filterResults.push_back(row.get());
		}
		_rows.push_back(std::move(row));
	}
}

void PeerListContent::appendSearchRow(std::unique_ptr<PeerListRow> row) {
	Expects(row != nullptr);
	Expects(showingSearch());

	if (_rowsById.find(row->id()) == _rowsById.cend()) {
		row->setAbsoluteIndex(_searchRows.size());
		row->setIsSearchResult(true);
		addRowEntry(row.get());
		_filterResults.push_back(row.get());
		_searchRows.push_back(std::move(row));
	}
}

void PeerListContent::appendFoundRow(not_null<PeerListRow*> row) {
	Expects(showingSearch());

	auto index = findRowIndex(row);
	if (index.value < 0) {
		_filterResults.push_back(row);
	}
}

void PeerListContent::changeCheckState(
		not_null<PeerListRow*> row,
		bool checked,
		anim::type animated) {
	row->setChecked(checked, _st.item.checkbox, animated, [=] {
		updateRow(row);
	});
}

void PeerListContent::setRowHidden(not_null<PeerListRow*> row, bool hidden) {
	Expects(!row->isSearchResult());

	row->setHidden(hidden);
	if (hidden) {
		_hiddenRows.emplace(row);
	} else {
		_hiddenRows.remove(row);
	}
}

void PeerListContent::addRowEntry(not_null<PeerListRow*> row) {
	const auto savedMessagesStatus = _controller->savedMessagesChatStatus();
	if (!savedMessagesStatus.isEmpty() && !row->special()) {
		const auto peer = row->peer();
		if (peer->isSelf()) {
			row->setSavedMessagesChatStatus(savedMessagesStatus);
		} else if (peer->isRepliesChat()) {
			row->setIsRepliesMessagesChat(true);
		} else if (peer->isVerifyCodes()) {
			row->setIsVerifyCodesChat(true);
		}
	}
	_rowsById.emplace(row->id(), row);
	if (!row->special()) {
		_rowsByPeer[row->peer()].push_back(row);
	}
	if (addingToSearchIndex()) {
		addToSearchIndex(row);
	}
	if (_controller->isRowSelected(row)) {
		Assert(row->special() || row->id() == row->peer()->id.value);
		changeCheckState(row, true, anim::type::instant);
	}
}

void PeerListContent::invalidatePixmapsCache() {
	auto invalidate = [](auto &&row) { row->invalidatePixmapsCache(); };
	ranges::for_each(_rows, invalidate);
	ranges::for_each(_searchRows, invalidate);
}

bool PeerListContent::addingToSearchIndex() const {
	// If we started indexing already, we continue.
	return (_searchMode != PeerListSearchMode::Disabled) || !_searchIndex.empty();
}

void PeerListContent::addToSearchIndex(not_null<PeerListRow*> row) {
	if (row->isSearchResult()) {
		return;
	}

	removeFromSearchIndex(row);
	row->setNameFirstLetters(row->generateNameFirstLetters());
	for (auto ch : row->nameFirstLetters()) {
		_searchIndex[ch].push_back(row);
	}
}

void PeerListContent::removeFromSearchIndex(not_null<PeerListRow*> row) {
	const auto &nameFirstLetters = row->nameFirstLetters();
	if (!nameFirstLetters.empty()) {
		for (auto ch : row->nameFirstLetters()) {
			auto it = _searchIndex.find(ch);
			if (it != _searchIndex.cend()) {
				auto &entry = it->second;
				entry.erase(ranges::remove(entry, row), end(entry));
				if (entry.empty()) {
					_searchIndex.erase(it);
				}
			}
		}
		row->setNameFirstLetters({});
	}
}

void PeerListContent::prependRow(std::unique_ptr<PeerListRow> row) {
	Expects(row != nullptr);

	if (_rowsById.find(row->id()) == _rowsById.cend()) {
		addRowEntry(row.get());
		if (!_hiddenRows.empty()) {
			Assert(!row->hidden());
			_filterResults.insert(_filterResults.begin(), row.get());
		}
		_rows.insert(_rows.begin(), std::move(row));
		refreshIndices();
	}
}

void PeerListContent::prependRowFromSearchResult(not_null<PeerListRow*> row) {
	if (!row->isSearchResult()) {
		return;
	}
	Assert(_rowsById.find(row->id()) != _rowsById.cend());
	auto index = row->absoluteIndex();
	Assert(index >= 0 && index < _searchRows.size());
	Assert(_searchRows[index].get() == row);

	row->setIsSearchResult(false);
	if (!_hiddenRows.empty()) {
		Assert(!row->hidden());
		_filterResults.insert(_filterResults.begin(), row);
	}
	_rows.insert(_rows.begin(), std::move(_searchRows[index]));
	refreshIndices();
	removeRowAtIndex(_searchRows, index);

	if (addingToSearchIndex()) {
		addToSearchIndex(row);
	}
}

void PeerListContent::refreshIndices() {
	auto index = 0;
	for (auto &row : _rows) {
		row->setAbsoluteIndex(index++);
	}
}

void PeerListContent::removeRowAtIndex(
		std::vector<std::unique_ptr<PeerListRow>> &from,
		int index) {
	from.erase(from.begin() + index);
	for (auto i = index, count = int(from.size()); i != count; ++i) {
		from[i]->setAbsoluteIndex(i);
	}
}

PeerListRow *PeerListContent::findRow(PeerListRowId id) {
	auto it = _rowsById.find(id);
	return (it == _rowsById.cend()) ? nullptr : it->second.get();
}

std::optional<QPoint> PeerListContent::lastRowMousePosition() const {
	if (!_lastMousePosition) {
		return std::nullopt;
	}
	const auto point = mapFromGlobal(*_lastMousePosition);
	auto in = parentWidget()->rect().contains(
		parentWidget()->mapFromGlobal(*_lastMousePosition));
	auto rowsPointY = point.y() - rowsTop();
	const auto index = (in
		&& rowsPointY >= 0
		&& rowsPointY < shownRowsCount() * _rowHeight)
		? (rowsPointY / _rowHeight)
		: -1;
	return (index >= 0 && index == _selected.index.value)
		? QPoint(point.x(), rowsPointY)
		: std::optional<QPoint>();
}

void PeerListContent::removeRow(not_null<PeerListRow*> row) {
	auto index = row->absoluteIndex();
	auto isSearchResult = row->isSearchResult();
	auto &eraseFrom = isSearchResult ? _searchRows : _rows;

	Assert(index >= 0 && index < eraseFrom.size());
	Assert(eraseFrom[index].get() == row);

	auto pressedData = saveSelectedData(_pressed);
	auto contextedData = saveSelectedData(_contexted);
	setSelected(Selected());
	setPressed(Selected());
	setContexted(Selected());

	_rowsById.erase(row->id());
	if (!row->special()) {
		auto &byPeer = _rowsByPeer[row->peer()];
		byPeer.erase(ranges::remove(byPeer, row), end(byPeer));
	}
	removeFromSearchIndex(row);
	_filterResults.erase(
		ranges::remove(_filterResults, row),
		end(_filterResults));
	_hiddenRows.remove(row);
	removeRowAtIndex(eraseFrom, index);

	restoreSelection();
	setPressed(restoreSelectedData(pressedData));
	setContexted(restoreSelectedData(contextedData));
}

void PeerListContent::clearAllContent() {
	setSelected(Selected());
	setPressed(Selected());
	setContexted(Selected());
	_mouseSelection = false;
	_lastMousePosition = std::nullopt;
	_rowsById.clear();
	_rowsByPeer.clear();
	_filterResults.clear();
	_searchIndex.clear();
	_rows.clear();
	_searchRows.clear();
	_searchQuery
		= _normalizedSearchQuery
		= _mentionHighlight
		= QString();
	if (_controller->hasComplexSearch()) {
		_controller->search(QString());
	}
}

void PeerListContent::convertRowToSearchResult(not_null<PeerListRow*> row) {
	if (row->isSearchResult()) {
		return;
	} else if (!showingSearch() || !_controller->hasComplexSearch()) {
		return removeRow(row);
	}
	auto index = row->absoluteIndex();
	Assert(index >= 0 && index < _rows.size());
	Assert(_rows[index].get() == row);

	removeFromSearchIndex(row);
	row->setIsSearchResult(true);
	row->setHidden(false);
	row->setAbsoluteIndex(_searchRows.size());
	_hiddenRows.remove(row);
	_searchRows.push_back(std::move(_rows[index]));
	removeRowAtIndex(_rows, index);
}

int PeerListContent::fullRowsCount() const {
	return _rows.size();
}

not_null<PeerListRow*> PeerListContent::rowAt(int index) const {
	Expects(index >= 0 && index < _rows.size());

	return _rows[index].get();
}

int PeerListContent::searchRowsCount() const {
	return _searchRows.size();
}

not_null<PeerListRow*> PeerListContent::searchRowAt(int index) const {
	Expects(index >= 0 && index < _searchRows.size());

	return _searchRows[index].get();
}

void PeerListContent::setDescription(object_ptr<Ui::FlatLabel> description) {
	_description = std::move(description);
	if (_description) {
		_description->setParent(this);
	}
}

void PeerListContent::setSearchLoading(object_ptr<Ui::FlatLabel> loading) {
	_searchLoading = std::move(loading);
	if (_searchLoading) {
		_searchLoading->setParent(this);
	}
}

void PeerListContent::setSearchNoResults(object_ptr<Ui::FlatLabel> noResults) {
	_searchNoResults = std::move(noResults);
	if (_searchNoResults) {
		_searchNoResults->setParent(this);
	}
}

void PeerListContent::setAboveWidget(object_ptr<Ui::RpWidget> widget) {
	_aboveWidget = std::move(widget);
	initDecorateWidget(_aboveWidget.data());
}

void PeerListContent::setAboveSearchWidget(object_ptr<Ui::RpWidget> widget) {
	_aboveSearchWidget = std::move(widget);
	initDecorateWidget(_aboveSearchWidget.data());
}

void PeerListContent::setHideEmpty(bool hide) {
	_hideEmpty = hide;
	resizeToWidth(width());
}

void PeerListContent::setBelowWidget(object_ptr<Ui::RpWidget> widget) {
	_belowWidget = std::move(widget);
	initDecorateWidget(_belowWidget.data());
}

void PeerListContent::initDecorateWidget(Ui::RpWidget *widget) {
	if (widget) {
		widget->setParent(this);
		widget->events(
		) | rpl::filter([=](not_null<QEvent*> e) {
			return (e->type() == QEvent::Enter) && widget->isVisible();
		}) | rpl::start_with_next([=] {
			mouseLeftGeometry();
		}, widget->lifetime());
		widget->heightValue() | rpl::skip(1) | rpl::start_with_next([=] {
			resizeToWidth(width());
		}, widget->lifetime());
	}
}

int PeerListContent::labelHeight() const {
	if (_hideEmpty && !shownRowsCount()) {
		return 0;
	}
	auto computeLabelHeight = [](auto &label) {
		if (!label) {
			return 0;
		}
		return st::membersAboutLimitPadding.top() + label->height() + st::membersAboutLimitPadding.bottom();
	};
	if (showingSearch()) {
		if (!_filterResults.empty()) {
			return 0;
		}
		if (_controller->isSearchLoading() && _searchLoading) {
			return computeLabelHeight(_searchLoading);
		}
		return computeLabelHeight(_searchNoResults);
	}
	return computeLabelHeight(_description);
}

void PeerListContent::refreshRows() {
	if (!_hiddenRows.empty()) {
		if (!_ignoreHiddenRowsOnSearch || _normalizedSearchQuery.isEmpty()) {
			_filterResults.clear();
			for (const auto &row : _rows) {
				if (!row->hidden()) {
					_filterResults.push_back(row.get());
				}
			}
		}
	}
	resizeToWidth(width());
	if (_visibleBottom > 0) {
		checkScrollForPreload();
	}
	if (_mouseSelection) {
		selectByMouse(QCursor::pos());
	}
	loadProfilePhotos();
	update();
}

void PeerListContent::setSearchMode(PeerListSearchMode mode) {
	if (_searchMode != mode) {
		if (!addingToSearchIndex()) {
			for (const auto &row : _rows) {
				addToSearchIndex(row.get());
			}
		}
		_searchMode = mode;
		if (_controller->hasComplexSearch()) {
			if (_mode == Mode::Custom) {
				if (!_searchLoading) {
					setSearchLoading(object_ptr<Ui::FlatLabel>(
						this,
						tr::lng_contacts_loading(tr::now),
						st::membersAbout));
				}
			} else {
				if (!_loadingAnimation) {
					_loadingAnimation = Ui::CreateLoadingPeerListItemWidget(
						this,
						_st.item,
						2);
				}
			}
		} else {
			clearSearchRows();
		}
	}
}

void PeerListContent::clearSearchRows() {
	while (!_searchRows.empty()) {
		removeRow(_searchRows.back().get());
	}
}

void PeerListContent::paintEvent(QPaintEvent *e) {
	Painter p(this);

	const auto clip = e->rect();
	if (_mode != Mode::Custom) {
		p.fillRect(clip, _st.item.button.textBg);
	}

	const auto repaintByStatusAfter = _repaintByStatus.remainingTime();
	auto repaintAfterMin = repaintByStatusAfter;

	const auto rowsTopCached = rowsTop();
	const auto now = crl::now();
	const auto yFrom = clip.y() - rowsTopCached;
	const auto yTo = clip.y() + clip.height() - rowsTopCached;
	p.translate(0, rowsTopCached);
	const auto count = shownRowsCount();
	if (count > 0) {
		const auto from = floorclamp(yFrom, _rowHeight, 0, count);
		const auto to = ceilclamp(yTo, _rowHeight, 0, count);
		p.translate(0, from * _rowHeight);
		for (auto index = from; index != to; ++index) {
			const auto repaintAfter = paintRow(p, now, RowIndex(index));
			if (repaintAfter > 0
				&& (repaintAfterMin < 0
					|| repaintAfterMin > repaintAfter)) {
				repaintAfterMin = repaintAfter;
			}
			p.translate(0, _rowHeight);
		}
	}
	if (repaintAfterMin != repaintByStatusAfter) {
		Assert(repaintAfterMin >= 0);
		_repaintByStatus.callOnce(repaintAfterMin);
	}
}

int PeerListContent::resizeGetHeight(int newWidth) {
	const auto rowsCount = shownRowsCount();
	const auto hideAll = !rowsCount && _hideEmpty;
	_aboveHeight = 0;
	if (_aboveWidget) {
		_aboveWidget->resizeToWidth(newWidth);
		_aboveWidget->moveToLeft(0, 0, newWidth);
		if (hideAll || showingSearch()) {
			_aboveWidget->hide();
		} else {
			_aboveWidget->show();
			_aboveHeight = _aboveWidget->height();
		}
	}
	if (_aboveSearchWidget) {
		_aboveSearchWidget->resizeToWidth(newWidth);
		_aboveSearchWidget->moveToLeft(0, 0, newWidth);
		if (hideAll || !showingSearch()) {
			_aboveSearchWidget->hide();
		} else {
			_aboveSearchWidget->show();
			_aboveHeight = _aboveSearchWidget->height();
		}
	}
	const auto labelTop = rowsTop()
		+ std::max(
			shownRowsCount() * _rowHeight,
			_controller->descriptionTopSkipMin());
	const auto labelWidth = newWidth - 2 * st::contactsPadding.left();
	if (_description) {
		_description->resizeToWidth(labelWidth);
		_description->moveToLeft(st::contactsPadding.left(), labelTop + st::membersAboutLimitPadding.top(), newWidth);
		_description->setVisible(!hideAll && !showingSearch());
	}
	if (_searchNoResults) {
		_searchNoResults->resizeToWidth(labelWidth);
		_searchNoResults->moveToLeft(st::contactsPadding.left(), labelTop + st::membersAboutLimitPadding.top(), newWidth);
		_searchNoResults->setVisible(!hideAll && showingSearch() && _filterResults.empty() && !_controller->isSearchLoading());
	}
	if (_searchLoading) {
		_searchLoading->resizeToWidth(labelWidth);
		_searchLoading->moveToLeft(st::contactsPadding.left(), labelTop + st::membersAboutLimitPadding.top(), newWidth);
		_searchLoading->setVisible(!hideAll && showingSearch() && _filterResults.empty() && _controller->isSearchLoading());
	}
	if (_loadingAnimation) {
		_loadingAnimation->resizeToWidth(newWidth);
		_loadingAnimation->moveToLeft(0, rowsTop(), newWidth);
		_loadingAnimation->setVisible(!hideAll
			&& showingSearch()
			&& _filterResults.empty()
			&& _controller->isSearchLoading());
	}
	const auto label = labelHeight();
	const auto belowTop = (label > 0 || rowsCount > 0)
		? (labelTop + label + _st.padding.bottom())
		: _aboveHeight;
	_belowHeight = 0;
	if (_belowWidget) {
		_belowWidget->resizeToWidth(newWidth);
		_belowWidget->moveToLeft(0, belowTop, newWidth);
		if (hideAll || showingSearch()) {
			_belowWidget->hide();
		} else {
			_belowWidget->show();
			_belowHeight = _belowWidget->height();
		}
	}
	return belowTop + _belowHeight;
}

void PeerListContent::enterEventHook(QEnterEvent *e) {
	setMouseTracking(true);
}

void PeerListContent::leaveEventHook(QEvent *e) {
	setMouseTracking(false);
	mouseLeftGeometry();
}

void PeerListContent::mouseMoveEvent(QMouseEvent *e) {
	handleMouseMove(e->globalPos());
}

void PeerListContent::handleMouseMove(QPoint globalPosition) {
	if (!_lastMousePosition) {
		_lastMousePosition = globalPosition;
		return;
	} else if (!_mouseSelection
		&& *_lastMousePosition == globalPosition) {
		return;
	}
	if (_trackPressStart
		&& ((*_trackPressStart - globalPosition).manhattanLength()
			> QApplication::startDragDistance())) {
		_trackPressStart = {};
		_controller->rowTrackPressCancel();
	}
	if (!_controller->rowTrackPressSkipMouseSelection()) {
		selectByMouse(globalPosition);
	}
}

void PeerListContent::pressLeftToContextMenu(bool shown) {
	if (shown) {
		setContexted(_pressed);
		setPressed(Selected());
	} else {
		setContexted(Selected());
	}
}

bool PeerListContent::trackRowPressFromGlobal(QPoint globalPosition) {
	selectByMouse(globalPosition);
	if (const auto row = getRow(_selected.index)) {
		if (_controller->rowTrackPress(row)) {
			_trackPressStart = globalPosition;
			return true;
		}
	}
	return false;
}

void PeerListContent::mousePressEvent(QMouseEvent *e) {
	_pressButton = e->button();
	selectByMouse(e->globalPos());
	setPressed(_selected);
	_trackPressStart = {};
	if (const auto row = getRow(_selected.index)) {
		const auto updateCallback = [this, row, hint = _selected.index] {
			updateRow(row, hint);
		};
		if (_selected.element) {
			const auto elementRect = getElementRect(
				row,
				_selected.index,
				_selected.element);
			if (!elementRect.isEmpty()) {
				row->elementAddRipple(
					_selected.element,
					mapFromGlobal(QCursor::pos()) - elementRect.topLeft(),
					std::move(updateCallback));
			}
		} else {
			auto point = mapFromGlobal(QCursor::pos()) - QPoint(0, getRowTop(_selected.index));
			if (_mode == Mode::Custom) {
				row->addRipple(_st.item, _controller->customRowRippleMaskGenerator(), point, std::move(updateCallback));
			} else {
				const auto maskGenerator = [&] {
					return Ui::RippleAnimation::RectMask(
						QSize(width(), _rowHeight));
				};
				row->addRipple(_st.item, maskGenerator, point, std::move(updateCallback));
			}
		}
		if (_pressButton == Qt::LeftButton && _controller->rowTrackPress(row)) {
			_trackPressStart = e->globalPos();
		}
	}
	if (anim::Disabled() && !_trackPressStart && !_selected.element) {
		mousePressReleased(e->button());
	}
}

void PeerListContent::mouseReleaseEvent(QMouseEvent *e) {
	mousePressReleased(e->button());
}

void PeerListContent::mousePressReleased(Qt::MouseButton button) {
	_trackPressStart = {};
	_controller->rowTrackPressCancel();

	updateRow(_pressed.index);
	updateRow(_selected.index);

	auto pressed = _pressed;
	setPressed(Selected());
	if (button == Qt::LeftButton && pressed == _selected) {
		if (auto row = getRow(pressed.index)) {
			if (pressed.element) {
				_controller->rowElementClicked(row, pressed.element);
			} else {
				_controller->rowClicked(row);
			}
		}
	} else if (button == Qt::MiddleButton && pressed == _selected) {
		if (auto row = getRow(pressed.index)) {
			_controller->rowMiddleClicked(row);
		}
	}
}

void PeerListContent::showRowMenu(
		not_null<PeerListRow*> row,
		bool highlightRow,
		Fn<void(not_null<Ui::PopupMenu*>)> destroyed) {
	const auto index = findRowIndex(row);
	showRowMenu(
		index,
		row,
		QCursor::pos(),
		highlightRow,
		std::move(destroyed));
}

bool PeerListContent::showRowMenu(
		RowIndex index,
		PeerListRow *row,
		QPoint globalPos,
		bool highlightRow,
		Fn<void(not_null<Ui::PopupMenu*>)> destroyed) {
	if (_contextMenu) {
		_contextMenu->setDestroyedCallback(nullptr);
		_contextMenu = nullptr;
	}
	setContexted(Selected());
	if (_pressButton != Qt::LeftButton) {
		mousePressReleased(_pressButton);
	}

	if (highlightRow) {
		row = getRow(index);
	}
	if (!row) {
		return false;
	}

	_contextMenu = _controller->rowContextMenu(this, row);
	const auto raw = _contextMenu.get();
	if (!raw) {
		return false;
	}

	if (highlightRow) {
		setContexted({ index, false });
	}
	raw->setDestroyedCallback(crl::guard(
		this,
		[=] {
			if (highlightRow) {
				setContexted(Selected());
			}
			handleMouseMove(QCursor::pos());
			if (destroyed) {
				destroyed(raw);
			}
		}));
	raw->popup(globalPos);
	return true;
}

void PeerListContent::contextMenuEvent(QContextMenuEvent *e) {
	if (e->reason() == QContextMenuEvent::Mouse) {
		handleMouseMove(e->globalPos());
	}
	if (showRowMenu(_selected.index, nullptr, e->globalPos(), true)) {
		e->accept();
	}
}

void PeerListContent::setPressed(Selected pressed) {
	if (_pressed == pressed) {
		return;
	} else if (const auto row = getRow(_pressed.index)) {
		row->stopLastRipple();
		row->elementsStopLastRipple();
	}
	_pressed = pressed;
}

crl::time PeerListContent::paintRow(
		Painter &p,
		crl::time now,
		RowIndex index) {
	const auto row = getRow(index);
	Assert(row != nullptr);

	const auto &st = row->computeSt(_st.item);

	row->lazyInitialize(st);
	const auto outerWidth = width();

	auto refreshStatusAt = row->refreshStatusTime();
	if (refreshStatusAt > 0 && now >= refreshStatusAt) {
		row->refreshStatus();
		refreshStatusAt = row->refreshStatusTime();
	}
	const auto refreshStatusIn = (refreshStatusAt > 0)
		? std::max(refreshStatusAt - now, crl::time(1))
		: 0;

	const auto peer = row->special() ? nullptr : row->peer().get();
	const auto active = (_contexted.index.value >= 0)
		? _contexted
		: (_pressed.index.value >= 0)
		? _pressed
		: _selected;
	const auto selected = (active.index == index)
		&& (!active.element || !row->elementOnlySelect(active.element));

	if (_mode == Mode::Custom) {
		_controller->customRowPaint(p, now, row, selected);
		return refreshStatusIn;
	}

	const auto opacity = row->opacity();
	const auto &bg = selected
		? st.button.textBgOver
		: st.button.textBg;
	if (opacity < 1.) {
		p.setOpacity(opacity);
	}
	const auto guard = gsl::finally([&] {
		if (opacity < 1.) {
			p.setOpacity(1.);
		}
	});

	p.fillRect(0, 0, outerWidth, _rowHeight, bg);
	row->paintRipple(p, st, 0, 0, outerWidth);
	row->paintUserpic(
		p,
		st,
		st.photoPosition.x(),
		st.photoPosition.y(),
		outerWidth);

	p.setPen(st::contactsNameFg);

	const auto skipRight = st.photoPosition.x();
	const auto rightActionSize = row->rightActionSize();
	const auto rightActionMargins = rightActionSize.isEmpty()
		? QMargins()
		: row->rightActionMargins();
	const auto &name = row->name();
	const auto namePosition = st.namePosition;
	const auto namex = namePosition.x();
	const auto namey = namePosition.y();
	auto namew = outerWidth - namex - skipRight;
	if (!rightActionSize.isEmpty()
		&& (namey < rightActionMargins.top() + rightActionSize.height())
		&& (namey + st.nameStyle.font->height
			> rightActionMargins.top())) {
		namew -= rightActionMargins.left()
			+ rightActionSize.width()
			+ rightActionMargins.right()
			- skipRight;
	}
	const auto statusx = st.statusPosition.x();
	const auto statusy = st.statusPosition.y();
	auto statusw = outerWidth - statusx - skipRight;
	if (!rightActionSize.isEmpty()
		&& (statusy < rightActionMargins.top() + rightActionSize.height())
		&& (statusy + st::contactsStatusFont->height
			> rightActionMargins.top())) {
		statusw -= rightActionMargins.left()
			+ rightActionSize.width()
			+ rightActionMargins.right()
			- skipRight;
	}
	namew -= row->paintNameIconGetWidth(
		p,
		[=] { updateRow(row); },
		now,
		namex,
		namey,
		name.maxWidth(),
		namew,
		width(),
		selected);
	auto nameCheckedRatio = row->disabled() ? 0. : row->checkedRatio();
	p.setPen(anim::pen(st.nameFg, st.nameFgChecked, nameCheckedRatio));
	name.drawLeftElided(p, namex, namey, namew, width());

	p.setFont(st::contactsStatusFont);
	if (row->isSearchResult()
		&& !_mentionHighlight.isEmpty()
		&& peer
		&& peer->username().startsWith(
			_mentionHighlight,
			Qt::CaseInsensitive)) {
		const auto username = peer->username();
		const auto availableWidth = statusw;
		auto highlightedPart = '@' + username.mid(0, _mentionHighlight.size());
		auto grayedPart = username.mid(_mentionHighlight.size());
		const auto highlightedWidth = st::contactsStatusFont->width(highlightedPart);
		if (highlightedWidth >= availableWidth || grayedPart.isEmpty()) {
			if (highlightedWidth > availableWidth) {
				highlightedPart = st::contactsStatusFont->elided(highlightedPart, availableWidth);
			}
			p.setPen(st.statusFgActive);
			p.drawTextLeft(statusx, statusy, width(), highlightedPart);
		} else {
			grayedPart = st::contactsStatusFont->elided(grayedPart, availableWidth - highlightedWidth);
			p.setPen(st.statusFgActive);
			p.drawTextLeft(statusx, statusy, width(), highlightedPart);
			p.setPen(selected ? st.statusFgOver : st.statusFg);
			p.drawTextLeft(statusx + highlightedWidth, statusy, width(), grayedPart);
		}
	} else {
		row->paintStatusText(p, st, statusx, statusy, statusw, width(), selected);
	}

	row->elementsPaint(
		p,
		width(),
		selected,
		(active.index == index) ? active.element : 0);

	return refreshStatusIn;
}

PeerListContent::SkipResult PeerListContent::selectSkip(int direction) {
	if (hasPressed()) {
		return { _selected.index.value, _selected.index.value };
	}
	_mouseSelection = false;
	_lastMousePosition = std::nullopt;

	auto newSelectedIndex = _selected.index.value + direction;

	auto result = SkipResult();
	result.shouldMoveTo = newSelectedIndex;

	auto rowsCount = shownRowsCount();
	auto index = 0;
	auto firstEnabled = -1, lastEnabled = -1;
	enumerateShownRows([&firstEnabled, &lastEnabled, &index](not_null<PeerListRow*> row) {
		if (!row->disabled()) {
			if (firstEnabled < 0) {
				firstEnabled = index;
			}
			lastEnabled = index;
		}
		++index;
		return true;
	});
	if (firstEnabled < 0) {
		firstEnabled = rowsCount;
		lastEnabled = firstEnabled - 1;
	}

	Assert(lastEnabled < rowsCount);
	Assert(firstEnabled - 1 <= lastEnabled);

	// Always pass through the first enabled item when changing from / to none selected.
	if ((_selected.index.value > firstEnabled && newSelectedIndex < firstEnabled)
		|| (_selected.index.value < firstEnabled && newSelectedIndex > firstEnabled)) {
		newSelectedIndex = firstEnabled;
	}

	// Snap the index.
	newSelectedIndex = std::clamp(
		newSelectedIndex,
		firstEnabled - 1,
		lastEnabled);

	// Skip the disabled rows.
	if (newSelectedIndex < firstEnabled) {
		newSelectedIndex = -1;
	} else if (newSelectedIndex > lastEnabled) {
		newSelectedIndex = lastEnabled;
	} else if (getRow(RowIndex(newSelectedIndex))->disabled()) {
		auto delta = (direction > 0) ? 1 : -1;
		for (newSelectedIndex += delta; ; newSelectedIndex += delta) {
			// We must find an enabled row, firstEnabled <= us <= lastEnabled.
			Assert(newSelectedIndex >= 0 && newSelectedIndex < rowsCount);
			if (!getRow(RowIndex(newSelectedIndex))->disabled()) {
				break;
			}
		}
	}

	if (_controller->overrideKeyboardNavigation(
			direction,
			_selected.index.value,
			newSelectedIndex)) {
		return { _selected.index.value, _selected.index.value };
	}

	_selected.index.value = newSelectedIndex;
	_selected.element = 0;
	if (newSelectedIndex >= 0) {
		auto top = (newSelectedIndex > 0) ? getRowTop(RowIndex(newSelectedIndex)) : _aboveHeight;
		auto bottom = (newSelectedIndex + 1 < rowsCount) ? getRowTop(RowIndex(newSelectedIndex + 1)) : height();
		_scrollToRequests.fire({ top, bottom });
	} else if (!_selected.index.value && direction < 0) {
		auto top = 0;
		auto bottom = _aboveHeight;
		_scrollToRequests.fire({ top, bottom });
	}

	update();

	_selectedIndex = _selected.index.value;
	result.reallyMovedTo = _selected.index.value;
	return result;
}

void PeerListContent::selectSkipPage(int height, int direction) {
	auto rowsToSkip = height / _rowHeight;
	if (!rowsToSkip) {
		return;
	}
	selectSkip(rowsToSkip * direction);
}

void PeerListContent::selectLast() {
	const auto rowsCount = shownRowsCount();
	const auto newSelectedIndex = rowsCount - 1;
	_selected.index.value = newSelectedIndex;
	_selected.element = 0;
	if (newSelectedIndex >= 0) {
		auto top = (newSelectedIndex > 0) ? getRowTop(RowIndex(newSelectedIndex)) : 0;
		auto bottom = (newSelectedIndex + 1 < rowsCount) ? getRowTop(RowIndex(newSelectedIndex + 1)) : height();
		_scrollToRequests.fire({ top, bottom });
	}

	update();

	_selectedIndex = _selected.index.value;
}

rpl::producer<int> PeerListContent::selectedIndexValue() const {
	return _selectedIndex.value();
}

int PeerListContent::selectedIndex() const {
	return _selectedIndex.current();
}

bool PeerListContent::hasSelection() const {
	return _selected.index.value >= 0;
}

bool PeerListContent::hasPressed() const {
	return _pressed.index.value >= 0;
}

void PeerListContent::clearSelection() {
	setSelected(Selected());
}

void PeerListContent::mouseLeftGeometry() {
	if (_mouseSelection) {
		setSelected(Selected());
		_mouseSelection = false;
		_lastMousePosition = std::nullopt;
	}
}

void PeerListContent::loadProfilePhotos() {
	if (_visibleTop >= _visibleBottom) {
		return;
	}

	auto yFrom = _visibleTop;
	auto yTo = _visibleBottom + (_visibleBottom - _visibleTop) * PreloadHeightsCount;

	if (yTo < 0) return;
	if (yFrom < 0) yFrom = 0;

	auto rowsCount = shownRowsCount();
	if (rowsCount > 0) {
		auto from = yFrom / _rowHeight;
		if (from < 0) from = 0;
		if (from < rowsCount) {
			auto to = (yTo / _rowHeight) + 1;
			if (to > rowsCount) to = rowsCount;

			for (auto index = from; index != to; ++index) {
				getRow(RowIndex(index))->preloadUserpic();
			}
		}
	}
}

void PeerListContent::checkScrollForPreload() {
	if (_visibleBottom + PreloadHeightsCount * (_visibleBottom - _visibleTop) >= height()) {
		_controller->loadMoreRows();
	}
}

void PeerListContent::searchQueryChanged(QString query) {
	const auto searchWordsList = TextUtilities::PrepareSearchWords(query);
	const auto normalizedQuery = searchWordsList.join(' ');
	if (_ignoreHiddenRowsOnSearch && !normalizedQuery.isEmpty()) {
		_filterResults.clear();
	}
	if (_normalizedSearchQuery != normalizedQuery) {
		setSearchQuery(query, normalizedQuery);
		if (_controller->searchInLocal() && !searchWordsList.isEmpty()) {
			Assert(_hiddenRows.empty() || _ignoreHiddenRowsOnSearch);

			auto minimalList = (const std::vector<not_null<PeerListRow*>>*)nullptr;
			for (const auto &searchWord : searchWordsList) {
				auto searchWordStart = searchWord[0].toLower();
				auto it = _searchIndex.find(searchWordStart);
				if (it == _searchIndex.cend()) {
					// Some word can't be found in any row.
					minimalList = nullptr;
					break;
				} else if (!minimalList || minimalList->size() > it->second.size()) {
					minimalList = &it->second;
				}
			}
			if (minimalList) {
				auto searchWordInNames = [](
						not_null<PeerListRow*> row,
						const QString &searchWord) {
					for (auto &nameWord : row->generateNameWords()) {
						if (nameWord.startsWith(searchWord)) {
							return true;
						}
					}
					return false;
				};
				auto allSearchWordsInNames = [&](
						not_null<PeerListRow*> row) {
					for (const auto &searchWord : searchWordsList) {
						if (!searchWordInNames(row, searchWord)) {
							return false;
						}
					}
					return true;
				};

				_filterResults.reserve(minimalList->size());
				for (const auto &row : *minimalList) {
					if (allSearchWordsInNames(row)) {
						_filterResults.push_back(row);
					}
				}
			}
		}
		if (_controller->hasComplexSearch()) {
			_controller->search(_searchQuery);
		}
		refreshRows();
	}
}

std::unique_ptr<PeerListState> PeerListContent::saveState() const {
	Expects(_hiddenRows.empty());

	auto result = std::make_unique<PeerListState>();
	result->controllerState
		= std::make_unique<PeerListController::SavedStateBase>();
	result->list.reserve(_rows.size());
	for (const auto &row : _rows) {
		result->list.push_back(row->peer());
	}
	result->filterResults.reserve(_filterResults.size());
	for (const auto &row : _filterResults) {
		result->filterResults.push_back(row->peer());
	}
	result->searchQuery = _searchQuery;
	return result;
}

void PeerListContent::restoreState(
		std::unique_ptr<PeerListState> state) {
	if (!state || !state->controllerState) {
		return;
	}

	clearAllContent();

	for (auto peer : state->list) {
		if (auto row = _controller->createRestoredRow(peer)) {
			appendRow(std::move(row));
		}
	}
	auto query = state->searchQuery;
	auto searchWords = TextUtilities::PrepareSearchWords(query);
	setSearchQuery(query, searchWords.join(' '));
	for (auto peer : state->filterResults) {
		if (auto existingRow = findRow(peer->id.value)) {
			_filterResults.push_back(existingRow);
		} else if (auto row = _controller->createSearchRow(peer)) {
			appendSearchRow(std::move(row));
		}
	}
	refreshRows();
}

void PeerListContent::setSearchQuery(
		const QString &query,
		const QString &normalizedQuery) {
	setSelected(Selected());
	setPressed(Selected());
	setContexted(Selected());
	_mouseSelection = false;
	_lastMousePosition = std::nullopt;
	_searchQuery = query;
	_normalizedSearchQuery = normalizedQuery;
	_mentionHighlight = _searchQuery.startsWith('@')
		? _searchQuery.mid(1)
		: _searchQuery;
	_filterResults.clear();
	clearSearchRows();
}

bool PeerListContent::submitted() {
	if (const auto row = getRow(_selected.index)) {
		_lastMousePosition = std::nullopt;
		_controller->rowClicked(row);
		return true;
	} else if (showingSearch()) {
		if (const auto row = getRow(RowIndex(0))) {
			_lastMousePosition = std::nullopt;
			_controller->rowClicked(row);
			return true;
		}
	} else {
		_noSearchSubmits.fire({});
		return true;
	}
	return false;
}

PeerListRowId PeerListContent::updateFromParentDrag(QPoint globalPosition) {
	selectByMouse(globalPosition);
	const auto row = getRow(_selected.index);
	return row ? row->id() : 0;
}

void PeerListContent::dragLeft() {
	clearSelection();
}

void PeerListContent::setIgnoreHiddenRowsOnSearch(bool value) {
	_ignoreHiddenRowsOnSearch = value;
}

void PeerListContent::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	_visibleTop = visibleTop;
	_visibleBottom = visibleBottom;
	loadProfilePhotos();
	checkScrollForPreload();
}

void PeerListContent::setSelected(Selected selected) {
	updateRow(_selected.index);
	if (_selected == selected) {
		return;
	}
	_selected = selected;
	updateRow(_selected.index);
	setCursor(_selected.element ? style::cur_pointer : style::cur_default);

	_selectedIndex = _selected.index.value;
}

void PeerListContent::setContexted(Selected contexted) {
	updateRow(_contexted.index);
	if (_contexted != contexted) {
		_contexted = contexted;
		updateRow(_contexted.index);
	}
}

void PeerListContent::restoreSelection() {
	if (_mouseSelection) {
		selectByMouse(QCursor::pos());
	}
}

auto PeerListContent::saveSelectedData(Selected from)
-> SelectedSaved {
	if (auto row = getRow(from.index)) {
		return { row->id(), from };
	}
	return { PeerListRowId(0), from };
}

auto PeerListContent::restoreSelectedData(SelectedSaved from)
-> Selected {
	auto result = from.old;
	if (auto row = findRow(from.id)) {
		result.index = findRowIndex(row, result.index);
	} else {
		result.index.value = -1;
	}
	return result;
}

void PeerListContent::selectByMouse(QPoint globalPosition) {
	_mouseSelection = true;
	_lastMousePosition = globalPosition;
	const auto point = mapFromGlobal(globalPosition);
	const auto customMode = (_mode == Mode::Custom);
	auto in = parentWidget()->rect().contains(parentWidget()->mapFromGlobal(globalPosition));
	auto selected = Selected();
	auto rowsPointY = point.y() - rowsTop();
	selected.index.value = (in
		&& rowsPointY >= 0
		&& rowsPointY < shownRowsCount() * _rowHeight)
		? (rowsPointY / _rowHeight)
		: -1;
	if (selected.index.value >= 0) {
		const auto row = getRow(selected.index);
		if (row->disabled()
			|| (customMode
				&& !_controller->customRowSelectionPoint(
					row,
					point.x(),
					rowsPointY - (selected.index.value * _rowHeight)))) {
			selected = Selected();
		} else if (!customMode) {
			for (auto i = 0, count = row->elementsCount(); i != count; ++i) {
				const auto rect = getElementRect(row, selected.index, i + 1);
				if (rect.contains(point)) {
					selected.element = i + 1;
					break;
				}
			}
		}
	}
	setSelected(selected);
}

QRect PeerListContent::getElementRect(
		not_null<PeerListRow*> row,
		RowIndex index,
		int element) const {
	if (row->elementDisabled(element)) {
		return QRect();
	}
	const auto geometry = row->elementGeometry(element, width());
	if (geometry.isEmpty()) {
		return QRect();
	}
	return geometry.translated(0, getRowTop(index));
}

int PeerListContent::rowsTop() const {
	return _aboveHeight + _st.padding.top();
}

int PeerListContent::getRowTop(RowIndex index) const {
	if (index.value >= 0) {
		return rowsTop() + index.value * _rowHeight;
	}
	return -1;
}

void PeerListContent::updateRow(not_null<PeerListRow*> row, RowIndex hint) {
	updateRow(findRowIndex(row, hint));
}

void PeerListContent::updateRow(RowIndex index) {
	if (index.value < 0) {
		return;
	}
	if (const auto row = getRow(index); row && row->disabled()) {
		if (index == _selected.index) {
			setSelected(Selected());
		}
		if (index == _pressed.index) {
			setPressed(Selected());
		}
		if (index == _contexted.index) {
			setContexted(Selected());
		}
	}
	update(0, getRowTop(index), width(), _rowHeight);
}

template <typename Callback>
bool PeerListContent::enumerateShownRows(Callback callback) {
	return enumerateShownRows(0, shownRowsCount(), std::move(callback));
}

template <typename Callback>
bool PeerListContent::enumerateShownRows(int from, int to, Callback callback) {
	Assert(0 <= from);
	Assert(from <= to);
	if (showingSearch()) {
		Assert(to <= _filterResults.size());
		for (auto i = from; i != to; ++i) {
			if (!callback(_filterResults[i])) {
				return false;
			}
		}
	} else {
		Assert(to <= _rows.size());
		for (auto i = from; i != to; ++i) {
			if (!callback(_rows[i].get())) {
				return false;
			}
		}
	}
	return true;
}

PeerListRow *PeerListContent::getRow(RowIndex index) {
	if (index.value >= 0) {
		if (showingSearch()) {
			if (index.value < _filterResults.size()) {
				return _filterResults[index.value];
			}
		} else if (index.value < _rows.size()) {
			return _rows[index.value].get();
		}
	}
	return nullptr;
}

PeerListContent::RowIndex PeerListContent::findRowIndex(
		not_null<PeerListRow*> row,
		RowIndex hint) {
	if (!showingSearch()) {
		Assert(!row->isSearchResult());
		return RowIndex(row->absoluteIndex());
	}

	auto result = hint;
	if (getRow(result) == row) {
		return result;
	}

	auto count = shownRowsCount();
	for (result.value = 0; result.value != count; ++result.value) {
		if (getRow(result) == row) {
			return result;
		}
	}
	result.value = -1;
	return result;
}

void PeerListContent::handleNameChanged(not_null<PeerData*> peer) {
	auto byPeer = _rowsByPeer.find(peer);
	if (byPeer != _rowsByPeer.cend()) {
		for (auto row : byPeer->second) {
			if (addingToSearchIndex()) {
				addToSearchIndex(row);
			}
			row->refreshName(_st.item);
			updateRow(row);
		}
	}
}

PeerListContent::~PeerListContent() {
	if (_contextMenu) {
		_contextMenu->setDestroyedCallback(nullptr);
	}
}

void PeerListContentDelegate::peerListShowRowMenu(
		not_null<PeerListRow*> row,
		bool highlightRow,
		Fn<void(not_null<Ui::PopupMenu *>)> destroyed) {
	_content->showRowMenu(row, highlightRow, std::move(destroyed));
}
