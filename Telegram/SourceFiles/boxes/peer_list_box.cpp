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

#include "styles/style_boxes.h"
#include "styles/style_dialogs.h"
#include "auth_session.h"
#include "mainwidget.h"
#include "ui/widgets/multi_select.h"
#include "ui/widgets/labels.h"
#include "ui/effects/round_checkbox.h"
#include "ui/effects/ripple_animation.h"
#include "ui/effects/widget_slide_wrap.h"
#include "lang/lang_keys.h"
#include "observer_peer.h"
#include "storage/file_download.h"
#include "window/themes/window_theme.h"

PeerListBox::PeerListBox(QWidget*, std::unique_ptr<PeerListController> controller, base::lambda<void(not_null<PeerListBox*>)> init)
: _controller(std::move(controller))
, _init(std::move(init)) {
	Expects(_controller != nullptr);
}

void PeerListBox::createMultiSelect() {
	Expects(_select == nullptr);

	auto entity = object_ptr<Ui::MultiSelect>(this, st::contactsMultiSelect, langFactory(lng_participant_filter));
	auto margins = style::margins(0, 0, 0, 0);
	auto callback = [this] { updateScrollSkips(); };
	_select.create(this, std::move(entity), margins, std::move(callback));
	_select->entity()->setSubmittedCallback([this](bool chtrlShiftEnter) { _inner->submitted(); });
	_select->entity()->setQueryChangedCallback([this](const QString &query) { searchQueryChanged(query); });
	_select->entity()->setItemRemovedCallback([this](uint64 itemId) {
		if (auto peer = App::peerLoaded(itemId)) {
			if (auto row = peerListFindRow(peer->id)) {
				_inner->changeCheckState(row, false, PeerListRow::SetStyle::Animated);
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
	_inner = setInnerWidget(object_ptr<Inner>(this, _controller.get()), st::boxLayerScroll);

	_controller->setDelegate(this);

	setDimensions(st::boxWideWidth, st::boxMaxListHeight);
	if (_select) {
		_select->finishAnimation();
		_scrollBottomFixed = true;
		onScrollToY(0);
	}

	connect(_inner, SIGNAL(mustScrollTo(int, int)), this, SLOT(onScrollToY(int, int)));

	if (_init) {
		_init(this);
	}
}

void PeerListBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Down) {
		_inner->selectSkip(1);
	} else if (e->key() == Qt::Key_Up) {
		_inner->selectSkip(-1);
	} else if (e->key() == Qt::Key_PageDown) {
		_inner->selectSkipPage(height(), 1);
	} else if (e->key() == Qt::Key_PageUp) {
		_inner->selectSkipPage(height(), -1);
	} else if (e->key() == Qt::Key_Escape && _select && !_select->entity()->getQuery().isEmpty()) {
		_select->entity()->clearQuery();
	} else {
		BoxContent::keyPressEvent(e);
	}
}

void PeerListBox::searchQueryChanged(const QString &query) {
	onScrollToY(0);
	_inner->searchQueryChanged(query);
}

void PeerListBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	if (_select) {
		_select->resizeToWidth(width());
		_select->moveToLeft(0, 0);

		updateScrollSkips();
	}

	_inner->resizeToWidth(width());
}

void PeerListBox::paintEvent(QPaintEvent *e) {
	Painter p(this);
	for (auto rect : e->region().rects()) {
		p.fillRect(rect, st::contactsBg);
	}
}

void PeerListBox::setInnerFocus() {
	if (!_select || _select->isHiddenOrHiding()) {
		_inner->setFocus();
	} else {
		_select->entity()->setInnerFocus();
	}
}

void PeerListBox::peerListAppendRow(std::unique_ptr<PeerListRow> row) {
	_inner->appendRow(std::move(row));
}

void PeerListBox::peerListAppendSearchRow(std::unique_ptr<PeerListRow> row) {
	_inner->appendSearchRow(std::move(row));
}

void PeerListBox::peerListAppendFoundRow(not_null<PeerListRow*> row) {
	_inner->appendFoundRow(row);
}

void PeerListBox::peerListPrependRow(std::unique_ptr<PeerListRow> row) {
	_inner->prependRow(std::move(row));
}

void PeerListBox::peerListPrependRowFromSearchResult(not_null<PeerListRow*> row) {
	_inner->prependRowFromSearchResult(row);
}

PeerListRow *PeerListBox::peerListFindRow(PeerListRowId id) {
	return _inner->findRow(id);
}

void PeerListBox::peerListUpdateRow(not_null<PeerListRow*> row) {
	_inner->updateRow(row);
}

void PeerListBox::peerListRemoveRow(not_null<PeerListRow*> row) {
	_inner->removeRow(row);
}

void PeerListBox::peerListConvertRowToSearchResult(not_null<PeerListRow*> row) {
	_inner->convertRowToSearchResult(row);
}

void PeerListBox::peerListSetRowChecked(not_null<PeerListRow*> row, bool checked) {
	auto peer = row->peer();
	if (checked) {
		addSelectItem(peer, PeerListRow::SetStyle::Animated);
		_inner->changeCheckState(row, checked, PeerListRow::SetStyle::Animated);
		peerListUpdateRow(row);

		// This call deletes row from _searchRows.
		_select->entity()->clearQuery();
	} else {
		// The itemRemovedCallback will call changeCheckState() here.
		_select->entity()->removeItem(peer->id);
		peerListUpdateRow(row);
	}
}

int PeerListBox::peerListFullRowsCount() {
	return _inner->fullRowsCount();
}

not_null<PeerListRow*> PeerListBox::peerListRowAt(int index) {
	return _inner->rowAt(index);
}

void PeerListBox::peerListRefreshRows() {
	_inner->refreshRows();
}

void PeerListBox::peerListScrollToTop() {
	onScrollToY(0);
}

void PeerListBox::peerListSetDescription(object_ptr<Ui::FlatLabel> description) {
	_inner->setDescription(std::move(description));
}

void PeerListBox::peerListSetSearchLoading(object_ptr<Ui::FlatLabel> loading) {
	_inner->setSearchLoading(std::move(loading));
}

void PeerListBox::peerListSetSearchNoResults(object_ptr<Ui::FlatLabel> noResults) {
	_inner->setSearchNoResults(std::move(noResults));
}

void PeerListBox::peerListSetAboveWidget(object_ptr<TWidget> aboveWidget) {
	_inner->setAboveWidget(std::move(aboveWidget));
}

void PeerListBox::peerListSetSearchMode(PeerListSearchMode mode) {
	_inner->setSearchMode(mode);
	auto selectVisible = (mode != PeerListSearchMode::Disabled);
	if (selectVisible && !_select) {
		createMultiSelect();
		_select->toggleFast(!selectVisible);
	}
	if (_select) {
		_select->toggleAnimated(selectVisible);
		_scrollBottomFixed = false;
		setInnerFocus();
	}
}

void PeerListBox::peerListSortRows(base::lambda<bool(PeerListRow &a, PeerListRow &b)> compare) {
	_inner->reorderRows([compare = std::move(compare)](auto &&begin, auto &&end) {
		std::sort(begin, end, [&compare](auto &&a, auto &&b) {
			return compare(*a, *b);
		});
	});
}

void PeerListBox::peerListPartitionRows(base::lambda<bool(PeerListRow &a)> border) {
	_inner->reorderRows([border = std::move(border)](auto &&begin, auto &&end) {
		std::stable_partition(begin, end, [&border](auto &&current) {
			return border(*current);
		});
	});
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

void PeerListBox::addSelectItem(not_null<PeerData*> peer, PeerListRow::SetStyle style) {
	if (!_select) {
		createMultiSelect();
		_select->toggleFast(false);
	}
	if (style == PeerListRow::SetStyle::Fast) {
		_select->entity()->addItemInBunch(peer->id, peer->shortName(), st::activeButtonBg, PaintUserpicCallback(peer));
	} else {
		_select->entity()->addItem(peer->id, peer->shortName(), st::activeButtonBg, PaintUserpicCallback(peer), Ui::MultiSelect::AddItemWay::Default);
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
	Expects(_select != nullptr);
	return _select->entity()->getItemsCount();
}

std::vector<not_null<PeerData*>> PeerListBox::peerListCollectSelectedRows() {
	Expects(_select != nullptr);
	auto result = std::vector<not_null<PeerData*>>();
	auto items = _select->entity()->getItems();
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
, _isSearchResult(false) {
}

bool PeerListRow::checked() const {
	return _checkbox && _checkbox->checked();
}

void PeerListRow::setCustomStatus(const QString &status) {
	setStatusText(status);
	_statusType = StatusType::Custom;
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
	if (auto user = peer()->asUser()) {
		auto time = unixtime();
		setStatusText(App::onlineText(user, time));
		if (App::onlineColorUse(user, time)) {
			_statusType = StatusType::Online;
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

void PeerListRow::refreshName() {
	if (!_initialized) {
		return;
	}
	_name.setText(st::contactsNameStyle, peer()->name, _textNameOptions);
}

PeerListRow::~PeerListRow() = default;

void PeerListRow::invalidatePixmapsCache() {
	if (_checkbox) {
		_checkbox->invalidateCache();
	}
}

void PeerListRow::paintStatusText(Painter &p, int x, int y, int availableWidth, int outerWidth, bool selected) {
	auto statusHasOnlineColor = (_statusType == PeerListRow::StatusType::Online);
	p.setFont(st::contactsStatusFont);
	p.setPen(statusHasOnlineColor ? st::contactsStatusFgOnline : (selected ? st::contactsStatusFgOver : st::contactsStatusFg));
	_status.drawLeftElided(p, x, y, availableWidth, outerWidth);
}

template <typename UpdateCallback>
void PeerListRow::addRipple(QSize size, QPoint point, UpdateCallback updateCallback) {
	if (!_ripple) {
		auto mask = Ui::RippleAnimation::rectMask(size);
		_ripple = std::make_unique<Ui::RippleAnimation>(st::contactsRipple, std::move(mask), std::move(updateCallback));
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

void PeerListRow::paintUserpic(Painter &p, TimeMs ms, int x, int y, int outerWidth) {
	if (_disabledState == State::DisabledChecked) {
		paintDisabledCheckUserpic(p, x, y, outerWidth);
	} else if (_checkbox) {
		_checkbox->paint(p, ms, x, y, outerWidth);
	} else {
		peer()->paintUserpicLeft(p, x, y, outerWidth, st::contactsPhotoSize);
	}
}

// Emulates Ui::RoundImageCheckbox::paint() in a checked state.
void PeerListRow::paintDisabledCheckUserpic(Painter &p, int x, int y, int outerWidth) const {
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

	peer()->paintUserpicLeft(p, userpicLeft, userpicTop, outerWidth, userpicRadius * 2);

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
	_status.setText(st::defaultTextStyle, text, _textNameOptions);
}

float64 PeerListRow::checkedRatio() {
	return _checkbox ? _checkbox->checkedAnimationRatio() : 0.;
}

void PeerListRow::lazyInitialize() {
	if (_initialized) {
		return;
	}
	_initialized = true;
	refreshName();
	refreshStatus();
}

void PeerListRow::createCheckbox(base::lambda<void()> updateCallback) {
	_checkbox = std::make_unique<Ui::RoundImageCheckbox>(st::contactsPhotoCheckbox, std::move(updateCallback), PaintUserpicCallback(_peer));
}

void PeerListRow::setCheckedInternal(bool checked, SetStyle style) {
	Expects(_checkbox != nullptr);
	using CheckboxStyle = Ui::RoundCheckbox::SetStyle;
	auto speed = (style == SetStyle::Animated) ? CheckboxStyle::Animated : CheckboxStyle::Fast;
	_checkbox->setChecked(checked, speed);
}

PeerListBox::Inner::Inner(QWidget *parent, not_null<PeerListController*> controller) : TWidget(parent)
, _controller(controller)
, _rowHeight(st::contactsPadding.top() + st::contactsPhotoSize + st::contactsPadding.bottom()) {
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
}

void PeerListBox::Inner::appendRow(std::unique_ptr<PeerListRow> row) {
	Expects(row != nullptr);
	if (_rowsById.find(row->id()) == _rowsById.cend()) {
		row->setAbsoluteIndex(_rows.size());
		addRowEntry(row.get());
		_rows.push_back(std::move(row));
	}
}

void PeerListBox::Inner::appendSearchRow(std::unique_ptr<PeerListRow> row) {
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

void PeerListBox::Inner::appendFoundRow(not_null<PeerListRow*> row) {
	Expects(showingSearch());
	auto index = findRowIndex(row);
	if (index.value < 0) {
		_filterResults.push_back(row);
	}
}

void PeerListBox::Inner::changeCheckState(not_null<PeerListRow*> row, bool checked, PeerListRow::SetStyle style) {
	row->setChecked(checked, style, [this, row] {
		updateRow(row);
	});
}

void PeerListBox::Inner::addRowEntry(not_null<PeerListRow*> row) {
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

void PeerListBox::Inner::invalidatePixmapsCache() {
	auto invalidate = [](auto &&row) { row->invalidatePixmapsCache(); };
	base::for_each(_rows, invalidate);
	base::for_each(_searchRows, invalidate);
}

bool PeerListBox::Inner::addingToSearchIndex() const {
	// If we started indexing already, we continue.
	return (_searchMode != PeerListSearchMode::Disabled) || !_searchIndex.empty();
}

void PeerListBox::Inner::addToSearchIndex(not_null<PeerListRow*> row) {
	if (row->isSearchResult()) {
		return;
	}

	removeFromSearchIndex(row);
	row->setNameFirstChars(row->peer()->chars);
	for_const (auto ch, row->nameFirstChars()) {
		_searchIndex[ch].push_back(row);
	}
}

void PeerListBox::Inner::removeFromSearchIndex(not_null<PeerListRow*> row) {
	auto &nameFirstChars = row->nameFirstChars();
	if (!nameFirstChars.empty()) {
		for_const (auto ch, row->nameFirstChars()) {
			auto it = _searchIndex.find(ch);
			if (it != _searchIndex.cend()) {
				auto &entry = it->second;
				entry.erase(std::remove(entry.begin(), entry.end(), row), entry.end());
				if (entry.empty()) {
					_searchIndex.erase(it);
				}
			}
		}
		row->setNameFirstChars(OrderedSet<QChar>());
	}
}

void PeerListBox::Inner::prependRow(std::unique_ptr<PeerListRow> row) {
	Expects(row != nullptr);
	if (_rowsById.find(row->id()) == _rowsById.cend()) {
		addRowEntry(row.get());
		_rows.insert(_rows.begin(), std::move(row));
		refreshIndices();
	}
}

void PeerListBox::Inner::prependRowFromSearchResult(not_null<PeerListRow*> row) {
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

void PeerListBox::Inner::refreshIndices() {
	auto index = 0;
	for (auto &row : _rows) {
		row->setAbsoluteIndex(index++);
	}
}

void PeerListBox::Inner::removeRowAtIndex(std::vector<std::unique_ptr<PeerListRow>> &from, int index) {
	from.erase(from.begin() + index);
	for (auto i = index, count = int(from.size()); i != count; ++i) {
		from[i]->setAbsoluteIndex(i);
	}
}

PeerListRow *PeerListBox::Inner::findRow(PeerListRowId id) {
	auto it = _rowsById.find(id);
	return (it == _rowsById.cend()) ? nullptr : it->second.get();
}

void PeerListBox::Inner::removeRow(not_null<PeerListRow*> row) {
	auto index = row->absoluteIndex();
	auto isSearchResult = row->isSearchResult();
	auto &eraseFrom = isSearchResult ? _searchRows : _rows;

	Assert(index >= 0 && index < eraseFrom.size());
	Assert(eraseFrom[index].get() == row);

	setSelected(Selected());
	setPressed(Selected());

	_rowsById.erase(row->id());
	auto &byPeer = _rowsByPeer[row->peer()];
	byPeer.erase(std::remove(byPeer.begin(), byPeer.end(), row), byPeer.end());
	removeFromSearchIndex(row);
	_filterResults.erase(std::find(_filterResults.begin(), _filterResults.end(), row), _filterResults.end());
	removeRowAtIndex(eraseFrom, index);

	restoreSelection();
}

void PeerListBox::Inner::convertRowToSearchResult(not_null<PeerListRow*> row) {
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

int PeerListBox::Inner::fullRowsCount() const {
	return _rows.size();
}

not_null<PeerListRow*> PeerListBox::Inner::rowAt(int index) const {
	Expects(index >= 0 && index < _rows.size());
	return _rows[index].get();
}

void PeerListBox::Inner::setDescription(object_ptr<Ui::FlatLabel> description) {
	_description = std::move(description);
	if (_description) {
		_description->setParent(this);
	}
}

void PeerListBox::Inner::setSearchLoading(object_ptr<Ui::FlatLabel> loading) {
	_searchLoading = std::move(loading);
	if (_searchLoading) {
		_searchLoading->setParent(this);
	}
}

void PeerListBox::Inner::setSearchNoResults(object_ptr<Ui::FlatLabel> noResults) {
	_searchNoResults = std::move(noResults);
	if (_searchNoResults) {
		_searchNoResults->setParent(this);
	}
}

void PeerListBox::Inner::setAboveWidget(object_ptr<TWidget> aboveWidget) {
	_aboveWidget = std::move(aboveWidget);
	if (_aboveWidget) {
		_aboveWidget->setParent(this);
	}
}

int PeerListBox::Inner::labelHeight() const {
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

void PeerListBox::Inner::refreshRows() {
	resizeToWidth(st::boxWideWidth);
	if (_visibleBottom > 0) {
		checkScrollForPreload();
	}
	updateSelection();
	update();
}

void PeerListBox::Inner::setSearchMode(PeerListSearchMode mode) {
	if (_searchMode != mode) {
		if (!addingToSearchIndex()) {
			for_const (auto &row, _rows) {
				addToSearchIndex(row.get());
			}
		}
		_searchMode = mode;
		if (_controller->hasComplexSearch()) {
			if (!_searchLoading) {
				setSearchLoading(object_ptr<Ui::FlatLabel>(this, lang(lng_contacts_loading), Ui::FlatLabel::InitType::Simple, st::membersAbout));
			}
		} else {
			clearSearchRows();
		}
	}
}

void PeerListBox::Inner::clearSearchRows() {
	while (!_searchRows.empty()) {
		removeRow(_searchRows.back().get());
	}
}

void PeerListBox::Inner::paintEvent(QPaintEvent *e) {
	QRect r(e->rect());
	Painter p(this);

	p.fillRect(r, st::contactsBg);

	auto rowsTopCached = rowsTop();
	auto ms = getms();
	auto yFrom = r.y() - rowsTopCached;
	auto yTo = r.y() + r.height() - rowsTopCached;
	p.translate(0, rowsTopCached);
	auto count = shownRowsCount();
	if (count > 0) {
		auto from = floorclamp(yFrom, _rowHeight, 0, count);
		auto to = ceilclamp(yTo, _rowHeight, 0, count);
		p.translate(0, from * _rowHeight);
		for (auto index = from; index != to; ++index) {
			paintRow(p, ms, RowIndex(index));
			p.translate(0, _rowHeight);
		}
	}
}

int PeerListBox::Inner::resizeGetHeight(int newWidth) {
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
	auto labelTop = rowsTop() + qMax(1, shownRowsCount()) * _rowHeight;
	if (_description) {
		_description->moveToLeft(st::contactsPadding.left(), labelTop + st::membersAboutLimitPadding.top(), newWidth);
		_description->setVisible(!showingSearch());
	}
	if (_searchNoResults) {
		_searchNoResults->moveToLeft(st::contactsPadding.left(), labelTop + st::membersAboutLimitPadding.top(), newWidth);
		_searchNoResults->setVisible(showingSearch() && _filterResults.empty() && !_controller->isSearchLoading());
	}
	if (_searchLoading) {
		_searchLoading->moveToLeft(st::contactsPadding.left(), labelTop + st::membersAboutLimitPadding.top(), newWidth);
		_searchLoading->setVisible(showingSearch() && _filterResults.empty() && _controller->isSearchLoading());
	}
	return labelTop + labelHeight() + st::membersMarginBottom;
}

void PeerListBox::Inner::enterEventHook(QEvent *e) {
	setMouseTracking(true);
}

void PeerListBox::Inner::leaveEventHook(QEvent *e) {
	_mouseSelection = false;
	setMouseTracking(false);
	setSelected(Selected());
}

void PeerListBox::Inner::mouseMoveEvent(QMouseEvent *e) {
	auto position = e->globalPos();
	if (_mouseSelection || _lastMousePosition != position) {
		_lastMousePosition = position;
		_mouseSelection = true;
		updateSelection();
	}
}

void PeerListBox::Inner::mousePressEvent(QMouseEvent *e) {
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
			row->addRipple(size, point, std::move(updateCallback));
		}
	}
}

void PeerListBox::Inner::mouseReleaseEvent(QMouseEvent *e) {
	updateRow(_pressed.index);
	updateRow(_selected.index);

	auto pressed = _pressed;
	setPressed(Selected());
	if (e->button() == Qt::LeftButton && pressed == _selected) {
		if (auto row = getRow(pressed.index)) {
			if (pressed.action) {
				_controller->rowActionClicked(row);
			} else {
				_controller->rowClicked(row);
			}
		}
	}
}

void PeerListBox::Inner::setPressed(Selected pressed) {
	if (auto row = getRow(_pressed.index)) {
		row->stopLastRipple();
		row->stopLastActionRipple();
	}
	_pressed = pressed;
}

void PeerListBox::Inner::paintRow(Painter &p, TimeMs ms, RowIndex index) {
	auto row = getRow(index);
	Assert(row != nullptr);
	row->lazyInitialize();

	auto peer = row->peer();
	auto user = peer->asUser();
	auto active = (_pressed.index.value >= 0) ? _pressed : _selected;
	auto selected = (active.index == index);
	auto actionSelected = (selected && active.action);

	p.fillRect(0, 0, width(), _rowHeight, selected ? st::contactsBgOver : st::contactsBg);
	row->paintRipple(p, ms, 0, 0, width());
	row->paintUserpic(p, ms, st::contactsPadding.left(), st::contactsPadding.top(), width());

	p.setPen(st::contactsNameFg);

	auto actionSize = row->actionSize();
	auto actionMargins = actionSize.isEmpty() ? QMargins() : row->actionMargins();
	auto &name = row->name();
	auto namex = st::contactsPadding.left() + st::contactsPhotoSize + st::contactsPadding.left();
	auto namew = width() - namex - st::contactsPadding.right();
	if (!actionSize.isEmpty()) {
		namew -= actionMargins.left() + actionSize.width() + actionMargins.right();
	}
	auto statusw = namew;
	if (row->needsVerifiedIcon()) {
		auto icon = &st::dialogsVerifiedIcon;
		namew -= icon->width();
		icon->paint(p, namex + qMin(name.maxWidth(), namew), st::contactsPadding.top() + st::contactsNameTop, width());
	}
	auto nameCheckedRatio = row->disabled() ? 0. : row->checkedRatio();
	p.setPen(anim::pen(st::contactsNameFg, st::contactsNameCheckedFg, nameCheckedRatio));
	name.drawLeftElided(p, namex, st::contactsPadding.top() + st::contactsNameTop, namew, width());

	if (!actionSize.isEmpty()) {
		auto actionLeft = width() - st::contactsPadding.right() - actionMargins.right() - actionSize.width();
		auto actionTop = actionMargins.top();
		row->paintAction(p, ms, actionLeft, actionTop, width(), actionSelected);
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
			p.setPen(st::contactsStatusFgOnline);
			p.drawTextLeft(namex, st::contactsPadding.top() + st::contactsStatusTop, width(), highlightedPart);
		} else {
			grayedPart = st::contactsStatusFont->elided(grayedPart, availableWidth - highlightedWidth);
			auto grayedWidth = st::contactsStatusFont->width(grayedPart);
			p.setPen(st::contactsStatusFgOnline);
			p.drawTextLeft(namex, st::contactsPadding.top() + st::contactsStatusTop, width(), highlightedPart);
			p.setPen(selected ? st::contactsStatusFgOver : st::contactsStatusFg);
			p.drawTextLeft(namex + highlightedWidth, st::contactsPadding.top() + st::contactsStatusTop, width(), grayedPart);
		}
	} else {
		row->paintStatusText(p, namex, st::contactsPadding.top() + st::contactsStatusTop, statusw, width(), selected);
	}
}

void PeerListBox::Inner::selectSkip(int direction) {
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
		emit mustScrollTo(top, bottom);
	}

	update();
}

void PeerListBox::Inner::selectSkipPage(int height, int direction) {
	auto rowsToSkip = height / _rowHeight;
	if (!rowsToSkip) return;
	selectSkip(rowsToSkip * direction);
}

void PeerListBox::Inner::loadProfilePhotos() {
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

void PeerListBox::Inner::checkScrollForPreload() {
	if (_visibleBottom + PreloadHeightsCount * (_visibleBottom - _visibleTop) > height()) {
		_controller->loadMoreRows();
	}
}

void PeerListBox::Inner::searchQueryChanged(QString query) {
	auto searchWordsList = TextUtilities::PrepareSearchWords(query);
	auto normalizedQuery = searchWordsList.isEmpty() ? QString() : searchWordsList.join(' ');
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
				auto searchWordInNames = [](PeerData *peer, const QString &searchWord) {
					for_const (auto &nameWord, peer->names) {
						if (nameWord.startsWith(searchWord)) {
							return true;
						}
					}
					return false;
				};
				auto allSearchWordsInNames = [searchWordInNames, &searchWordsList](PeerData *peer) {
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

void PeerListBox::Inner::setSearchQuery(const QString &query, const QString &normalizedQuery) {
	setSelected(Selected());
	setPressed(Selected());
	_searchQuery = query;
	_normalizedSearchQuery = normalizedQuery;
	_mentionHighlight = _searchQuery.startsWith('@') ? _searchQuery.mid(1) : _searchQuery;
	_filterResults.clear();
	clearSearchRows();
}

void PeerListBox::Inner::submitted() {
	if (auto row = getRow(_selected.index)) {
		_controller->rowClicked(row);
	}
}

void PeerListBox::Inner::setVisibleTopBottom(int visibleTop, int visibleBottom) {
	_visibleTop = visibleTop;
	_visibleBottom = visibleBottom;
	loadProfilePhotos();
	checkScrollForPreload();
}

void PeerListBox::Inner::setSelected(Selected selected) {
	updateRow(_selected.index);
	if (_selected != selected) {
		_selected = selected;
		updateRow(_selected.index);
		setCursor(_selected.action ? style::cur_pointer : style::cur_default);
	}
}

void PeerListBox::Inner::restoreSelection() {
	_lastMousePosition = QCursor::pos();
	updateSelection();
}

void PeerListBox::Inner::updateSelection() {
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

QRect PeerListBox::Inner::getActionRect(not_null<PeerListRow*> row, RowIndex index) const {
	auto actionSize = row->actionSize();
	if (actionSize.isEmpty()) {
		return QRect();
	}
	auto actionMargins = row->actionMargins();
	auto actionRight = st::contactsPadding.right() + actionMargins.right();
	auto actionTop = actionMargins.top();
	auto actionLeft = width() - actionRight - actionSize.width();
	auto rowTop = getRowTop(index);
	return myrtlrect(actionLeft, rowTop + actionTop, actionSize.width(), actionSize.height());
}

int PeerListBox::Inner::rowsTop() const {
	return _aboveHeight + st::membersMarginTop;
}

int PeerListBox::Inner::getRowTop(RowIndex index) const {
	if (index.value >= 0) {
		return rowsTop() + index.value * _rowHeight;
	}
	return -1;
}

void PeerListBox::Inner::updateRow(not_null<PeerListRow*> row, RowIndex hint) {
	updateRow(findRowIndex(row, hint));
}

void PeerListBox::Inner::updateRow(RowIndex index) {
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
	}
	update(0, getRowTop(index), width(), _rowHeight);
}

template <typename Callback>
bool PeerListBox::Inner::enumerateShownRows(Callback callback) {
	return enumerateShownRows(0, shownRowsCount(), std::move(callback));
}

template <typename Callback>
bool PeerListBox::Inner::enumerateShownRows(int from, int to, Callback callback) {
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

PeerListRow *PeerListBox::Inner::getRow(RowIndex index) {
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

PeerListBox::Inner::RowIndex PeerListBox::Inner::findRowIndex(not_null<PeerListRow*> row, RowIndex hint) {
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

void PeerListBox::Inner::handleNameChanged(const Notify::PeerUpdate &update) {
	auto byPeer = _rowsByPeer.find(update.peer);
	if (byPeer != _rowsByPeer.cend()) {
		for (auto row : byPeer->second) {
			if (addingToSearchIndex()) {
				addToSearchIndex(row);
			}
			row->refreshName();
			updateRow(row);
		}
	}
}
