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
#include "ui/widgets/scroll_area.h"
#include "ui/effects/ripple_animation.h"
#include "dialogs/dialogs_indexed_list.h"
#include "observer_peer.h"
#include "auth_session.h"
#include "mainwidget.h"
#include "storage/file_download.h"
#include "ui/widgets/multi_select.h"
#include "ui/widgets/labels.h"
#include "ui/effects/widget_slide_wrap.h"
#include "lang.h"
#include "ui/effects/round_checkbox.h"
#include "boxes/contacts_box.h"
#include "window/themes/window_theme.h"

PeerListBox::PeerListBox(QWidget*, std::unique_ptr<Controller> controller)
: _controller(std::move(controller)) {
}

object_ptr<Ui::WidgetSlideWrap<Ui::MultiSelect>> PeerListBox::createMultiSelect() {
	auto entity = object_ptr<Ui::MultiSelect>(this, st::contactsMultiSelect, lang(lng_participant_filter));
	auto margins = style::margins(0, 0, 0, 0);
	auto callback = [this] { updateScrollSkips(); };
	return object_ptr<Ui::WidgetSlideWrap<Ui::MultiSelect>>(this, std::move(entity), margins, std::move(callback));
}

int PeerListBox::getTopScrollSkip() const {
	auto result = 0;
	if (_select && !_select->isHidden()) {
		result += _select->height();
	}
	return result;
}

void PeerListBox::updateScrollSkips() {
	setInnerTopSkip(getTopScrollSkip(), true);
}

void PeerListBox::prepare() {
	_inner = setInnerWidget(object_ptr<Inner>(this, _controller.get()), st::boxLayerScroll);

	_controller->setView(this);

	setDimensions(st::boxWideWidth, st::boxMaxListHeight);
	if (_select) {
		_select->finishAnimation();
		onScrollToY(0);
	}

	connect(_inner, SIGNAL(mustScrollTo(int, int)), this, SLOT(onScrollToY(int, int)));
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

	_inner->resize(width(), _inner->height());
}

void PeerListBox::setInnerFocus() {
	if (!_select || _select->isHidden()) {
		_inner->setFocus();
	} else {
		_select->entity()->setInnerFocus();
	}
}

void PeerListBox::appendRow(std::unique_ptr<Row> row) {
	_inner->appendRow(std::move(row));
}

void PeerListBox::prependRow(std::unique_ptr<Row> row) {
	_inner->prependRow(std::move(row));
}

PeerListBox::Row *PeerListBox::findRow(PeerData *peer) {
	return _inner->findRow(peer);
}

void PeerListBox::updateRow(Row *row) {
	_inner->updateRow(row);
}

void PeerListBox::removeRow(Row *row) {
	_inner->removeRow(row);
}

void PeerListBox::setRowChecked(Row *row, bool checked) {
	auto peer = row->peer();
	if (checked) {
		addSelectItem(peer, Row::SetStyle::Animated);
		_inner->changeCheckState(row, checked, Row::SetStyle::Animated);
		updateRow(row);

		// This call deletes row from _globalSearchRows.
		_select->entity()->clearQuery();
	} else {
		// The itemRemovedCallback will call changeCheckState() here.
		_select->entity()->removeItem(peer->id);
		updateRow(row);
	}
}

int PeerListBox::fullRowsCount() const {
	return _inner->fullRowsCount();
}

void PeerListBox::setAboutText(const QString &aboutText) {
	if (aboutText.isEmpty()) {
		setAbout(nullptr);
	} else {
		setAbout(object_ptr<Ui::FlatLabel>(this, aboutText, Ui::FlatLabel::InitType::Simple, st::membersAbout));
	}
}

void PeerListBox::setAbout(object_ptr<Ui::FlatLabel> about) {
	_inner->setAbout(std::move(about));
}

void PeerListBox::refreshRows() {
	_inner->refreshRows();
}

