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
#include "observer_peer.h"
#include "auth_session.h"
#include "mainwidget.h"
#include "storage/file_download.h"
#include "ui/widgets/multi_select.h"
#include "ui/effects/widget_slide_wrap.h"
#include "lang.h"

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
	} else {
		BoxContent::keyPressEvent(e);
	}
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

int PeerListBox::rowsCount() const {
	return _inner->rowsCount();
}

void PeerListBox::setAboutText(const QString &aboutText) {
	_inner->setAboutText(aboutText);
}

void PeerListBox::refreshRows() {
	_inner->refreshRows();
}

void PeerListBox::setSearchable(bool searchable) {
	_inner->setSearchable(searchable);
	if (searchable) {
		if (!_select) {
			_select = createMultiSelect();
			_select->resizeToWidth(width());
			_select->moveToLeft(0, 0);
		}
		_select->slideDown();
	} else if (_select) {
		_select->slideUp();
	}
}

PeerListBox::Row::Row(PeerData *peer) : _peer(peer) {
}

void PeerListBox::Row::setDisabled(bool disabled) {
	_disabled = disabled;
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

void PeerListBox::Row::paintRipple(Painter &p, int x, int y, int outerWidth, TimeMs ms) {
	if (_ripple) {
		_ripple->paint(p, x, y, outerWidth, ms);
		if (_ripple->empty()) {
			_ripple.reset();
		}
	}
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

PeerListBox::Inner::Inner(QWidget *parent, Controller *controller) : TWidget(parent)
, _controller(controller)
, _rowHeight(st::contactsPadding.top() + st::contactsPhotoSize + st::contactsPadding.bottom())
, _aboutWidth(st::boxWideWidth - st::contactsPadding.left() - st::contactsPadding.right())
, _about(_aboutWidth) {
	subscribe(AuthSession::CurrentDownloaderTaskFinished(), [this] { update(); });

	connect(App::main(), SIGNAL(peerNameChanged(PeerData*, const PeerData::Names&, const PeerData::NameFirstChars&)), this, SLOT(onPeerNameChanged(PeerData*, const PeerData::Names&, const PeerData::NameFirstChars&)));
	connect(App::main(), SIGNAL(peerPhotoChanged(PeerData*)), this, SLOT(peerUpdated(PeerData*)));
}

void PeerListBox::Inner::appendRow(std::unique_ptr<Row> row) {
	if (_rowsByPeer.find(row->peer()) == _rowsByPeer.cend()) {
		row->setIndex(_rows.size());
		addRowEntry(row.get());
		_rows.push_back(std::move(row));
	}
}

void PeerListBox::Inner::addRowEntry(Row *row) {
	_rowsByPeer.emplace(row->peer(), row);
	if (_searchable) {
		addToSearchIndex(row);
	}
}

void PeerListBox::Inner::addToSearchIndex(Row *row) {
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
		_rowsByPeer.emplace(row->peer(), row.get());
		_rows.insert(_rows.begin(), std::move(row));
		refreshIndices();
	}
}

void PeerListBox::Inner::refreshIndices() {
	auto index = 0;
	for (auto &row : _rows) {
		row->setIndex(index++);
	}
}

PeerListBox::Row *PeerListBox::Inner::findRow(PeerData *peer) {
	auto it = _rowsByPeer.find(peer);
	return (it == _rowsByPeer.cend()) ? nullptr : it->second;
}

void PeerListBox::Inner::updateRow(Row *row) {
	auto index = row->index();
	if (row->disabled()) {
		if (index == _selected.index) {
			_selected = SelectedRow();
		}
		if (index == _pressed.index) {
			setPressed(SelectedRow());
		}
	}
	updateRowWithIndex(index);
}

void PeerListBox::Inner::removeRow(Row *row) {
	auto index = row->index();
	t_assert(index >= 0 && index < _rows.size());
	t_assert(_rows[index].get() == row);

	clearSelection();

	_rowsByPeer.erase(row->peer());
	if (_searchable) {
		removeFromSearchIndex(row);
	}
	_rows.erase(_rows.begin() + index);
	for (auto i = index, count = int(_rows.size()); i != count; ++i) {
		_rows[i]->setIndex(i);
	}
}

int PeerListBox::Inner::rowsCount() const {
	return _rows.size();
}

void PeerListBox::Inner::setAboutText(const QString &aboutText) {
	if (_about.isEmpty() && aboutText.isEmpty()) {
		return;
	}
	_about.setText(st::boxLabelStyle, aboutText);
}

void PeerListBox::Inner::refreshRows() {
	if (!_about.isEmpty()) {
		_aboutHeight = st::membersAboutLimitPadding.top() + _about.countHeight(_aboutWidth) + st::membersAboutLimitPadding.bottom();
	} else {
		_aboutHeight = 0;
	}
	if (_rows.empty()) {
		resize(width(), st::membersMarginTop + _rowHeight + _aboutHeight + st::membersMarginBottom);
	} else {
		resize(width(), st::membersMarginTop + _rows.size() * _rowHeight + _aboutHeight + st::membersMarginBottom);
	}
	if (_visibleBottom > 0) {
		checkScrollForPreload();
	}
	update();
}

void PeerListBox::Inner::setSearchable(bool searchable) {
	// We don't destroy a search index if we have one already.
	if (searchable && !_searchable) {
		_searchable = true;
		for_const (auto &row, _rows) {
			addToSearchIndex(row.get());
		}
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
	if (_rows.empty()) {
		if (!_about.isEmpty()) {
			p.setPen(st::membersAboutLimitFg);
			_about.draw(p, st::contactsPadding.left(), _rowHeight + st::membersAboutLimitPadding.top(), _aboutWidth, style::al_center);
		}
	} else {
		auto from = floorclamp(yFrom, _rowHeight, 0, _rows.size());
		auto to = ceilclamp(yTo, _rowHeight, 0, _rows.size());
		p.translate(0, from * _rowHeight);
		for (auto index = from; index != to; ++index) {
			paintRow(p, ms, index);
			p.translate(0, _rowHeight);
		}
		if (!_about.isEmpty() && to == _rows.size()) {
			p.setPen(st::membersAboutLimitFg);
			_about.draw(p, st::contactsPadding.left(), st::membersAboutLimitPadding.top(), _aboutWidth, style::al_center);
		}
	}
}

void PeerListBox::Inner::enterEventHook(QEvent *e) {
	setMouseTracking(true);
}

void PeerListBox::Inner::leaveEventHook(QEvent *e) {
	_mouseSelection = false;
	setMouseTracking(false);
	if (_selected.index >= 0) {
		clearSelection();
	}
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
	if (_selected.index >= 0 && _selected.index < _rows.size() && !_selected.action) {
		auto size = QSize(width(), _rowHeight);
		auto point = mapFromGlobal(QCursor::pos()) - QPoint(0, getRowTop(_selected.index));
		auto row = _rows[_selected.index].get();
		row->addRipple(size, point, [this, row] {
			updateRow(row);
		});
	}
}

void PeerListBox::Inner::mouseReleaseEvent(QMouseEvent *e) {
	updateRowWithIndex(_pressed.index);
	updateRowWithIndex(_selected.index);

	auto pressed = _pressed;
	setPressed(SelectedRow());
	if (e->button() == Qt::LeftButton) {
		if (pressed == _selected && pressed.index >= 0) {
			if (pressed.action) {
				_controller->rowActionClicked(_rows[pressed.index]->peer());
			} else {
				_controller->rowClicked(_rows[pressed.index]->peer());
			}
		}
	}
}

void PeerListBox::Inner::setPressed(SelectedRow pressed) {
	if (_pressed.index >= 0 && _pressed.index < _rows.size()) {
		_rows[_pressed.index]->stopLastRipple();
	}
	_pressed = pressed;
}

void PeerListBox::Inner::paintRow(Painter &p, TimeMs ms, int index) {
	t_assert(index >= 0 && index < _rows.size());
	auto row = _rows[index].get();
	row->lazyInitialize();

	auto peer = row->peer();
	auto user = peer->asUser();
	auto active = (_pressed.index >= 0) ? _pressed : _selected;
	auto selected = (active.index == index);
	auto actionSelected = (selected && active.action);

	p.fillRect(0, 0, width(), _rowHeight, selected ? st::contactsBgOver : st::contactsBg);
	row->paintRipple(p, 0, 0, width(), ms);
	peer->paintUserpicLeft(p, st::contactsPadding.left(), st::contactsPadding.top(), width(), st::contactsPhotoSize);

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
	name.drawLeftElided(p, namex, st::contactsPadding.top() + st::contactsNameTop, namew, width());

	if (actionWidth) {
		p.setFont(actionSelected ? st::linkOverFont : st::linkFont);
		p.setPen(actionSelected ? st::defaultLinkButton.overColor : st::defaultLinkButton.color);
		auto actionRight = st::contactsPadding.right() + st::contactsCheckPosition.x();
		auto actionTop = (_rowHeight - st::normalFont->height) / 2;
		p.drawTextRight(actionRight, actionTop, width(), row->action(), actionWidth);
	}

	auto statusHasOnlineColor = (row->statusType() == Row::StatusType::Online);
	p.setFont(st::contactsStatusFont);
	p.setPen(statusHasOnlineColor ? st::contactsStatusFgOnline : (selected ? st::contactsStatusFgOver : st::contactsStatusFg));
	p.drawTextLeft(namex, st::contactsPadding.top() + st::contactsStatusTop, width(), row->status());
}

void PeerListBox::Inner::selectSkip(int direction) {
	if (_pressed.index >= 0) {
		return;
	}
	_mouseSelection = false;

	auto newSelectedIndex = _selected.index + direction;

	auto firstEnabled = 0;
	for_const (auto &row, _rows) {
		if (!row->disabled()) {
			break;
		}
		++firstEnabled;
	}
	auto lastEnabled = int(_rows.size()) - 1;
	for (; lastEnabled > firstEnabled; --lastEnabled) {
		if (!_rows[lastEnabled]->disabled()) {
			break;
		}
	}

	t_assert(lastEnabled < _rows.size());
	t_assert(firstEnabled - 1 <= lastEnabled);

	// Always pass through the first enabled item when changing from / to none selected.
	if ((_selected.index > firstEnabled && newSelectedIndex < firstEnabled)
		|| (_selected.index < firstEnabled && newSelectedIndex > firstEnabled)) {
		newSelectedIndex = firstEnabled;
	}

	// Snap the index.
	newSelectedIndex = snap(newSelectedIndex, firstEnabled - 1, lastEnabled);

	// Skip the disabled rows.
	if (newSelectedIndex < firstEnabled) {
		newSelectedIndex = -1;
	} else if (newSelectedIndex > lastEnabled) {
		newSelectedIndex = lastEnabled;
	} else if (_rows[newSelectedIndex]->disabled()) {
		auto delta = (direction > 0) ? 1 : -1;
		for (newSelectedIndex += delta; ; newSelectedIndex += delta) {
			// We must find an enabled row, firstEnabled <= us <= lastEnabled.
			t_assert(newSelectedIndex >= 0 && newSelectedIndex < _rows.size());
			if (!_rows[newSelectedIndex]->disabled()) {
				break;
			}
		}
	}

	_selected.index = newSelectedIndex;
	_selected.action = false;
	if (newSelectedIndex >= 0) {
		auto top = (newSelectedIndex > 0) ? getRowTop(newSelectedIndex) : 0;
		auto bottom = (newSelectedIndex + 1 < _rows.size()) ? getRowTop(newSelectedIndex + 1) : height();
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
	AuthSession::Current().downloader()->clearPriorities();

	if (yTo < 0) return;
	if (yFrom < 0) yFrom = 0;

	if (!_rows.empty()) {
		auto from = yFrom / _rowHeight;
		if (from < 0) from = 0;
		if (from < _rows.size()) {
			auto to = (yTo / _rowHeight) + 1;
			if (to > _rows.size()) to = _rows.size();

			for (auto index = from; index != to; ++index) {
				_rows[index]->peer()->loadUserpic();
			}
		}
	}
}

void PeerListBox::Inner::checkScrollForPreload() {
	if (_visibleBottom + PreloadHeightsCount * (_visibleBottom - _visibleTop) > height()) {
		_controller->preloadRows();
	}
}

void PeerListBox::Inner::setVisibleTopBottom(int visibleTop, int visibleBottom) {
	_visibleTop = visibleTop;
	_visibleBottom = visibleBottom;
	loadProfilePhotos();
	checkScrollForPreload();
}

void PeerListBox::Inner::clearSelection() {
	updateRowWithIndex(_selected.index);
	_selected = SelectedRow();
	_lastMousePosition = QCursor::pos();
	updateSelection();
}

void PeerListBox::Inner::updateSelection() {
	if (!_mouseSelection) return;

	auto point = mapFromGlobal(_lastMousePosition);
	point.setY(point.y() - st::membersMarginTop);
	auto in = parentWidget()->rect().contains(parentWidget()->mapFromGlobal(_lastMousePosition));
	auto selected = SelectedRow();
	selected.index = (in && point.y() >= 0 && point.y() < _rows.size() * _rowHeight) ? (point.y() / _rowHeight) : -1;
	if (selected.index >= 0) {
		auto &row = _rows[selected.index];
		if (row->disabled()) {
			selected = SelectedRow();
		} else {
			auto actionRight = st::contactsPadding.right() + st::contactsCheckPosition.x();
			auto actionTop = (_rowHeight - st::normalFont->height) / 2;
			auto actionWidth = _rows[selected.index]->actionWidth();
			auto actionLeft = width() - actionWidth - actionRight;
			auto rowTop = selected.index * _rowHeight;
			auto actionRect = myrtlrect(actionLeft, rowTop + actionTop, actionWidth, st::normalFont->height);
			if (actionRect.contains(point)) {
				selected.action = true;
			}
		}
	}
	if (_selected != selected) {
		updateRowWithIndex(_selected.index);
		_selected = selected;
		updateRowWithIndex(_selected.index);
		setCursor(_selected.action ? style::cur_pointer : style::cur_default);
	}
}

void PeerListBox::Inner::peerUpdated(PeerData *peer) {
	update();
}

int PeerListBox::Inner::getRowTop(int index) const {
	if (index >= 0) {
		return st::membersMarginTop + index * _rowHeight;
	}
	return -1;
}

void PeerListBox::Inner::updateRowWithIndex(int index) {
	if (index >= 0) {
		update(0, getRowTop(index), width(), _rowHeight);
	}
}

void PeerListBox::Inner::onPeerNameChanged(PeerData *peer, const PeerData::Names &oldNames, const PeerData::NameFirstChars &oldChars) {
	if (auto row = findRow(peer)) {
		if (_searchable) {
			addToSearchIndex(row);
		}
		row->refreshName();
		update(0, st::membersMarginTop + row->index() * _rowHeight, width(), _rowHeight);
	}
}
