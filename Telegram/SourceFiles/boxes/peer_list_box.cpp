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
#include "boxes/peer_list_box.h"

#include <rpl/range.h>
#include "styles/style_boxes.h"
#include "styles/style_dialogs.h"
#include "styles/style_widgets.h"
#include "auth_session.h"
#include "mainwidget.h"
#include "ui/widgets/multi_select.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/popup_menu.h"
#include "ui/effects/round_checkbox.h"
#include "ui/effects/ripple_animation.h"
#include "ui/empty_userpic.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/text_options.h"
#include "lang/lang_keys.h"
#include "observer_peer.h"
#include "storage/file_download.h"
#include "data/data_peer_values.h"
#include "window/themes/window_theme.h"

PeerListBox::PeerListBox(
	QWidget*,
	std::unique_ptr<PeerListController> controller,
	base::lambda<void(not_null<PeerListBox*>)> init)
: _controller(std::move(controller))
, _init(std::move(init)) {
	Expects(_controller != nullptr);
}

void PeerListBox::createMultiSelect() {
	Expects(_select == nullptr);

	auto entity = object_ptr<Ui::MultiSelect>(this, st::contactsMultiSelect, langFactory(lng_participant_filter));
	_select.create(this, std::move(entity));
	_select->heightValue(
	) | rpl::start_with_next(
		[this] { updateScrollSkips(); },
		lifetime());
	_select->entity()->setSubmittedCallback([this](bool chtrlShiftEnter) { content()->submitted(); });
	_select->entity()->setQueryChangedCallback([this](const QString &query) { searchQueryChanged(query); });
	_select->entity()->setItemRemovedCallback([this](uint64 itemId) {
		if (auto peer = App::peerLoaded(itemId)) {
			if (auto row = peerListFindRow(peer->id)) {
				content()->changeCheckState(row, false, PeerListRow::SetStyle::Animated);
				update();
			}
			_controller->itemDeselectedHook(peer);
		}
	});
	_select->resizeToWidth(st::boxWideWidth);
	_select->moveToLeft(0, 0);
}

int PeerListBox::getTopScrollSkip() const {
	auto result = 0;
	if (_select && !_select->isHidden()) {
		result += _select->height();
	}
	return result;
}

void PeerListBox::updateScrollSkips() {
	// If we show / hide the search field scroll top is fixed.
	// If we resize search field by bubbles scroll bottom is fixed.
	setInnerTopSkip(getTopScrollSkip(), _scrollBottomFixed);
	if (!_select->animating()) {
		_scrollBottomFixed = true;
	}
}

void PeerListBox::prepare() {
	setContent(setInnerWidget(
		object_ptr<PeerListContent>(
			this,
			_controller.get(),
			st::peerListBox),
		st::boxLayerScroll));
	content()->resizeToWidth(st::boxWideWidth);

	_controller->setDelegate(this);

	setDimensions(st::boxWideWidth, st::boxMaxListHeight);
	if (_select) {
		_select->finishAnimating();
		Ui::SendPendingMoveResizeEvents(_select);
		_scrollBottomFixed = true;
		onScrollToY(0);
	}

	content()->scrollToRequests(
	) | rpl::start_with_next([this](Ui::ScrollToRequest request) {
		onScrollToY(request.ymin, request.ymax);
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
	} else if (e->key() == Qt::Key_Escape && _select && !_select->entity()->getQuery().isEmpty()) {
		_select->entity()->clearQuery();
	} else {
		BoxContent::keyPressEvent(e);
	}
}

void PeerListBox::searchQueryChanged(const QString &query) {
	onScrollToY(0);
	content()->searchQueryChanged(query);
}

void PeerListBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	if (_select) {
		_select->resizeToWidth(width());
		_select->moveToLeft(0, 0);

		updateScrollSkips();
	}

	content()->resizeToWidth(width());
}