void PeerListBox::setSearchMode(SearchMode mode) {
	_inner->setSearchMode(mode);
	if (mode != SearchMode::None && !_select) {
		_select = createMultiSelect();
		_select->entity()->setSubmittedCallback([this](bool chtrlShiftEnter) { _inner->submitted(); });
		_select->entity()->setQueryChangedCallback([this](const QString &query) { searchQueryChanged(query); });
		_select->entity()->setItemRemovedCallback([this](uint64 itemId) {
			if (auto peer = App::peerLoaded(itemId)) {
				if (auto row = findRow(peer)) {
					_inner->changeCheckState(row, false, Row::SetStyle::Animated);
					update();
				}
			}
		});
		_select->resizeToWidth(st::boxWideWidth);
		_select->moveToLeft(0, 0);
	}
	if (_select) {
		_select->toggleAnimated(mode != SearchMode::None);
	}
}

void PeerListBox::setSearchNoResultsText(const QString &searchNoResultsText) {
	if (searchNoResultsText.isEmpty()) {
		setSearchNoResults(nullptr);
	} else {
		setSearchNoResults(object_ptr<Ui::FlatLabel>(this, searchNoResultsText, Ui::FlatLabel::InitType::Simple, st::membersAbout));
	}
}

void PeerListBox::setSearchNoResults(object_ptr<Ui::FlatLabel> searchNoResults) {
	_inner->setSearchNoResults(std::move(searchNoResults));
}

void PeerListBox::setSearchLoadingText(const QString &searchLoadingText) {
	if (searchLoadingText.isEmpty()) {
		setSearchLoading(nullptr);
	} else {
		setSearchLoading(object_ptr<Ui::FlatLabel>(this, searchLoadingText, Ui::FlatLabel::InitType::Simple, st::membersAbout));
	}
}

void PeerListBox::setSearchLoading(object_ptr<Ui::FlatLabel> searchLoading) {
	_inner->setSearchLoading(std::move(searchLoading));
}

QVector<PeerData*> PeerListBox::collectSelectedRows() const {
	Expects(_select != nullptr);
	auto result = QVector<PeerData*>();
	auto items = _select->entity()->getItems();
	if (!items.empty()) {
		result.reserve(items.size());
		for_const (auto itemId, items) {
			result.push_back(App::peer(itemId));
		}
	}
	return result;
}

void PeerListBox::addSelectItem(PeerData *peer, Row::SetStyle style) {
	Expects(_select != nullptr);
	if (style == Row::SetStyle::Fast) {
		_select->entity()->addItemInBunch(peer->id, peer->shortName(), st::activeButtonBg, PaintUserpicCallback(peer));
	} else {
		_select->entity()->addItem(peer->id, peer->shortName(), st::activeButtonBg, PaintUserpicCallback(peer), Ui::MultiSelect::AddItemWay::Default);
	}
}

void PeerListBox::finishSelectItemsBunch() {
	Expects(_select != nullptr);
	_select->entity()->finishItemsBunch();
}

bool PeerListBox::isRowSelected(PeerData *peer) const {
	Expects(_select != nullptr);
	return _select->entity()->hasItem(peer->id);
}

PeerListBox::Row::Row(PeerData *peer) : _peer(peer) {
}

bool PeerListBox::Row::checked() const {
	return _checkbox && _checkbox->checked();
}

void PeerListBox::Row::setActionLink(const QString &action) {
	_action = action;
	refreshActionLink();
}

void PeerListBox::Row::refreshActionLink() {
	if (!_initialized) return;
	_actionWidth = _action.isEmpty() ? 0 : st::normalFont->width(_action);
}

void PeerListBox::Row::setCustomStatus(const QString &status) {
	_status = status;
	_statusType = StatusType::Custom;
}

void PeerListBox::Row::clearCustomStatus() {
	_statusType = StatusType::Online;
	refreshStatus();
}

