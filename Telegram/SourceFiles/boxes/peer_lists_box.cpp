/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peer_lists_box.h"

#include "lang/lang_keys.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/widgets/multi_select.h"
#include "ui/widgets/scroll_area.h"
#include "main/main_session.h"
#include "data/data_session.h"
#include "data/data_peer.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"

PeerListsBox::PeerListsBox(
	QWidget*,
	std::vector<std::unique_ptr<PeerListController>> controllers,
	Fn<void(not_null<PeerListsBox*>)> init)
: _lists(makeLists(std::move(controllers)))
, _init(std::move(init)) {
	Expects(!_lists.empty());
}

auto PeerListsBox::collectSelectedRows()
-> std::vector<not_null<PeerData*>> {
	auto result = std::vector<not_null<PeerData*>>();
	auto items = _select
		? _select->entity()->getItems()
		: QVector<uint64>();
	if (!items.empty()) {
		result.reserve(items.size());
		const auto session = &firstController()->session();
		for (const auto itemId : items) {
			const auto foreign = [&] {
				for (const auto &list : _lists) {
					if (list.controller->isForeignRow(itemId)) {
						return true;
					}
				}
				return false;
			}();
			if (!foreign) {
				result.push_back(session->data().peer(itemId));
			}
		}
	}
	return result;
}


PeerListsBox::List PeerListsBox::makeList(
		std::unique_ptr<PeerListController> controller) {
	auto delegate = std::make_unique<Delegate>(this, controller.get());
	return {
		std::move(controller),
		std::move(delegate),
	};
}

std::vector<PeerListsBox::List> PeerListsBox::makeLists(
		std::vector<std::unique_ptr<PeerListController>> controllers) {
	auto result = std::vector<List>();
	result.reserve(controllers.size());
	for (auto &controller : controllers) {
		result.push_back(makeList(std::move(controller)));
	}
	return result;
}

not_null<PeerListController*> PeerListsBox::firstController() const {
	return _lists.front().controller.get();
}

void PeerListsBox::createMultiSelect() {
	Expects(_select == nullptr);

	auto entity = object_ptr<Ui::MultiSelect>(
		this,
		(firstController()->selectSt()
			? *firstController()->selectSt()
			: st::defaultMultiSelect),
		tr::lng_participant_filter());
	_select.create(this, std::move(entity));
	_select->heightValue(
	) | rpl::start_with_next(
		[this] { updateScrollSkips(); },
		lifetime());
	_select->entity()->setSubmittedCallback([=](Qt::KeyboardModifiers) {
		for (const auto &list : _lists) {
			if (list.content->submitted()) {
				break;
			}
		}
	});
	_select->entity()->setQueryChangedCallback([=](const QString &query) {
		searchQueryChanged(query);
	});
	_select->entity()->setItemRemovedCallback([=](uint64 itemId) {
		for (const auto &list : _lists) {
			if (list.controller->handleDeselectForeignRow(itemId)) {
				return;
			}
		}
		const auto session = &firstController()->session();
		if (const auto peer = session->data().peerLoaded(itemId)) {
			const auto id = peer->id;
			for (const auto &list : _lists) {
				if (const auto row = list.delegate->peerListFindRow(id)) {
					list.content->changeCheckState(
						row,
						false,
						anim::type::normal);
					update();
				}
				list.controller->itemDeselectedHook(peer);
			}
		}
	});
	_select->resizeToWidth(firstController()->contentWidth());
	_select->moveToLeft(0, 0);
}

int PeerListsBox::getTopScrollSkip() const {
	auto result = 0;
	if (_select && !_select->isHidden()) {
		result += _select->height();
	}
	return result;
}

void PeerListsBox::updateScrollSkips() {
	// If we show / hide the search field scroll top is fixed.
	// If we resize search field by bubbles scroll bottom is fixed.
	setInnerTopSkip(getTopScrollSkip(), _scrollBottomFixed);
	if (!_select->animating()) {
		_scrollBottomFixed = true;
	}
}

void PeerListsBox::prepare() {
	auto rows = setInnerWidget(
		object_ptr<Ui::VerticalLayout>(this),
		st::boxScroll);
	for (auto &list : _lists) {
		const auto content = rows->add(object_ptr<PeerListContent>(
			rows,
			list.controller.get()));
		list.content = content;
		list.delegate->setContent(content);
		list.controller->setDelegate(list.delegate.get());

		content->scrollToRequests(
		) | rpl::start_with_next([=](Ui::ScrollToRequest request) {
			const auto skip = content->y();
			onScrollToY(
				skip + request.ymin,
				(request.ymax >= 0) ? (skip + request.ymax) : request.ymax);
		}, lifetime());

		content->selectedIndexValue(
		) | rpl::filter([=](int index) {
			return (index >= 0);
		}) | rpl::start_with_next([=] {
			for (const auto &list : _lists) {
				if (list.content && list.content != content) {
					list.content->clearSelection();
				}
			}
		}, lifetime());
	}
	rows->resizeToWidth(firstController()->contentWidth());

	setDimensions(firstController()->contentWidth(), st::boxMaxListHeight);
	if (_select) {
		_select->finishAnimating();
		Ui::SendPendingMoveResizeEvents(_select);
		_scrollBottomFixed = true;
		onScrollToY(0);
	}

	if (_init) {
		_init(this);
	}
}