void PeerListBox::paintEvent(QPaintEvent *e) {
	Painter p(this);
	for (auto rect : e->region().rects()) {
		p.fillRect(rect, st::contactsBg);
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
	auto peer = row->peer();
	if (checked) {
		addSelectItem(peer, PeerListRow::SetStyle::Animated);
		PeerListContentDelegate::peerListSetRowChecked(row, checked);
		peerListUpdateRow(row);

		// This call deletes row from _searchRows.
		_select->entity()->clearQuery();
	} else {
		// The itemRemovedCallback will call changeCheckState() here.
		_select->entity()->removeItem(peer->id);
		peerListUpdateRow(row);
	}
}

void PeerListBox::peerListScrollToTop() {
	onScrollToY(0);
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

PeerListController::PeerListController(std::unique_ptr<PeerListSearchController> searchController) : _searchController(std::move(searchController)) {
	if (_searchController) {
		_searchController->setDelegate(this);
	}
}

bool PeerListController::hasComplexSearch() const {
	return (_searchController != nullptr);
}

void PeerListController::search(const QString &query) {
	Expects(hasComplexSearch());
	_searchController->searchQuery(query);
}

void PeerListController::peerListSearchAddRow(not_null<PeerData*> peer) {
	if (auto row = delegate()->peerListFindRow(peer->id)) {
		Assert(row->id() == row->peer()->id);
		delegate()->peerListAppendFoundRow(row);
	} else if (auto row = createSearchRow(peer)) {
		Assert(row->id() == row->peer()->id);
		delegate()->peerListAppendSearchRow(std::move(row));
	}
}

void PeerListController::peerListSearchRefreshRows() {
	delegate()->peerListRefreshRows();
}

rpl::producer<int> PeerListController::onlineCountValue() const {
	return rpl::single(0);
}

void PeerListController::setDescriptionText(const QString &text) {
	if (text.isEmpty()) {
		setDescription(nullptr);
	} else {
		setDescription(object_ptr<Ui::FlatLabel>(nullptr, text, Ui::FlatLabel::InitType::Simple, st::membersAbout));
	}
}

void PeerListController::setSearchLoadingText(const QString &text) {
	if (text.isEmpty()) {
		setSearchLoading(nullptr);
	} else {
		setSearchLoading(object_ptr<Ui::FlatLabel>(nullptr, text, Ui::FlatLabel::InitType::Simple, st::membersAbout));
	}
}

void PeerListController::setSearchNoResultsText(const QString &text) {
	if (text.isEmpty()) {
		setSearchNoResults(nullptr);
	} else {
		setSearchNoResults(object_ptr<Ui::FlatLabel>(nullptr, text, Ui::FlatLabel::InitType::Simple, st::membersAbout));
	}
}

std::unique_ptr<PeerListState> PeerListController::saveState() const {
	return delegate()->peerListSaveState();
}

void PeerListController::restoreState(
		std::unique_ptr<PeerListState> state) {
	delegate()->peerListRestoreState(std::move(state));
}

void PeerListBox::addSelectItem(not_null<PeerData*> peer, PeerListRow::SetStyle style) {
	if (!_select) {
		createMultiSelect();
		_select->hide(anim::type::instant);
	}
	const auto respect = _controller->respectSavedMessagesChat();
	const auto text = (respect && peer->isSelf())
		? lang(lng_saved_short)
		: peer->shortName();
	const auto callback = PaintUserpicCallback(peer, respect);
	if (style == PeerListRow::SetStyle::Fast) {
		_select->entity()->addItemInBunch(
			peer->id,
			text,
			st::activeButtonBg,
			std::move(callback));
	} else {
		_select->entity()->addItem(
			peer->id,
			text,
			st::activeButtonBg,
			std::move(callback));
	}
}

void PeerListBox::peerListFinishSelectedRowsBunch() {
	Expects(_select != nullptr);
	_select->entity()->finishItemsBunch();
}

bool PeerListBox::peerListIsRowSelected(not_null<PeerData*> peer) {
	return _select ? _select->entity()->hasItem(peer->id) : false;
}

int PeerListBox::peerListSelectedRowsCount() {
	return _select ? _select->entity()->getItemsCount() : 0;
}

std::vector<not_null<PeerData*>> PeerListBox::peerListCollectSelectedRows() {
	auto result = std::vector<not_null<PeerData*>> {};
	auto items = _select ? _select->entity()->getItems() : QVector<uint64> {};
	if (!items.empty()) {
		result.reserve(items.size());
		for_const (auto itemId, items) {
			result.push_back(App::peer(itemId));
		}
	}
	return result;
}

PeerListRow::PeerListRow(not_null<PeerData*> peer) : PeerListRow(peer, peer->id) {
}

PeerListRow::PeerListRow(not_null<PeerData*> peer, PeerListRowId id)
: _id(id)
, _peer(peer)
, _initialized(false)
, _isSearchResult(false)
, _isSavedMessagesChat(false) {
}

bool PeerListRow::checked() const {
	return _checkbox && _checkbox->checked();
}

void PeerListRow::setCustomStatus(const QString &status) {
	setStatusText(status);
	_statusType = StatusType::Custom;
	_statusValidTill = 0;
}

void PeerListRow::clearCustomStatus() {
	_statusType = StatusType::Online;
	refreshStatus();
}

void PeerListRow::refreshStatus() {
	if (!_initialized || _statusType == StatusType::Custom) {
		return;
	}
	_statusType = StatusType::LastSeen;
	_statusValidTill = 0;
	if (auto user = peer()->asUser()) {
		if (_isSavedMessagesChat) {
			setStatusText(lang(lng_saved_forward_here));
		} else {
			auto time = unixtime();
			setStatusText(Data::OnlineText(user, time));
			if (Data::OnlineTextActive(user, time)) {
				_statusType = StatusType::Online;
			}
			_statusValidTill = getms()
				+ Data::OnlineChangeTimeout(user, time);
		}
	} else if (auto chat = peer()->asChat()) {
		if (!chat->amIn()) {
			setStatusText(lang(lng_chat_status_unaccessible));
		} else if (chat->count > 0) {
			setStatusText(lng_chat_status_members(lt_count, chat->count));
		} else {
			setStatusText(lang(lng_group_status));
		}
	} else if (peer()->isMegagroup()) {
		setStatusText(lang(lng_group_status));
	} else if (peer()->isChannel()) {
		setStatusText(lang(lng_channel_status));
	}
}

TimeMs PeerListRow::refreshStatusTime() const {
	return _statusValidTill;
}

void PeerListRow::refreshName(const style::PeerListItem &st) {
	if (!_initialized) {
		return;
	}
	const auto text = _isSavedMessagesChat
		? lang(lng_saved_messages)
		: peer()->name;
	_name.setText(st.nameStyle, text, Ui::NameTextOptions());
}

PeerListRow::~PeerListRow() = default;

void PeerListRow::invalidatePixmapsCache() {
	if (_checkbox) {
		_checkbox->invalidateCache();
	}
}

int PeerListRow::nameIconWidth() const {
	return _peer->isVerified() ? st::dialogsVerifiedIcon.width() : 0;
}

void PeerListRow::paintNameIcon(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		bool selected) {
	st::dialogsVerifiedIcon.paint(p, x, y, outerWidth);
}

void PeerListRow::paintStatusText(
		Painter &p,
		const style::PeerListItem &st,
		int x,
		int y,
		int availableWidth,
		int outerWidth,
		bool selected) {
	auto statusHasOnlineColor = (_statusType == PeerListRow::StatusType::Online);
	p.setFont(st::contactsStatusFont);
	p.setPen(statusHasOnlineColor ? st.statusFgActive : (selected ? st.statusFgOver : st.statusFg));
	_status.drawLeftElided(p, x, y, availableWidth, outerWidth);
}

template <typename UpdateCallback>
void PeerListRow::addRipple(const style::PeerListItem &st, QSize size, QPoint point, UpdateCallback updateCallback) {
	if (!_ripple) {
		auto mask = Ui::RippleAnimation::rectMask(size);
		_ripple = std::make_unique<Ui::RippleAnimation>(st.button.ripple, std::move(mask), std::move(updateCallback));
	}
	_ripple->add(point);
}

void PeerListRow::stopLastRipple() {
	if (_ripple) {
		_ripple->lastStop();
	}
}

void PeerListRow::paintRipple(Painter &p, TimeMs ms, int x, int y, int outerWidth) {
	if (_ripple) {
		_ripple->paint(p, x, y, outerWidth, ms);
		if (_ripple->empty()) {
			_ripple.reset();
		}
	}
}

void PeerListRow::paintUserpic(
		Painter &p,
		const style::PeerListItem &st,
		TimeMs ms,
		int x,
		int y,
		int outerWidth) {
	if (_disabledState == State::DisabledChecked) {
		paintDisabledCheckUserpic(p, st, x, y, outerWidth);
	} else if (_checkbox) {
		_checkbox->paint(p, ms, x, y, outerWidth);
	} else if (_isSavedMessagesChat) {
		Ui::EmptyUserpic::PaintSavedMessages(p, x, y, outerWidth, st.photoSize);
	} else {
		peer()->paintUserpicLeft(p, x, y, outerWidth, st.photoSize);
	}
}

// Emulates Ui::RoundImageCheckbox::paint() in a checked state.
void PeerListRow::paintDisabledCheckUserpic(
		Painter &p,
		const style::PeerListItem &st,
		int x,
		int y,
		int outerWidth) const {
	auto userpicRadius = st::contactsPhotoCheckbox.imageSmallRadius;
	auto userpicShift = st::contactsPhotoCheckbox.imageRadius - userpicRadius;
	auto userpicDiameter = st::contactsPhotoCheckbox.imageRadius * 2;
	auto userpicLeft = x + userpicShift;
	auto userpicTop = y + userpicShift;
	auto userpicEllipse = rtlrect(x, y, userpicDiameter, userpicDiameter, outerWidth);
	auto userpicBorderPen = st::contactsPhotoDisabledCheckFg->p;
	userpicBorderPen.setWidth(st::contactsPhotoCheckbox.selectWidth);

	auto iconDiameter = st::contactsPhotoCheckbox.check.size;
	auto iconLeft = x + userpicDiameter + st::contactsPhotoCheckbox.selectWidth - iconDiameter;
	auto iconTop = y + userpicDiameter + st::contactsPhotoCheckbox.selectWidth - iconDiameter;
	auto iconEllipse = rtlrect(iconLeft, iconTop, iconDiameter, iconDiameter, outerWidth);
	auto iconBorderPen = st::contactsPhotoCheckbox.check.border->p;
	iconBorderPen.setWidth(st::contactsPhotoCheckbox.selectWidth);

	if (_isSavedMessagesChat) {
		Ui::EmptyUserpic::PaintSavedMessages(p, userpicLeft, userpicTop, outerWidth, userpicRadius * 2);
	} else {
		peer()->paintUserpicLeft(p, userpicLeft, userpicTop, outerWidth, userpicRadius * 2);
	}

	{
		PainterHighQualityEnabler hq(p);

		p.setPen(userpicBorderPen);
		p.setBrush(Qt::NoBrush);
		p.drawEllipse(userpicEllipse);

		p.setPen(iconBorderPen);
		p.setBrush(st::contactsPhotoDisabledCheckFg);
		p.drawEllipse(iconEllipse);
	}

	st::contactsPhotoCheckbox.check.check.paint(p, iconEllipse.topLeft(), outerWidth);
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

void PeerListRow::createCheckbox(base::lambda<void()> updateCallback) {
	_checkbox = std::make_unique<Ui::RoundImageCheckbox>(
		st::contactsPhotoCheckbox,
		std::move(updateCallback),
		PaintUserpicCallback(_peer, _isSavedMessagesChat));
}

void PeerListRow::setCheckedInternal(bool checked, SetStyle style) {
	Expects(_checkbox != nullptr);
	using CheckboxStyle = Ui::RoundCheckbox::SetStyle;
	auto speed = (style == SetStyle::Animated) ? CheckboxStyle::Animated : CheckboxStyle::Fast;
	_checkbox->setChecked(checked, speed);
}

PeerListContent::PeerListContent(
	QWidget *parent,
	not_null<PeerListController*> controller,
	const style::PeerList &st)
: RpWidget(parent)
, _st(st)
, _controller(controller)
, _rowHeight(_st.item.height) {
	subscribe(Auth().downloaderTaskFinished(), [this] { update(); });

	using UpdateFlag = Notify::PeerUpdate::Flag;
	auto changes = UpdateFlag::NameChanged | UpdateFlag::PhotoChanged;
	subscribe(Notify::PeerUpdated(), Notify::PeerUpdatedHandler(changes, [this](const Notify::PeerUpdate &update) {
		if (update.flags & UpdateFlag::PhotoChanged) {
			this->update();
		} else if (update.flags & UpdateFlag::NameChanged) {
			handleNameChanged(update);
		}
	}));
	subscribe(Window::Theme::Background(), [this](const Window::Theme::BackgroundUpdate &update) {
		if (update.paletteChanged()) {
			invalidatePixmapsCache();
		}
	});
	_repaintByStatus.setCallback([this] { update(); });
}

void PeerListContent::appendRow(std::unique_ptr<PeerListRow> row) {
	Expects(row != nullptr);
	if (_rowsById.find(row->id()) == _rowsById.cend()) {
		row->setAbsoluteIndex(_rows.size());
		addRowEntry(row.get());
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

void PeerListContent::changeCheckState(not_null<PeerListRow*> row, bool checked, PeerListRow::SetStyle style) {
	row->setChecked(
		checked,
		style,
		[this, row] { updateRow(row); });
}

void PeerListContent::addRowEntry(not_null<PeerListRow*> row) {
	if (_controller->respectSavedMessagesChat() && row->peer()->isSelf()) {
		row->setIsSavedMessagesChat(true);
	}
	_rowsById.emplace(row->id(), row);
	_rowsByPeer[row->peer()].push_back(row);
	if (addingToSearchIndex()) {
		addToSearchIndex(row);
	}
	if (_controller->isRowSelected(row->peer())) {
		Assert(row->id() == row->peer()->id);
		changeCheckState(row, true, PeerListRow::SetStyle::Fast);
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
	row->setNameFirstChars(row->peer()->nameFirstChars());
	for (auto ch : row->nameFirstChars()) {
		_searchIndex[ch].push_back(row);
	}
}

void PeerListContent::removeFromSearchIndex(not_null<PeerListRow*> row) {
	auto &nameFirstChars = row->nameFirstChars();
	if (!nameFirstChars.empty()) {
		for (auto ch : row->nameFirstChars()) {
			auto it = _searchIndex.find(ch);
			if (it != _searchIndex.cend()) {
				auto &entry = it->second;
				entry.erase(std::remove(entry.begin(), entry.end(), row), entry.end());
				if (entry.empty()) {
					_searchIndex.erase(it);
				}
			}
		}
		row->setNameFirstChars({});
	}
}

void PeerListContent::prependRow(std::unique_ptr<PeerListRow> row) {
	Expects(row != nullptr);
	if (_rowsById.find(row->id()) == _rowsById.cend()) {
		addRowEntry(row.get());
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
	auto &byPeer = _rowsByPeer[row->peer()];
	byPeer.erase(std::remove(byPeer.begin(), byPeer.end(), row), byPeer.end());
	removeFromSearchIndex(row);
	_filterResults.erase(
		std::find(_filterResults.begin(), _filterResults.end(), row),
		_filterResults.end());
	removeRowAtIndex(eraseFrom, index);

	restoreSelection();
	setPressed(restoreSelectedData(pressedData));
	setContexted(restoreSelectedData(contextedData));
}

void PeerListContent::clearAllContent() {
	setSelected(Selected());
	setPressed(Selected());
	setContexted(Selected());
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
	row->setAbsoluteIndex(_searchRows.size());
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

void PeerListContent::setAboveWidget(object_ptr<TWidget> aboveWidget) {
	_aboveWidget = std::move(aboveWidget);
	if (_aboveWidget) {
		_aboveWidget->setParent(this);
	}
}

int PeerListContent::labelHeight() const {
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
		if (_controller->isSearchLoading()) {
			return computeLabelHeight(_searchLoading);
		}
		return computeLabelHeight(_searchNoResults);
	}
	return computeLabelHeight(_description);
}

void PeerListContent::refreshRows() {
	resizeToWidth(width());
	if (_visibleBottom > 0) {
		checkScrollForPreload();
	}
	updateSelection();
	update();
}

void PeerListContent::setSearchMode(PeerListSearchMode mode) {
	if (_searchMode != mode) {
		if (!addingToSearchIndex()) {
			for_const (auto &row, _rows) {
				addToSearchIndex(row.get());
			}
		}
		_searchMode = mode;
		if (_controller->hasComplexSearch()) {
			if (!_searchLoading) {
				setSearchLoading(object_ptr<Ui::FlatLabel>(
					this,
					lang(lng_contacts_loading),
					Ui::FlatLabel::InitType::Simple,
					st::membersAbout));
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

	auto clip = e->rect();
	p.fillRect(clip, _st.item.button.textBg);

	auto repaintByStatusAfter = _repaintByStatus.remainingTime();
	auto repaintAfterMin = repaintByStatusAfter;

	auto rowsTopCached = rowsTop();
	auto ms = getms();
	auto yFrom = clip.y() - rowsTopCached;
	auto yTo = clip.y() + clip.height() - rowsTopCached;
	p.translate(0, rowsTopCached);
	auto count = shownRowsCount();
	if (count > 0) {
		auto from = floorclamp(yFrom, _rowHeight, 0, count);
		auto to = ceilclamp(yTo, _rowHeight, 0, count);
		p.translate(0, from * _rowHeight);
		for (auto index = from; index != to; ++index) {
			auto repaintAfter = paintRow(p, ms, RowIndex(index));
			if (repaintAfter >= 0
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
	_aboveHeight = 0;
	if (_aboveWidget) {
		_aboveWidget->resizeToWidth(newWidth);
		_aboveWidget->moveToLeft(0, 0, newWidth);
		if (showingSearch()) {
			_aboveWidget->hide();
		} else {
			_aboveWidget->show();
			_aboveHeight = _aboveWidget->height();
		}
	}
	auto rowsCount = shownRowsCount();
	auto labelTop = rowsTop() + qMax(1, shownRowsCount()) * _rowHeight;
	auto labelWidth = newWidth - 2 * st::contactsPadding.left();
	if (_description) {
		_description->resizeToWidth(labelWidth);
		_description->moveToLeft(st::contactsPadding.left(), labelTop + st::membersAboutLimitPadding.top(), newWidth);
		_description->setVisible(!showingSearch());
	}
	if (_searchNoResults) {
		_searchNoResults->resizeToWidth(labelWidth);
		_searchNoResults->moveToLeft(st::contactsPadding.left(), labelTop + st::membersAboutLimitPadding.top(), newWidth);
		_searchNoResults->setVisible(showingSearch() && _filterResults.empty() && !_controller->isSearchLoading());
	}
	if (_searchLoading) {
		_searchLoading->resizeToWidth(labelWidth);
		_searchLoading->moveToLeft(st::contactsPadding.left(), labelTop + st::membersAboutLimitPadding.top(), newWidth);
		_searchLoading->setVisible(showingSearch() && _filterResults.empty() && _controller->isSearchLoading());
	}
	auto label = labelHeight();
	return ((label > 0 || rowsCount > 0) ? (labelTop + label) : 0)
		+ _st.padding.bottom();
}

void PeerListContent::enterEventHook(QEvent *e) {
	setMouseTracking(true);
}

void PeerListContent::leaveEventHook(QEvent *e) {
	_mouseSelection = false;
	setMouseTracking(false);
	setSelected(Selected());
}

void PeerListContent::mouseMoveEvent(QMouseEvent *e) {
	handleMouseMove(e->globalPos());
}

void PeerListContent::handleMouseMove(QPoint position) {
	if (_mouseSelection || _lastMousePosition != position) {
		_lastMousePosition = position;
		_mouseSelection = true;
		updateSelection();
	}
}

void PeerListContent::mousePressEvent(QMouseEvent *e) {
	_pressButton = e->button();
	_mouseSelection = true;
	_lastMousePosition = e->globalPos();
	updateSelection();

	setPressed(_selected);
	if (auto row = getRow(_selected.index)) {
		auto updateCallback = [this, row, hint = _selected.index] {
			updateRow(row, hint);
		};
		if (_selected.action) {
			auto actionRect = getActionRect(row, _selected.index);
			if (!actionRect.isEmpty()) {
				auto point = mapFromGlobal(QCursor::pos()) - actionRect.topLeft();
				row->addActionRipple(point, std::move(updateCallback));
			}
		} else {
			auto size = QSize(width(), _rowHeight);
			auto point = mapFromGlobal(QCursor::pos()) - QPoint(0, getRowTop(_selected.index));
			row->addRipple(_st.item, size, point, std::move(updateCallback));
		}
	}
}

void PeerListContent::mouseReleaseEvent(QMouseEvent *e) {
	mousePressReleased(e->button());
}

void PeerListContent::mousePressReleased(Qt::MouseButton button) {
	updateRow(_pressed.index);
	updateRow(_selected.index);

	auto pressed = _pressed;
	setPressed(Selected());
	if (button == Qt::LeftButton && pressed == _selected) {
		if (auto row = getRow(pressed.index)) {
			if (pressed.action) {
				_controller->rowActionClicked(row);
			} else {
				_controller->rowClicked(row);
			}
		}
	}
}

void PeerListContent::contextMenuEvent(QContextMenuEvent *e) {
	if (_contextMenu) {
		_contextMenu->deleteLater();
		_contextMenu = nullptr;
	}
	setContexted(Selected());
	if (e->reason() == QContextMenuEvent::Mouse) {
		handleMouseMove(e->globalPos());
	}

	setContexted(_selected);
	if (_pressButton != Qt::LeftButton) {
		mousePressReleased(_pressButton);
	}

	if (auto row = getRow(_contexted.index)) {
		_contextMenu = _controller->rowContextMenu(row);
		if (_contextMenu) {
			_contextMenu->setDestroyedCallback(base::lambda_guarded(
				this,
				[this] {
					setContexted(Selected());
					handleMouseMove(QCursor::pos());
				}));
			_contextMenu->popup(e->globalPos());
			e->accept();
		}
	}
}

void PeerListContent::setPressed(Selected pressed) {
	if (auto row = getRow(_pressed.index)) {
		row->stopLastRipple();
		row->stopLastActionRipple();
	}
	_pressed = pressed;
}

TimeMs PeerListContent::paintRow(Painter &p, TimeMs ms, RowIndex index) {
	auto row = getRow(index);
	Assert(row != nullptr);

	row->lazyInitialize(_st.item);

	auto refreshStatusAt = row->refreshStatusTime();
	if (refreshStatusAt >= 0 && ms >= refreshStatusAt) {
		row->refreshStatus();
		refreshStatusAt = row->refreshStatusTime();
	}

	auto peer = row->peer();
	auto user = peer->asUser();
	auto active = (_contexted.index.value >= 0)
		? _contexted
		: (_pressed.index.value >= 0)
		? _pressed
		: _selected;
	auto selected = (active.index == index);
	auto actionSelected = (selected && active.action);

	auto &bg = selected
		? _st.item.button.textBgOver
		: _st.item.button.textBg;
	p.fillRect(0, 0, width(), _rowHeight, bg);
	row->paintRipple(p, ms, 0, 0, width());
	row->paintUserpic(
		p,
		_st.item,
		ms,
		_st.item.photoPosition.x(),
		_st.item.photoPosition.y(),
		width());

	p.setPen(st::contactsNameFg);

	auto skipRight = _st.item.photoPosition.x();
	auto actionSize = row->actionSize();
	auto actionMargins = actionSize.isEmpty() ? QMargins() : row->actionMargins();
	auto &name = row->name();
	auto namex = _st.item.namePosition.x();
	auto namew = width() - namex - skipRight;
	if (!actionSize.isEmpty()) {
		namew -= actionMargins.left()
			+ actionSize.width()
			+ actionMargins.right()
			- skipRight;
	}
	auto statusw = namew;
	if (auto iconWidth = row->nameIconWidth()) {
		namew -= iconWidth;
		row->paintNameIcon(
			p,
			namex + qMin(name.maxWidth(), namew),
			_st.item.namePosition.y(),
			width(),
			selected);
	}
	auto nameCheckedRatio = row->disabled() ? 0. : row->checkedRatio();
	p.setPen(anim::pen(st::contactsNameFg, st::contactsNameCheckedFg, nameCheckedRatio));
	name.drawLeftElided(p, namex, _st.item.namePosition.y(), namew, width());

	if (!actionSize.isEmpty()) {
		auto actionLeft = width()
			- actionMargins.right()
			- actionSize.width();
		auto actionTop = actionMargins.top();
		row->paintAction(
			p,
			ms,
			actionLeft,
			actionTop,
			width(),
			selected,
			actionSelected);
	}

	p.setFont(st::contactsStatusFont);
	if (row->isSearchResult() && !_mentionHighlight.isEmpty() && peer->userName().startsWith(_mentionHighlight, Qt::CaseInsensitive)) {
		auto username = peer->userName();
		auto availableWidth = statusw;
		auto highlightedPart = '@' + username.mid(0, _mentionHighlight.size());
		auto grayedPart = username.mid(_mentionHighlight.size());
		auto highlightedWidth = st::contactsStatusFont->width(highlightedPart);
		if (highlightedWidth >= availableWidth || grayedPart.isEmpty()) {
			if (highlightedWidth > availableWidth) {
				highlightedPart = st::contactsStatusFont->elided(highlightedPart, availableWidth);
			}
			p.setPen(_st.item.statusFgActive);
			p.drawTextLeft(_st.item.statusPosition.x(), _st.item.statusPosition.y(), width(), highlightedPart);
		} else {
			grayedPart = st::contactsStatusFont->elided(grayedPart, availableWidth - highlightedWidth);
			auto grayedWidth = st::contactsStatusFont->width(grayedPart);
			p.setPen(_st.item.statusFgActive);
			p.drawTextLeft(_st.item.statusPosition.x(), _st.item.statusPosition.y(), width(), highlightedPart);
			p.setPen(selected ? _st.item.statusFgOver : _st.item.statusFg);
			p.drawTextLeft(_st.item.statusPosition.x() + highlightedWidth, _st.item.statusPosition.y(), width(), grayedPart);
		}
	} else {
		row->paintStatusText(p, _st.item, _st.item.statusPosition.x(), _st.item.statusPosition.y(), statusw, width(), selected);
	}
	return (refreshStatusAt - ms);
}

void PeerListContent::selectSkip(int direction) {
	if (_pressed.index.value >= 0) {
		return;
	}
	_mouseSelection = false;

	auto newSelectedIndex = _selected.index.value + direction;

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
	newSelectedIndex = snap(newSelectedIndex, firstEnabled - 1, lastEnabled);

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

	_selected.index.value = newSelectedIndex;
	_selected.action = false;
	if (newSelectedIndex >= 0) {
		auto top = (newSelectedIndex > 0) ? getRowTop(RowIndex(newSelectedIndex)) : 0;
		auto bottom = (newSelectedIndex + 1 < rowsCount) ? getRowTop(RowIndex(newSelectedIndex + 1)) : height();
		_scrollToRequests.fire({ top, bottom });
	}

	update();
}

void PeerListContent::selectSkipPage(int height, int direction) {
	auto rowsToSkip = height / _rowHeight;
	if (!rowsToSkip) return;
	selectSkip(rowsToSkip * direction);
}

void PeerListContent::loadProfilePhotos() {
	if (_visibleTop >= _visibleBottom) return;

	auto yFrom = _visibleTop;
	auto yTo = _visibleBottom + (_visibleBottom - _visibleTop) * PreloadHeightsCount;
	Auth().downloader().clearPriorities();

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
				getRow(RowIndex(index))->peer()->loadUserpic();
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
	auto searchWordsList = TextUtilities::PrepareSearchWords(query);
	auto normalizedQuery = searchWordsList.join(' ');
	if (_normalizedSearchQuery != normalizedQuery) {
		setSearchQuery(query, normalizedQuery);
		if (_controller->searchInLocal() && !searchWordsList.isEmpty()) {
			auto minimalList = (const std::vector<not_null<PeerListRow*>>*)nullptr;
			for_const (auto &searchWord, searchWordsList) {
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
						not_null<PeerData*> peer,
						const QString &searchWord) {
					for (auto &nameWord : peer->nameWords()) {
						if (nameWord.startsWith(searchWord)) {
							return true;
						}
					}
					return false;
				};
				auto allSearchWordsInNames = [&](
						not_null<PeerData*> peer) {
					for_const (auto &searchWord, searchWordsList) {
						if (!searchWordInNames(peer, searchWord)) {
							return false;
						}
					}
					return true;
				};

				_filterResults.reserve(minimalList->size());
				for_const (auto row, *minimalList) {
					if (allSearchWordsInNames(row->peer())) {
						_filterResults.push_back(row);
					}
				}
			}
		}
		if (_controller->hasComplexSearch()) {
			_controller->search(_searchQuery);
		}
		refreshRows();
		restoreSelection();
	}
}

std::unique_ptr<PeerListState> PeerListContent::saveState() const {
	auto result = std::make_unique<PeerListState>();
	result->controllerState
		= std::make_unique<PeerListController::SavedStateBase>();
	result->list.reserve(_rows.size());
	for (auto &row : _rows) {
		result->list.push_back(row->peer());
	}
	result->filterResults.reserve(_filterResults.size());
	for (auto &row : _filterResults) {
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
		if (auto existingRow = findRow(peer->id)) {
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
	_searchQuery = query;
	_normalizedSearchQuery = normalizedQuery;
	_mentionHighlight = _searchQuery.startsWith('@')
		? _searchQuery.mid(1)
		: _searchQuery;
	_filterResults.clear();
	clearSearchRows();
}

void PeerListContent::submitted() {
	if (const auto row = getRow(_selected.index)) {
		_controller->rowClicked(row);
	} else if (showingSearch()) {
		if (const auto row = getRow(RowIndex(0))) {
			_controller->rowClicked(row);
		}
	}
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
	if (_selected != selected) {
		_selected = selected;
		updateRow(_selected.index);
		setCursor(_selected.action ? style::cur_pointer : style::cur_default);
	}
}

void PeerListContent::setContexted(Selected contexted) {
	updateRow(_contexted.index);
	if (_contexted != contexted) {
		_contexted = contexted;
		updateRow(_contexted.index);
	}
}

void PeerListContent::restoreSelection() {
	_lastMousePosition = QCursor::pos();
	updateSelection();
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

void PeerListContent::updateSelection() {
	if (!_mouseSelection) return;

	auto point = mapFromGlobal(_lastMousePosition);
	auto in = parentWidget()->rect().contains(parentWidget()->mapFromGlobal(_lastMousePosition));
	auto selected = Selected();
	auto rowsPointY = point.y() - rowsTop();
	selected.index.value = (in && rowsPointY >= 0 && rowsPointY < shownRowsCount() * _rowHeight) ? (rowsPointY / _rowHeight) : -1;
	if (selected.index.value >= 0) {
		auto row = getRow(selected.index);
		if (row->disabled()) {
			selected = Selected();
		} else {
			if (getActionRect(row, selected.index).contains(point)) {
				selected.action = true;
			}
		}
	}
	setSelected(selected);
}

QRect PeerListContent::getActionRect(not_null<PeerListRow*> row, RowIndex index) const {
	auto actionSize = row->actionSize();
	if (actionSize.isEmpty()) {
		return QRect();
	}
	auto actionMargins = row->actionMargins();
	auto actionRight = actionMargins.right();
	auto actionTop = actionMargins.top();
	auto actionLeft = width() - actionRight - actionSize.width();
	auto rowTop = getRowTop(index);
	return myrtlrect(actionLeft, rowTop + actionTop, actionSize.width(), actionSize.height());
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
	auto row = getRow(index);
	if (row->disabled()) {
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

void PeerListContent::handleNameChanged(const Notify::PeerUpdate &update) {
	auto byPeer = _rowsByPeer.find(update.peer);
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