void PeerListBox::Row::refreshStatus() {
	if (!_initialized || _statusType == StatusType::Custom) {
		return;
	}
	if (auto user = peer()->asUser()) {
		auto time = unixtime();
		_status = App::onlineText(user, time);
		_statusType = App::onlineColorUse(user, time) ? StatusType::Online : StatusType::LastSeen;
	}
}

PeerListBox::Row::StatusType PeerListBox::Row::statusType() const {
	return _statusType;
}

void PeerListBox::Row::refreshName() {
	if (!_initialized) {
		return;
	}
	_name.setText(st::contactsNameStyle, peer()->name, _textNameOptions);
}

QString PeerListBox::Row::status() const {
	return _status;
}

QString PeerListBox::Row::action() const {
	return _action;
}

int PeerListBox::Row::actionWidth() const {
	return _actionWidth;
}

PeerListBox::Row::~Row() = default;

void PeerListBox::Row::invalidatePixmapsCache() {
	if (_checkbox) {
		_checkbox->invalidateCache();
	}
}

template <typename UpdateCallback>
void PeerListBox::Row::addRipple(QSize size, QPoint point, UpdateCallback updateCallback) {
	if (!_ripple) {
		auto mask = Ui::RippleAnimation::rectMask(size);
		_ripple = std::make_unique<Ui::RippleAnimation>(st::contactsRipple, std::move(mask), std::move(updateCallback));
	}
	_ripple->add(point);
}

void PeerListBox::Row::stopLastRipple() {
	if (_ripple) {
		_ripple->lastStop();
	}
}

void PeerListBox::Row::paintRipple(Painter &p, TimeMs ms, int x, int y, int outerWidth) {
	if (_ripple) {
		_ripple->paint(p, x, y, outerWidth, ms);
		if (_ripple->empty()) {
			_ripple.reset();
		}
	}
}

void PeerListBox::Row::paintUserpic(Painter &p, TimeMs ms, int x, int y, int outerWidth) {
	if (_checkbox) {
		if (disabled() && checked()) {
			paintDisabledCheckUserpic(p, x, y, outerWidth);
		} else {
			_checkbox->paint(p, ms, x, y, outerWidth);
		}
	} else {
		peer()->paintUserpicLeft(p, x, y, outerWidth, st::contactsPhotoSize);
	}
}