void PeerListsBox::keyPressEvent(QKeyEvent *e) {
	const auto skipRows = [&](int rows) {
		if (rows == 0) {
			return;
		}
		for (const auto &list : _lists) {
			if (list.content->hasPressed()) {
				return;
			}
		}
		const auto from = begin(_lists), till = end(_lists);
		auto i = from;
		for (; i != till; ++i) {
			if (i->content->hasSelection()) {
				break;
			}
		}
		if (i == till && rows < 0) {
			return;
		}
		if (rows > 0) {
			if (i == till) {
				i = from;
			}
			for (; i != till; ++i) {
				const auto result = i->content->selectSkip(rows);
				if (result.shouldMoveTo - result.reallyMovedTo >= rows) {
					continue;
				} else if (result.reallyMovedTo >= result.shouldMoveTo) {
					return;
				} else {
					rows = result.shouldMoveTo - result.reallyMovedTo;
				}
			}
		} else {
			for (++i; i != from;) {
				const auto result = (--i)->content->selectSkip(rows);
				if (result.shouldMoveTo - result.reallyMovedTo <= rows) {
					continue;
				} else if (result.reallyMovedTo <= result.shouldMoveTo) {
					return;
				} else {
					rows = result.shouldMoveTo - result.reallyMovedTo;
				}
			}
		}
	};
	const auto rowsInPage = [&] {
		const auto rowHeight = firstController()->computeListSt().item.height;
		return height() / rowHeight;
	};
	if (e->key() == Qt::Key_Down) {
		skipRows(1);
	} else if (e->key() == Qt::Key_Up) {
		skipRows(-1);
	} else if (e->key() == Qt::Key_PageDown) {
		skipRows(rowsInPage());
	} else if (e->key() == Qt::Key_PageUp) {
		skipRows(-rowsInPage());
	} else if (e->key() == Qt::Key_Escape && _select && !_select->entity()->getQuery().isEmpty()) {
		_select->entity()->clearQuery();
	} else {
		BoxContent::keyPressEvent(e);
	}
}

void PeerListsBox::searchQueryChanged(const QString &query) {
	onScrollToY(0);
	for (const auto &list : _lists) {
		list.content->searchQueryChanged(query);
	}
}

void PeerListsBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	if (_select) {
		_select->resizeToWidth(width());
		_select->moveToLeft(0, 0);

		updateScrollSkips();
	}

	for (const auto &list : _lists) {
		list.content->resizeToWidth(width());
	}
}

void PeerListsBox::paintEvent(QPaintEvent *e) {
	Painter p(this);

	const auto &bg = (firstController()->listSt()
		? *firstController()->listSt()
		: st::peerListBox).bg;
	for (const auto rect : e->region()) {
		p.fillRect(rect, bg);
	}
}

void PeerListsBox::setInnerFocus() {
	if (!_select || !_select->toggled()) {
		_lists.front().content->setFocus();
	} else {
		_select->entity()->setInnerFocus();
	}
}

PeerListsBox::Delegate::Delegate(
	not_null<PeerListsBox*> box,
	not_null<PeerListController*> controller)
: _box(box)
, _controller(controller) {
}

void PeerListsBox::Delegate::peerListSetTitle(rpl::producer<QString> title) {
}

void PeerListsBox::Delegate::peerListSetAdditionalTitle(
	rpl::producer<QString> title) {
}

void PeerListsBox::Delegate::peerListSetRowChecked(
		not_null<PeerListRow*> row,
		bool checked) {
	if (checked) {
		_box->addSelectItem(row, anim::type::normal);
		PeerListContentDelegate::peerListSetRowChecked(row, checked);
		peerListUpdateRow(row);

		// This call deletes row from _searchRows.
		_box->_select->entity()->clearQuery();
	} else {
		// The itemRemovedCallback will call changeCheckState() here.
		_box->_select->entity()->removeItem(row->id());
		peerListUpdateRow(row);
	}
}

void PeerListsBox::Delegate::peerListSetForeignRowChecked(
		not_null<PeerListRow*> row,
		bool checked,
		anim::type animated) {
	if (checked) {
		_box->addSelectItem(row, animated);

		// This call deletes row from _searchRows.
		_box->_select->entity()->clearQuery();
	} else {
		// The itemRemovedCallback will call changeCheckState() here.
		_box->_select->entity()->removeItem(row->id());
	}
}

void PeerListsBox::Delegate::peerListScrollToTop() {
	_box->onScrollToY(0);
}

void PeerListsBox::Delegate::peerListSetSearchMode(PeerListSearchMode mode) {
	PeerListContentDelegate::peerListSetSearchMode(mode);
	_box->setSearchMode(mode);
}

void PeerListsBox::setSearchMode(PeerListSearchMode mode) {
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

void PeerListsBox::Delegate::peerListFinishSelectedRowsBunch() {
	Expects(_box->_select != nullptr);

	_box->_select->entity()->finishItemsBunch();
}

bool PeerListsBox::Delegate::peerListIsRowChecked(
		not_null<PeerListRow*> row) {
	return _box->_select
		? _box->_select->entity()->hasItem(row->id())
		: false;
}

int PeerListsBox::Delegate::peerListSelectedRowsCount() {
	return _box->_select ? _box->_select->entity()->getItemsCount() : 0;
}

void PeerListsBox::addSelectItem(
		not_null<PeerData*> peer,
		anim::type animated) {
	addSelectItem(
		peer->id,
		peer->shortName(),
		PaintUserpicCallback(peer, false),
		animated);
}

void PeerListsBox::addSelectItem(
		not_null<PeerListRow*> row,
		anim::type animated) {
	addSelectItem(
		row->id(),
		row->generateShortName(),
		row->generatePaintUserpicCallback(),
		animated);
}

void PeerListsBox::addSelectItem(
		uint64 itemId,
		const QString &text,
		Ui::MultiSelect::PaintRoundImage paintUserpic,
		anim::type animated) {
	if (!_select) {
		createMultiSelect();
		_select->hide(anim::type::instant);
	}
	const auto &activeBg = (firstController()->selectSt()
		? *firstController()->selectSt()
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