// Emulates Ui::RoundImageCheckbox::paint() in a checked state.
void PeerListBox::Row::paintDisabledCheckUserpic(Painter &p, int x, int y, int outerWidth) const {
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

float64 PeerListBox::Row::checkedRatio() {
	return _checkbox ? _checkbox->checkedAnimationRatio() : 0.;
}

void PeerListBox::Row::lazyInitialize() {
	if (_initialized) {
		return;
	}
	_initialized = true;
	refreshActionLink();
	refreshName();
	refreshStatus();
}

void PeerListBox::Row::createCheckbox(base::lambda<void()> updateCallback) {
	_checkbox = std::make_unique<Ui::RoundImageCheckbox>(st::contactsPhotoCheckbox, std::move(updateCallback), PaintUserpicCallback(_peer));
}

void PeerListBox::Row::setCheckedInternal(bool checked, SetStyle style) {
	Expects(_checkbox != nullptr);
	using CheckboxStyle = Ui::RoundCheckbox::SetStyle;
	auto speed = (style == SetStyle::Animated) ? CheckboxStyle::Animated : CheckboxStyle::Fast;
	_checkbox->setChecked(checked, speed);
}

PeerListBox::Inner::Inner(QWidget *parent, Controller *controller) : TWidget(parent)
, _controller(controller)
, _rowHeight(st::contactsPadding.top() + st::contactsPhotoSize + st::contactsPadding.bottom()) {
	subscribe(AuthSession::CurrentDownloaderTaskFinished(), [this] { update(); });

	connect(App::main(), SIGNAL(peerNameChanged(PeerData*, const PeerData::Names&, const PeerData::NameFirstChars&)), this, SLOT(onPeerNameChanged(PeerData*, const PeerData::Names&, const PeerData::NameFirstChars&)));
	connect(App::main(), SIGNAL(peerPhotoChanged(PeerData*)), this, SLOT(peerUpdated(PeerData*)));

	subscribe(Window::Theme::Background(), [this](const Window::Theme::BackgroundUpdate &update) {
		if (update.paletteChanged()) {
			invalidatePixmapsCache();
		}
	});
}

void PeerListBox::Inner::appendRow(std::unique_ptr<Row> row) {
	if (_rowsByPeer.find(row->peer()) == _rowsByPeer.cend()) {
		row->setAbsoluteIndex(_rows.size());
		addRowEntry(row.get());
		_rows.push_back(std::move(row));
	}
}

void PeerListBox::Inner::appendGlobalSearchRow(std::unique_ptr<Row> row) {
	Expects(showingSearch());
	if (_rowsByPeer.find(row->peer()) == _rowsByPeer.cend()) {
		row->setAbsoluteIndex(_globalSearchRows.size());
		row->setIsGlobalSearchResult(true);
		addRowEntry(row.get());
		_filterResults.push_back(row.get());
		_globalSearchRows.push_back(std::move(row));
	}
}

void PeerListBox::Inner::changeCheckState(Row *row, bool checked, Row::SetStyle style) {
	row->setChecked(checked, style, [this, row] {
		updateRow(row);
	});
}

void PeerListBox::Inner::addRowEntry(Row *row) {
	_rowsByPeer.emplace(row->peer(), row);
	if (addingToSearchIndex()) {
		addToSearchIndex(row);
	}
	if (_searchMode != SearchMode::None) {
		if (_controller->view()->isRowSelected(row->peer())) {
			changeCheckState(row, true, Row::SetStyle::Fast);
		}
	}
}

void PeerListBox::Inner::invalidatePixmapsCache() {
	for_const (auto &row, _rows) {
		row->invalidatePixmapsCache();
	}
	for_const (auto &row, _globalSearchRows) {
		row->invalidatePixmapsCache();
	}
}

bool PeerListBox::Inner::addingToSearchIndex() const {
	// If we started indexing already, we continue.
	return (_searchMode != SearchMode::None) || !_searchIndex.empty();
}

void PeerListBox::Inner::addToSearchIndex(Row *row) {
	if (row->isGlobalSearchResult()) {
		return;
	}

	removeFromSearchIndex(row);
	row->setNameFirstChars(row->peer()->chars);
	for_const (auto ch, row->nameFirstChars()) {
		_searchIndex[ch].push_back(row);
	}
}

void PeerListBox::Inner::removeFromSearchIndex(Row *row) {
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

void PeerListBox::Inner::prependRow(std::unique_ptr<Row> row) {
	if (_rowsByPeer.find(row->peer()) == _rowsByPeer.cend()) {
		addRowEntry(row.get());
		_rows.insert(_rows.begin(), std::move(row));
		refreshIndices();
	}
}

void PeerListBox::Inner::refreshIndices() {
	auto index = 0;
	for (auto &row : _rows) {
		row->setAbsoluteIndex(index++);
	}
}

PeerListBox::Row *PeerListBox::Inner::findRow(PeerData *peer) {
	auto it = _rowsByPeer.find(peer);
	return (it == _rowsByPeer.cend()) ? nullptr : it->second;
}

void PeerListBox::Inner::removeRow(Row *row) {
	auto index = row->absoluteIndex();
	auto isGlobalSearchResult = row->isGlobalSearchResult();
	auto &eraseFrom = isGlobalSearchResult ? _globalSearchRows : _rows;

	t_assert(index >= 0 && index < eraseFrom.size());
	t_assert(eraseFrom[index].get() == row);

	setSelected(Selected());
	setPressed(Selected());

	_rowsByPeer.erase(row->peer());
	removeFromSearchIndex(row);
	_filterResults.erase(std::find(_filterResults.begin(), _filterResults.end(), row), _filterResults.end());
	eraseFrom.erase(eraseFrom.begin() + index);
	for (auto i = index, count = int(eraseFrom.size()); i != count; ++i) {
		eraseFrom[i]->setAbsoluteIndex(i);
	}

	restoreSelection();
}

int PeerListBox::Inner::fullRowsCount() const {
	return _rows.size();
}

void PeerListBox::Inner::setAbout(object_ptr<Ui::FlatLabel> about) {
	_about = std::move(about);
	if (_about) {
		_about->setParent(this);
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
		if (globalSearchLoading()) {
			return computeLabelHeight(_searchLoading);
		}
		return computeLabelHeight(_searchNoResults);
	}
	return computeLabelHeight(_about);
}

void PeerListBox::Inner::refreshRows() {
	auto labelTop = st::membersMarginTop + qMax(1, shownRowsCount()) * _rowHeight;
	resize(width(), labelTop + labelHeight() + st::membersMarginBottom);
	if (_about) {
		_about->moveToLeft(st::contactsPadding.left(), labelTop + st::membersAboutLimitPadding.top());
		_about->setVisible(!showingSearch());
	}
	if (_searchNoResults) {
		_searchNoResults->moveToLeft(st::contactsPadding.left(), labelTop + st::membersAboutLimitPadding.top());
		_searchNoResults->setVisible(showingSearch() && _filterResults.empty() && !globalSearchLoading());
	}
	if (_searchLoading) {
		_searchLoading->moveToLeft(st::contactsPadding.left(), labelTop + st::membersAboutLimitPadding.top());
		_searchLoading->setVisible(showingSearch() && _filterResults.empty() && globalSearchLoading());
	}
	if (_visibleBottom > 0) {
		checkScrollForPreload();
	}
	update();
}

void PeerListBox::Inner::setSearchMode(SearchMode mode) {
	if (_searchMode != mode) {
		if (!addingToSearchIndex()) {
			for_const (auto &row, _rows) {
				addToSearchIndex(row.get());
			}
		}
		_searchMode = mode;
		if (_searchMode == SearchMode::Global) {
			if (!_searchLoading) {
				setSearchLoading(object_ptr<Ui::FlatLabel>(this, lang(lng_contacts_loading), Ui::FlatLabel::InitType::Simple, st::membersAbout));
			}
		} else {
			clearGlobalSearchRows();
		}
	}
}

void PeerListBox::Inner::clearGlobalSearchRows() {
	while (!_globalSearchRows.empty()) {
		removeRow(_globalSearchRows.back().get());
	}
}

void PeerListBox::Inner::setSearchNoResults(object_ptr<Ui::FlatLabel> searchNoResults) {
	_searchNoResults = std::move(searchNoResults);
	if (_searchNoResults) {
		_searchNoResults->setParent(this);
	}
}

void PeerListBox::Inner::setSearchLoading(object_ptr<Ui::FlatLabel> searchLoading) {
	_searchLoading = std::move(searchLoading);
	if (_searchLoading) {
		_searchLoading->setParent(this);
	}
}

void PeerListBox::Inner::paintEvent(QPaintEvent *e) {
	QRect r(e->rect());
	Painter p(this);

	p.fillRect(r, st::contactsBg);

	auto ms = getms();
	auto yFrom = r.y() - st::membersMarginTop;
	auto yTo = r.y() + r.height() - st::membersMarginTop;
	p.translate(0, st::membersMarginTop);
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
	if (!_selected.action) {
		if (auto row = getRow(_selected.index)) {
			auto size = QSize(width(), _rowHeight);
			auto point = mapFromGlobal(QCursor::pos()) - QPoint(0, getRowTop(_selected.index));
			auto hint = _selected.index;
			row->addRipple(size, point, [this, row, hint] {
				updateRow(row, hint);
			});
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
	}
	_pressed = pressed;
}

void PeerListBox::Inner::paintRow(Painter &p, TimeMs ms, RowIndex index) {
	auto row = getRow(index);
	t_assert(row != nullptr);
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

	auto actionWidth = row->actionWidth();
	auto &name = row->name();
	auto namex = st::contactsPadding.left() + st::contactsPhotoSize + st::contactsPadding.left();
	auto namew = width() - namex - st::contactsPadding.right() - (actionWidth ? (actionWidth + st::contactsCheckPosition.x() * 2) : 0);
	if (peer->isVerified()) {
		auto icon = &st::dialogsVerifiedIcon;
		namew -= icon->width();
		icon->paint(p, namex + qMin(name.maxWidth(), namew), st::contactsPadding.top() + st::contactsNameTop, width());
	}
	p.setPen(anim::pen(st::contactsNameFg, st::contactsNameCheckedFg, row->checkedRatio()));
	name.drawLeftElided(p, namex, st::contactsPadding.top() + st::contactsNameTop, namew, width());

	if (actionWidth) {
		p.setFont(actionSelected ? st::linkOverFont : st::linkFont);
		p.setPen(actionSelected ? st::defaultLinkButton.overColor : st::defaultLinkButton.color);
		auto actionRight = st::contactsPadding.right() + st::contactsCheckPosition.x();
		auto actionTop = (_rowHeight - st::normalFont->height) / 2;
		p.drawTextRight(actionRight, actionTop, width(), row->action(), actionWidth);
	}

	p.setFont(st::contactsStatusFont);
	if (row->isGlobalSearchResult() && !peer->userName().isEmpty()) {
		auto username = peer->userName();
		if (!_globalSearchHighlight.isEmpty() && username.startsWith(_globalSearchHighlight, Qt::CaseInsensitive)) {
			auto availableWidth = width() - namex - st::contactsPadding.right();
			auto highlightedPart = '@' + username.mid(0, _globalSearchHighlight.size());
			auto grayedPart = username.mid(_globalSearchHighlight.size());
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
			p.setPen(st::contactsStatusFgOnline);
			p.drawTextLeft(namex, st::contactsPadding.top() + st::contactsStatusTop, width(), '@' + username);
		}
	} else {
		auto statusHasOnlineColor = (row->statusType() == Row::StatusType::Online);
		p.setPen(statusHasOnlineColor ? st::contactsStatusFgOnline : (selected ? st::contactsStatusFgOver : st::contactsStatusFg));
		p.drawTextLeft(namex, st::contactsPadding.top() + st::contactsStatusTop, width(), row->status());
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
	enumerateShownRows([&firstEnabled, &lastEnabled, &index](Row *row) {
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

	t_assert(lastEnabled < rowsCount);
	t_assert(firstEnabled - 1 <= lastEnabled);

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
			t_assert(newSelectedIndex >= 0 && newSelectedIndex < rowsCount);
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
	AuthSession::Current().downloader().clearPriorities();

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
		_controller->preloadRows();
	}
}

void PeerListBox::Inner::searchQueryChanged(QString query) {
	auto searchWordsList = query.isEmpty() ? QStringList() : query.split(cWordSplit(), QString::SkipEmptyParts);
	if (!searchWordsList.isEmpty()) {
		query = searchWordsList.join(' ');
	}
	if (_searchQuery != query) {
		setSelected(Selected());
		setPressed(Selected());

		_searchQuery = query;
		_filterResults.clear();
		clearGlobalSearchRows();
		if (!searchWordsList.isEmpty()) {
			auto minimalList = (const std::vector<Row*>*)nullptr;
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
		if (_searchMode == SearchMode::Global) {
			needGlobalSearch();
		}
		refreshRows();
		restoreSelection();
	}
}

void PeerListBox::Inner::needGlobalSearch() {
	if (!globalSearchInCache()) {
		if (!_globalSearchTimer) {
			_globalSearchTimer = object_ptr<SingleTimer>(this);
			_globalSearchTimer->setTimeoutHandler([this] { globalSearchOnServer(); });
		}
		_globalSearchTimer->start(AutoSearchTimeout);
	}
}

bool PeerListBox::Inner::globalSearchInCache() {
	auto it = _globalSearchCache.find(_searchQuery);
	if (it != _globalSearchCache.cend()) {
		_globalSearchQuery = _searchQuery;
		_globalSearchRequestId = 0;
		globalSearchDone(it->second, _globalSearchRequestId);
		return true;
	}
	return false;
}

void PeerListBox::Inner::globalSearchOnServer() {
	_globalSearchQuery = _searchQuery;
	_globalSearchRequestId = MTP::send(MTPcontacts_Search(MTP_string(_globalSearchQuery), MTP_int(SearchPeopleLimit)), ::rpcDone(base::lambda_guarded(this, [this](const MTPcontacts_Found &result, mtpRequestId requestId) {
		globalSearchDone(result, requestId);
	})), ::rpcFail(base::lambda_guarded(this, [this](const RPCError &error, mtpRequestId requestId) {
		return globalSearchFail(error, requestId);
	})));
	_globalSearchQueries.emplace(_globalSearchRequestId, _globalSearchQuery);
}

void PeerListBox::Inner::globalSearchDone(const MTPcontacts_Found &result, mtpRequestId requestId) {
	auto query = _globalSearchQuery;
	auto it = _globalSearchQueries.find(requestId);
	if (it != _globalSearchQueries.cend()) {
		query = it->second;
		_globalSearchCache[query] = result;
		_globalSearchQueries.erase(it);
	}

	if (_globalSearchRequestId == requestId) {
		_globalSearchRequestId = 0;
		if (result.type() == mtpc_contacts_found) {
			auto &contacts = result.c_contacts_found();
			App::feedUsers(contacts.vusers);
			App::feedChats(contacts.vchats);

			_globalSearchHighlight = query;
			if (!_globalSearchHighlight.isEmpty() && _globalSearchHighlight[0] == '@') {
				_globalSearchHighlight = _globalSearchHighlight.mid(1);
			}

			for_const (auto &mtpPeer, contacts.vresults.v) {
				if (auto peer = App::peerLoaded(peerFromMTP(mtpPeer))) {
					if (findRow(peer)) {
						continue;
					}
					if (auto row = _controller->createGlobalRow(peer)) {
						appendGlobalSearchRow(std::move(row));
					}
				}
			}
		}
		refreshRows();
		updateSelection();
	}
}

bool PeerListBox::Inner::globalSearchFail(const RPCError &error, mtpRequestId requestId) {
	if (MTP::isDefaultHandledError(error)) return false;

	if (_globalSearchRequestId == requestId) {
		_globalSearchRequestId = 0;
		refreshRows();
	}
	return true;
}

bool PeerListBox::Inner::globalSearchLoading() const {
	return (_globalSearchTimer && _globalSearchTimer->isActive()) || _globalSearchRequestId;
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

	auto rowsTop = st::membersMarginTop;
	auto point = mapFromGlobal(_lastMousePosition);
	auto in = parentWidget()->rect().contains(parentWidget()->mapFromGlobal(_lastMousePosition));
	auto selected = Selected();
	auto rowsPointY = point.y() - rowsTop;
	selected.index.value = (in && rowsPointY >= 0 && rowsPointY < shownRowsCount() * _rowHeight) ? (rowsPointY / _rowHeight) : -1;
	if (selected.index.value >= 0) {
		auto row = getRow(selected.index);
		if (row->disabled()) {
			selected = Selected();
		} else {
			auto actionRight = st::contactsPadding.right() + st::contactsCheckPosition.x();
			auto actionTop = (_rowHeight - st::normalFont->height) / 2;
			auto actionWidth = row->actionWidth();
			auto actionLeft = width() - actionWidth - actionRight;
			auto rowTop = getRowTop(selected.index);
			auto actionRect = myrtlrect(actionLeft, rowTop + actionTop, actionWidth, st::normalFont->height);
			if (actionRect.contains(point)) {
				selected.action = true;
			}
		}
	}
	setSelected(selected);
}

void PeerListBox::Inner::peerUpdated(PeerData *peer) {
	update();
}

int PeerListBox::Inner::getRowTop(RowIndex index) const {
	if (index.value >= 0) {
		return st::membersMarginTop + index.value * _rowHeight;
	}
	return -1;
}

void PeerListBox::Inner::updateRow(Row *row, RowIndex hint) {
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
	t_assert(0 <= from);
	t_assert(from <= to);
	if (showingSearch()) {
		t_assert(to <= _filterResults.size());
		for (auto i = from; i != to; ++i) {
			if (!callback(_filterResults[i])) {
				return false;
			}
		}
	} else {
		t_assert(to <= _rows.size());
		for (auto i = from; i != to; ++i) {
			if (!callback(_rows[i].get())) {
				return false;
			}
		}
	}
	return true;
}

PeerListBox::Row *PeerListBox::Inner::getRow(RowIndex index) {
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

PeerListBox::Inner::RowIndex PeerListBox::Inner::findRowIndex(Row *row, RowIndex hint) {
	if (!showingSearch()) {
		t_assert(!row->isGlobalSearchResult());
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

void PeerListBox::Inner::onPeerNameChanged(PeerData *peer, const PeerData::Names &oldNames, const PeerData::NameFirstChars &oldChars) {
	if (auto row = findRow(peer)) {
		if (addingToSearchIndex()) {
			addToSearchIndex(row);
		}
		row->refreshName();
		updateRow(row);
	}
}

void ChatsListBoxController::prepare() {
	view()->setSearchNoResultsText(lang(lng_blocked_list_not_found));
	view()->setSearchMode(PeerListBox::SearchMode::Global);

	prepareViewHook();

	rebuildRows();

	auto &sessionData = AuthSession::Current().data();
	subscribe(sessionData.contactsLoaded(), [this](bool loaded) {
		rebuildRows();
	});
	subscribe(sessionData.moreChatsLoaded(), [this] {
		rebuildRows();
	});
	subscribe(sessionData.allChatsLoaded(), [this](bool loaded) {
		checkForEmptyRows();
	});
}

void ChatsListBoxController::rebuildRows() {
	auto ms = getms();
	auto wasEmpty = !view()->fullRowsCount();
	auto appendList = [this](auto chats) {
		auto count = 0;
		for_const (auto row, chats->all()) {
			auto history = row->history();
			if (history->peer->isUser()) {
				if (appendRow(history)) {
					++count;
				}
			}
		}
		return count;
	};
	auto added = appendList(App::main()->dialogsList());
	added += appendList(App::main()->contactsNoDialogsList());
	if (!wasEmpty && added > 0) {
		view()->reorderRows([](auto &&begin, auto &&end) {
			// Place dialogs list before contactsNoDialogs list.
			std::stable_partition(begin, end, [](auto &row) {
				auto history = static_cast<Row&>(*row).history();
				return history->inChatList(Dialogs::Mode::All);
			});
		});
	}
	checkForEmptyRows();
	view()->refreshRows();
}

void ChatsListBoxController::checkForEmptyRows() {
	if (view()->fullRowsCount()) {
		view()->setAboutText(QString());
	} else {
		auto &sessionData = AuthSession::Current().data();
		auto loaded = sessionData.contactsLoaded().value() && sessionData.allChatsLoaded().value();
		view()->setAboutText(lang(loaded ? lng_contacts_not_found : lng_contacts_loading));
	}
}

std::unique_ptr<PeerListBox::Row> ChatsListBoxController::createGlobalRow(PeerData *peer) {
	return createRow(App::history(peer));
}

bool ChatsListBoxController::appendRow(History *history) {
	if (auto row = view()->findRow(history->peer)) {
		updateRowHook(static_cast<Row*>(row));
		return false;
	}
	if (auto row = createRow(history)) {
		view()->appendRow(std::move(row));
		return true;
	}
	return false;
}
