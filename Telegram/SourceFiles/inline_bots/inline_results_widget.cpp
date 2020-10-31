/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "inline_bots/inline_results_widget.h"

#include "api/api_common.h"
#include "chat_helpers/send_context_menu.h" // SendMenu::FillSendMenu
#include "data/data_photo.h"
#include "data/data_document.h"
#include "data/data_channel.h"
#include "data/data_user.h"
#include "data/data_session.h"
#include "data/data_file_origin.h"
#include "data/data_changes.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/shadow.h"
#include "ui/effects/ripple_animation.h"
#include "ui/image/image_prepare.h"
#include "boxes/confirm_box.h"
#include "inline_bots/inline_bot_result.h"
#include "inline_bots/inline_bot_layout_item.h"
#include "dialogs/dialogs_layout.h"
#include "storage/localstorage.h"
#include "lang/lang_keys.h"
#include "mainwindow.h"
#include "mainwidget.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/labels.h"
#include "ui/cached_round_corners.h"
#include "history/view/history_view_cursor_state.h"
#include "facades.h"
#include "styles/style_chat_helpers.h"

#include <QtWidgets/QApplication>

namespace InlineBots {
namespace Layout {
namespace internal {
namespace {

constexpr auto kInlineBotRequestDelay = 400;

} // namespace

Inner::Inner(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: RpWidget(parent)
, _controller(controller)
, _updateInlineItems([=] { updateInlineItems(); })
, _previewTimer([=] { showPreview(); }) {
	resize(st::emojiPanWidth - st::emojiScroll.width - st::buttonRadius, st::inlineResultsMinHeight);

	setMouseTracking(true);
	setAttribute(Qt::WA_OpaquePaintEvent);

	_controller->session().downloaderTaskFinished(
	) | rpl::start_with_next([=] {
		update();
	}, lifetime());

	subscribe(controller->gifPauseLevelChanged(), [this] {
		if (!_controller->isGifPausedAtLeastFor(Window::GifPauseReason::InlineResults)) {
			update();
		}
	});

	_controller->session().changes().peerUpdates(
		Data::PeerUpdate::Flag::Rights
	) | rpl::filter([=](const Data::PeerUpdate &update) {
		return (update.peer.get() == _inlineQueryPeer);
	}) | rpl::start_with_next([=] {
		auto isRestricted = (_restrictedLabel != nullptr);
		if (isRestricted != isRestrictedView()) {
			auto h = countHeight();
			if (h != height()) resize(width(), h);
		}
	}, lifetime());
}

void Inner::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	_visibleBottom = visibleBottom;
	if (_visibleTop != visibleTop) {
		_visibleTop = visibleTop;
		_lastScrolled = crl::now();
	}
}

void Inner::checkRestrictedPeer() {
	if (_inlineQueryPeer) {
		const auto error = Data::RestrictionError(
			_inlineQueryPeer,
			ChatRestriction::f_send_inline);
		if (error) {
			if (!_restrictedLabel) {
				_restrictedLabel.create(this, *error, st::stickersRestrictedLabel);
				_restrictedLabel->show();
				_restrictedLabel->move(st::inlineResultsLeft - st::buttonRadius, st::stickerPanPadding);
				_restrictedLabel->resizeToNaturalWidth(width() - (st::inlineResultsLeft - st::buttonRadius) * 2);
				if (_switchPmButton) {
					_switchPmButton->hide();
				}
				update();
			}
			return;
		}
	}
	if (_restrictedLabel) {
		_restrictedLabel.destroy();
		if (_switchPmButton) {
			_switchPmButton->show();
		}
		update();
	}
}

bool Inner::isRestrictedView() {
	checkRestrictedPeer();
	return (_restrictedLabel != nullptr);
}

int Inner::countHeight() {
	if (isRestrictedView()) {
		return st::stickerPanPadding + _restrictedLabel->height() + st::stickerPanPadding;
	} else if (_rows.isEmpty() && !_switchPmButton) {
		return st::stickerPanPadding + st::normalFont->height + st::stickerPanPadding;
	}
	auto result = st::stickerPanPadding;
	if (_switchPmButton) {
		result += _switchPmButton->height() + st::inlineResultsSkip;
	}
	for (int i = 0, l = _rows.count(); i < l; ++i) {
		result += _rows[i].height;
	}
	return result + st::stickerPanPadding;
}

QString Inner::tooltipText() const {
	if (const auto lnk = ClickHandler::getActive()) {
		return lnk->tooltip();
	}
	return QString();
}

QPoint Inner::tooltipPos() const {
	return _lastMousePos;
}

bool Inner::tooltipWindowActive() const {
	return Ui::AppInFocus() && Ui::InFocusChain(window());
}

rpl::producer<> Inner::inlineRowsCleared() const {
	return _inlineRowsCleared.events();
}

Inner::~Inner() = default;

void Inner::paintEvent(QPaintEvent *e) {
	Painter p(this);
	QRect r = e ? e->rect() : rect();
	if (r != rect()) {
		p.setClipRect(r);
	}
	p.fillRect(r, st::emojiPanBg);

	paintInlineItems(p, r);
}

void Inner::paintInlineItems(Painter &p, const QRect &r) {
	if (_restrictedLabel) {
		return;
	}
	if (_rows.isEmpty() && !_switchPmButton) {
		p.setFont(st::normalFont);
		p.setPen(st::noContactsColor);
		p.drawText(QRect(0, 0, width(), (height() / 3) * 2 + st::normalFont->height), tr::lng_inline_bot_no_results(tr::now), style::al_center);
		return;
	}
	auto gifPaused = _controller->isGifPausedAtLeastFor(Window::GifPauseReason::InlineResults);
	InlineBots::Layout::PaintContext context(crl::now(), false, gifPaused, false);

	auto top = st::stickerPanPadding;
	if (_switchPmButton) {
		top += _switchPmButton->height() + st::inlineResultsSkip;
	}

	auto fromx = rtl() ? (width() - r.x() - r.width()) : r.x();
	auto tox = rtl() ? (width() - r.x()) : (r.x() + r.width());
	for (auto row = 0, rows = _rows.size(); row != rows; ++row) {
		auto &inlineRow = _rows[row];
		if (top >= r.top() + r.height()) break;
		if (top + inlineRow.height > r.top()) {
			auto left = st::inlineResultsLeft - st::buttonRadius;
			if (row == rows - 1) context.lastRow = true;
			for (int col = 0, cols = inlineRow.items.size(); col < cols; ++col) {
				if (left >= tox) break;

				auto item = inlineRow.items.at(col);
				auto w = item->width();
				if (left + w > fromx) {
					p.translate(left, top);
					item->paint(p, r.translated(-left, -top), &context);
					p.translate(-left, -top);
				}
				left += w;
				if (item->hasRightSkip()) {
					left += st::inlineResultsSkip;
				}
			}
		}
		top += inlineRow.height;
	}
}

void Inner::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	_lastMousePos = e->globalPos();
	updateSelected();

	_pressed = _selected;
	ClickHandler::pressed();
	_previewTimer.callOnce(QApplication::startDragTime());
}

void Inner::mouseReleaseEvent(QMouseEvent *e) {
	_previewTimer.cancel();

	auto pressed = std::exchange(_pressed, -1);
	auto activated = ClickHandler::unpressed();

	if (_previewShown) {
		_previewShown = false;
		return;
	}

	_lastMousePos = e->globalPos();
	updateSelected();

	if (_selected < 0 || _selected != pressed || !activated) {
		return;
	}

	if (dynamic_cast<InlineBots::Layout::SendClickHandler*>(activated.get())) {
		int row = _selected / MatrixRowShift, column = _selected % MatrixRowShift;
		selectInlineResult(row, column);
	} else {
		ActivateClickHandler(window(), activated, e->button());
	}
}

void Inner::selectInlineResult(int row, int column) {
	selectInlineResult(row, column, Api::SendOptions());
}

void Inner::selectInlineResult(
		int row,
		int column,
		Api::SendOptions options) {
	if (row >= _rows.size() || column >= _rows.at(row).items.size()) {
		return;
	}

	auto item = _rows[row].items[column];
	if (const auto inlineResult = item->getResult()) {
		if (inlineResult->onChoose(item)) {
			_resultSelectedCallback(
				inlineResult,
				_inlineBot,
				std::move(options));
		}
	}
}

void Inner::mouseMoveEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	updateSelected();
}

void Inner::leaveEventHook(QEvent *e) {
	clearSelection();
	Ui::Tooltip::Hide();
}

void Inner::leaveToChildEvent(QEvent *e, QWidget *child) {
	clearSelection();
}

void Inner::enterFromChildEvent(QEvent *e, QWidget *child) {
	_lastMousePos = QCursor::pos();
	updateSelected();
}

void Inner::contextMenuEvent(QContextMenuEvent *e) {
	if (_selected < 0 || _pressed >= 0) {
		return;
	}
	const auto row = _selected / MatrixRowShift;
	const auto column = _selected % MatrixRowShift;
	const auto type = SendMenu::Type::Scheduled;

	_menu = base::make_unique_q<Ui::PopupMenu>(this);

	const auto send = [=](Api::SendOptions options) {
		selectInlineResult(row, column, options);
	};
	SendMenu::FillSendMenu(
		_menu,
		[&] { return type; },
		SendMenu::DefaultSilentCallback(send),
		SendMenu::DefaultScheduleCallback(this, type, send));

	if (!_menu->actions().empty()) {
		_menu->popup(QCursor::pos());
	}
}

void Inner::clearSelection() {
	if (_selected >= 0) {
		int srow = _selected / MatrixRowShift, scol = _selected % MatrixRowShift;
		Assert(srow >= 0 && srow < _rows.size() && scol >= 0 && scol < _rows.at(srow).items.size());
		ClickHandler::clearActive(_rows.at(srow).items.at(scol));
		setCursor(style::cur_default);
	}
	_selected = _pressed = -1;
	update();
}

void Inner::hideFinished() {
	clearHeavyData();
}

void Inner::clearHeavyData() {
	clearInlineRows(false);
	for (const auto &[result, layout] : _inlineLayouts) {
		layout->unloadHeavyPart();
	}
}

bool Inner::inlineRowsAddItem(Result *result, Row &row, int32 &sumWidth) {
	auto layout = layoutPrepareInlineResult(result, (_rows.size() * MatrixRowShift) + row.items.size());
	if (!layout) return false;

	layout->preload();
	if (inlineRowFinalize(row, sumWidth, layout->isFullLine())) {
		layout->setPosition(_rows.size() * MatrixRowShift);
	}

	sumWidth += layout->maxWidth();
	if (!row.items.isEmpty() && row.items.back()->hasRightSkip()) {
		sumWidth += st::inlineResultsSkip;
	}

	row.items.push_back(layout);
	return true;
}

bool Inner::inlineRowFinalize(Row &row, int32 &sumWidth, bool force) {
	if (row.items.isEmpty()) return false;

	auto full = (row.items.size() >= kInlineItemsMaxPerRow);
	auto big = (sumWidth >= st::emojiPanWidth - st::emojiScroll.width - st::inlineResultsLeft);
	if (full || big || force) {
		_rows.push_back(layoutInlineRow(row, (full || big) ? sumWidth : 0));
		row = Row();
		row.items.reserve(kInlineItemsMaxPerRow);
		sumWidth = 0;
		return true;
	}
	return false;
}

void Inner::inlineBotChanged() {
	refreshInlineRows(nullptr, nullptr, nullptr, true);
}

void Inner::clearInlineRows(bool resultsDeleted) {
	if (resultsDeleted) {
		_selected = _pressed = -1;
	} else {
		clearSelection();
		for_const (auto &row, _rows) {
			for_const (auto &item, row.items) {
				item->setPosition(-1);
			}
		}
	}
	_rows.clear();
}

ItemBase *Inner::layoutPrepareInlineResult(Result *result, int32 position) {
	auto it = _inlineLayouts.find(result);
	if (it == _inlineLayouts.cend()) {
		if (auto layout = ItemBase::createLayout(this, result, _inlineWithThumb)) {
			it = _inlineLayouts.emplace(result, std::move(layout)).first;
			it->second->initDimensions();
		} else {
			return nullptr;
		}
	}
	if (!it->second->maxWidth()) {
		return nullptr;
	}

	it->second->setPosition(position);
	return it->second.get();
}

void Inner::deleteUnusedInlineLayouts() {
	if (_rows.isEmpty()) { // delete all
		_inlineLayouts.clear();
	} else {
		for (auto i = _inlineLayouts.begin(); i != _inlineLayouts.cend();) {
			if (i->second->position() < 0) {
				i = _inlineLayouts.erase(i);
			} else {
				++i;
			}
		}
	}
}

Inner::Row &Inner::layoutInlineRow(Row &row, int32 sumWidth) {
	auto count = int(row.items.size());
	Assert(count <= kInlineItemsMaxPerRow);

	// enumerate items in the order of growing maxWidth()
	// for that sort item indices by maxWidth()
	int indices[kInlineItemsMaxPerRow];
	for (auto i = 0; i != count; ++i) {
		indices[i] = i;
	}
	std::sort(indices, indices + count, [&row](int a, int b) -> bool {
		return row.items.at(a)->maxWidth() < row.items.at(b)->maxWidth();
	});

	row.height = 0;
	int availw = width() - (st::inlineResultsLeft - st::buttonRadius);
	for (int i = 0; i < count; ++i) {
		int index = indices[i];
		int w = sumWidth ? (row.items.at(index)->maxWidth() * availw / sumWidth) : row.items.at(index)->maxWidth();
		int actualw = qMax(w, int(st::inlineResultsMinWidth));
		row.height = qMax(row.height, row.items.at(index)->resizeGetHeight(actualw));
		if (sumWidth) {
			availw -= actualw;
			sumWidth -= row.items.at(index)->maxWidth();
			if (index > 0 && row.items.at(index - 1)->hasRightSkip()) {
				availw -= st::inlineResultsSkip;
				sumWidth -= st::inlineResultsSkip;
			}
		}
	}
	return row;
}

void Inner::preloadImages() {
	for (auto row = 0, rows = _rows.size(); row != rows; ++row) {
		for (auto col = 0, cols = _rows[row].items.size(); col != cols; ++col) {
			_rows[row].items[col]->preload();
		}
	}
}

void Inner::hideInlineRowsPanel() {
	clearInlineRows(false);
}

void Inner::clearInlineRowsPanel() {
	clearInlineRows(false);
}

void Inner::refreshSwitchPmButton(const CacheEntry *entry) {
	if (!entry || entry->switchPmText.isEmpty()) {
		_switchPmButton.destroy();
		_switchPmStartToken.clear();
	} else {
		if (!_switchPmButton) {
			_switchPmButton.create(this, nullptr, st::switchPmButton);
			_switchPmButton->show();
			_switchPmButton->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
			_switchPmButton->addClickHandler([=] { onSwitchPm(); });
		}
		_switchPmButton->setText(rpl::single(entry->switchPmText));
		_switchPmStartToken = entry->switchPmStartToken;
		const auto buttonTop = st::stickerPanPadding;
		_switchPmButton->move(st::inlineResultsLeft - st::buttonRadius, buttonTop);
		if (isRestrictedView()) {
			_switchPmButton->hide();
		}
	}
	update();
}

int Inner::refreshInlineRows(PeerData *queryPeer, UserData *bot, const CacheEntry *entry, bool resultsDeleted) {
	_inlineBot = bot;
	_inlineQueryPeer = queryPeer;
	refreshSwitchPmButton(entry);
	auto clearResults = [&] {
		if (!entry) {
			return true;
		}
		if (entry->results.empty() && entry->switchPmText.isEmpty()) {
			return true;
		}
		return false;
	};
	auto clearResultsResult = clearResults(); // Clang workaround.
	if (clearResultsResult) {
		if (resultsDeleted) {
			clearInlineRows(true);
			deleteUnusedInlineLayouts();
		}
		_inlineRowsCleared.fire({});
		return 0;
	}

	clearSelection();

	Assert(_inlineBot != 0);

	auto count = int(entry->results.size());
	auto from = validateExistingInlineRows(entry->results);
	auto added = 0;

	if (count) {
		_rows.reserve(count);
		auto row = Row();
		row.items.reserve(kInlineItemsMaxPerRow);
		auto sumWidth = 0;
		for (auto i = from; i != count; ++i) {
			if (inlineRowsAddItem(entry->results[i].get(), row, sumWidth)) {
				++added;
			}
		}
		inlineRowFinalize(row, sumWidth, true);
	}

	auto h = countHeight();
	if (h != height()) resize(width(), h);
	update();

	_lastMousePos = QCursor::pos();
	updateSelected();

	return added;
}

int Inner::validateExistingInlineRows(const Results &results) {
	int count = results.size(), until = 0, untilrow = 0, untilcol = 0;
	for (; until < count;) {
		if (untilrow >= _rows.size() || _rows[untilrow].items[untilcol]->getResult() != results[until].get()) {
			break;
		}
		++until;
		if (++untilcol == _rows[untilrow].items.size()) {
			++untilrow;
			untilcol = 0;
		}
	}
	if (until == count) { // all items are layed out
		if (untilrow == _rows.size()) { // nothing changed
			return until;
		}

		for (int i = untilrow, l = _rows.size(), skip = untilcol; i < l; ++i) {
			for (int j = 0, s = _rows[i].items.size(); j < s; ++j) {
				if (skip) {
					--skip;
				} else {
					_rows[i].items[j]->setPosition(-1);
				}
			}
		}
		if (!untilcol) { // all good rows are filled
			_rows.resize(untilrow);
			return until;
		}
		_rows.resize(untilrow + 1);
		_rows[untilrow].items.resize(untilcol);
		_rows[untilrow] = layoutInlineRow(_rows[untilrow]);
		return until;
	}
	if (untilrow && !untilcol) { // remove last row, maybe it is not full
		--untilrow;
		untilcol = _rows[untilrow].items.size();
	}
	until -= untilcol;

	for (int i = untilrow, l = _rows.size(); i < l; ++i) {
		for (int j = 0, s = _rows[i].items.size(); j < s; ++j) {
			_rows[i].items[j]->setPosition(-1);
		}
	}
	_rows.resize(untilrow);

	if (_rows.isEmpty()) {
		_inlineWithThumb = false;
		for (int i = until; i < count; ++i) {
			if (results.at(i)->hasThumbDisplay()) {
				_inlineWithThumb = true;
				break;
			}
		}
	}
	return until;
}

void Inner::inlineItemLayoutChanged(const ItemBase *layout) {
	if (_selected < 0 || !isVisible()) {
		return;
	}

	int row = _selected / MatrixRowShift, col = _selected % MatrixRowShift;
	if (row < _rows.size() && col < _rows.at(row).items.size()) {
		if (layout == _rows.at(row).items.at(col)) {
			updateSelected();
		}
	}
}

void Inner::inlineItemRepaint(const ItemBase *layout) {
	auto ms = crl::now();
	if (_lastScrolled + 100 <= ms) {
		update();
	} else {
		_updateInlineItems.callOnce(_lastScrolled + 100 - ms);
	}
}

bool Inner::inlineItemVisible(const ItemBase *layout) {
	int32 position = layout->position();
	if (position < 0 || !isVisible()) {
		return false;
	}

	int row = position / MatrixRowShift, col = position % MatrixRowShift;
	Assert((row < _rows.size()) && (col < _rows[row].items.size()));

	auto &inlineItems = _rows[row].items;
	int top = st::stickerPanPadding;
	for (int32 i = 0; i < row; ++i) {
		top += _rows.at(i).height;
	}

	return (top < _visibleBottom) && (top + _rows[row].items[col]->height() > _visibleTop);
}

Data::FileOrigin Inner::inlineItemFileOrigin() {
	return Data::FileOrigin();
}

void Inner::updateSelected() {
	if (_pressed >= 0 && !_previewShown) {
		return;
	}

	auto newSelected = -1;
	auto p = mapFromGlobal(_lastMousePos);

	int sx = (rtl() ? width() - p.x() : p.x()) - (st::inlineResultsLeft - st::buttonRadius);
	int sy = p.y() - st::stickerPanPadding;
	if (_switchPmButton) {
		sy -= _switchPmButton->height() + st::inlineResultsSkip;
	}
	int row = -1, col = -1, sel = -1;
	ClickHandlerPtr lnk;
	ClickHandlerHost *lnkhost = nullptr;
	HistoryView::CursorState cursor = HistoryView::CursorState::None;
	if (sy >= 0) {
		row = 0;
		for (int rows = _rows.size(); row < rows; ++row) {
			if (sy < _rows[row].height) {
				break;
			}
			sy -= _rows[row].height;
		}
	}
	if (sx >= 0 && row >= 0 && row < _rows.size()) {
		auto &inlineItems = _rows[row].items;
		col = 0;
		for (int cols = inlineItems.size(); col < cols; ++col) {
			int width = inlineItems.at(col)->width();
			if (sx < width) {
				break;
			}
			sx -= width;
			if (inlineItems.at(col)->hasRightSkip()) {
				sx -= st::inlineResultsSkip;
			}
		}
		if (col < inlineItems.size()) {
			sel = row * MatrixRowShift + col;
			auto result = inlineItems[col]->getState(
				QPoint(sx, sy),
				HistoryView::StateRequest());
			lnk = result.link;
			cursor = result.cursor;
			lnkhost = inlineItems[col];
		} else {
			row = col = -1;
		}
	} else {
		row = col = -1;
	}
	int srow = (_selected >= 0) ? (_selected / MatrixRowShift) : -1;
	int scol = (_selected >= 0) ? (_selected % MatrixRowShift) : -1;
	if (_selected != sel) {
		if (srow >= 0 && scol >= 0) {
			Assert(srow >= 0 && srow < _rows.size() && scol >= 0 && scol < _rows.at(srow).items.size());
			_rows[srow].items[scol]->update();
		}
		_selected = sel;
		if (row >= 0 && col >= 0) {
			Assert(row >= 0 && row < _rows.size() && col >= 0 && col < _rows.at(row).items.size());
			_rows[row].items[col]->update();
		}
		if (_previewShown && _selected >= 0 && _pressed != _selected) {
			_pressed = _selected;
			if (row >= 0 && col >= 0) {
				auto layout = _rows.at(row).items.at(col);
				if (const auto w = App::wnd()) {
					if (const auto previewDocument = layout->getPreviewDocument()) {
						w->showMediaPreview(
							Data::FileOrigin(),
							previewDocument);
					} else if (auto previewPhoto = layout->getPreviewPhoto()) {
						w->showMediaPreview(Data::FileOrigin(), previewPhoto);
					}
				}
			}
		}
	}
	if (ClickHandler::setActive(lnk, lnkhost)) {
		setCursor(lnk ? style::cur_pointer : style::cur_default);
		Ui::Tooltip::Hide();
	}
	if (lnk) {
		Ui::Tooltip::Show(1000, this);
	}
}

void Inner::showPreview() {
	if (_pressed < 0) return;

	int row = _pressed / MatrixRowShift, col = _pressed % MatrixRowShift;
	if (row < _rows.size() && col < _rows.at(row).items.size()) {
		auto layout = _rows.at(row).items.at(col);
		if (const auto w = App::wnd()) {
			if (const auto previewDocument = layout->getPreviewDocument()) {
				_previewShown = w->showMediaPreview(Data::FileOrigin(), previewDocument);
			} else if (const auto previewPhoto = layout->getPreviewPhoto()) {
				_previewShown = w->showMediaPreview(Data::FileOrigin(), previewPhoto);
			}
		}
	}
}

void Inner::updateInlineItems() {
	auto ms = crl::now();
	if (_lastScrolled + 100 <= ms) {
		update();
	} else {
		_updateInlineItems.callOnce(_lastScrolled + 100 - ms);
	}
}

void Inner::onSwitchPm() {
	if (_inlineBot && _inlineBot->isBot()) {
		_inlineBot->botInfo->startToken = _switchPmStartToken;
		Ui::showPeerHistory(_inlineBot, ShowAndStartBotMsgId);
	}
}

} // namespace internal

Widget::Widget(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: RpWidget(parent)
, _controller(controller)
, _api(&_controller->session().mtp())
, _contentMaxHeight(st::emojiPanMaxHeight)
, _contentHeight(_contentMaxHeight)
, _scroll(this, st::inlineBotsScroll)
, _inlineRequestTimer([=] { onInlineRequest(); }) {
	resize(QRect(0, 0, st::emojiPanWidth, _contentHeight).marginsAdded(innerPadding()).size());
	_width = width();
	_height = height();

	_scroll->resize(st::emojiPanWidth - st::buttonRadius, _contentHeight);

	_scroll->move(verticalRect().topLeft());
	_inner = _scroll->setOwnedWidget(object_ptr<internal::Inner>(this, controller));

	_inner->moveToLeft(0, 0, _scroll->width());

	connect(
		_scroll,
		&Ui::ScrollArea::scrolled,
		this,
		&InlineBots::Layout::Widget::onScroll);

	_inner->inlineRowsCleared(
	) | rpl::start_with_next([=] {
		hideAnimated();
		_inner->clearInlineRowsPanel();
	}, lifetime());

	macWindowDeactivateEvents(
	) | rpl::filter([=] {
		return !isHidden();
	}) | rpl::start_with_next([=] {
		leaveEvent(nullptr);
	}, lifetime());

	// Inner widget has OpaquePaintEvent attribute so it doesn't repaint on scroll.
	// But we should force it to repaint so that GIFs will continue to animate without update() calls.
	// We do that by creating a transparent widget above our _inner.
	auto forceRepaintOnScroll = object_ptr<TWidget>(this);
	forceRepaintOnScroll->setGeometry(innerRect().x() + st::buttonRadius, innerRect().y() + st::buttonRadius, st::buttonRadius, st::buttonRadius);
	forceRepaintOnScroll->setAttribute(Qt::WA_TransparentForMouseEvents);
	forceRepaintOnScroll->show();

	setMouseTracking(true);
	setAttribute(Qt::WA_OpaquePaintEvent, false);
}

void Widget::moveBottom(int bottom) {
	_bottom = bottom;
	updateContentHeight();
}

void Widget::updateContentHeight() {
	auto addedHeight = innerPadding().top() + innerPadding().bottom();
	auto wantedContentHeight = qRound(st::emojiPanHeightRatio * _bottom) - addedHeight;
	auto contentHeight = snap(wantedContentHeight, st::inlineResultsMinHeight, st::inlineResultsMaxHeight);
	accumulate_min(contentHeight, _bottom - addedHeight);
	accumulate_min(contentHeight, _contentMaxHeight);
	auto resultTop = _bottom - addedHeight - contentHeight;
	if (contentHeight == _contentHeight) {
		move(x(), resultTop);
		return;
	}

	auto was = _contentHeight;
	_contentHeight = contentHeight;

	resize(QRect(0, 0, innerRect().width(), _contentHeight).marginsAdded(innerPadding()).size());
	_height = height();
	moveToLeft(0, resultTop);

	if (was > _contentHeight) {
		_scroll->resize(_scroll->width(), _contentHeight);
		auto scrollTop = _scroll->scrollTop();
		_inner->setVisibleTopBottom(scrollTop, scrollTop + _contentHeight);
	} else {
		auto scrollTop = _scroll->scrollTop();
		_inner->setVisibleTopBottom(scrollTop, scrollTop + _contentHeight);
		_scroll->resize(_scroll->width(), _contentHeight);
	}

	update();
}

void Widget::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto opacityAnimating = _a_opacity.animating();

	auto showAnimating = _a_show.animating();
	if (_showAnimation && !showAnimating) {
		_showAnimation.reset();
		if (!opacityAnimating) {
			showChildren();
		}
	}

	if (showAnimating) {
		Assert(_showAnimation != nullptr);
		if (auto opacity = _a_opacity.value(_hiding ? 0. : 1.)) {
			_showAnimation->paintFrame(p, 0, 0, width(), _a_show.value(1.), opacity);
		}
	} else if (opacityAnimating) {
		p.setOpacity(_a_opacity.value(_hiding ? 0. : 1.));
		p.drawPixmap(0, 0, _cache);
	} else if (_hiding || isHidden()) {
		hideFinished();
	} else {
		if (!_cache.isNull()) _cache = QPixmap();
		if (!_inPanelGrab) Ui::Shadow::paint(p, innerRect(), width(), st::emojiPanAnimation.shadow);
		paintContent(p);
	}
}

void Widget::paintContent(Painter &p) {
	auto inner = innerRect();
	Ui::FillRoundRect(p, inner, st::emojiPanBg, ImageRoundRadius::Small, RectPart::FullTop | RectPart::FullBottom);

	auto horizontal = horizontalRect();
	auto sidesTop = horizontal.y();
	auto sidesHeight = horizontal.height();
	p.fillRect(myrtlrect(inner.x() + inner.width() - st::emojiScroll.width, sidesTop, st::emojiScroll.width, sidesHeight), st::emojiPanBg);
	p.fillRect(myrtlrect(inner.x(), sidesTop, st::buttonRadius, sidesHeight), st::emojiPanBg);
}

void Widget::moveByBottom() {
	updateContentHeight();
}

void Widget::hideFast() {
	if (isHidden()) return;

	_hiding = false;
	_a_opacity.stop();
	hideFinished();
}

void Widget::opacityAnimationCallback() {
	update();
	if (!_a_opacity.animating()) {
		if (_hiding) {
			_hiding = false;
			hideFinished();
		} else if (!_a_show.animating()) {
			showChildren();
		}
	}
}

void Widget::prepareCache() {
	if (_a_opacity.animating()) return;

	auto showAnimation = base::take(_a_show);
	auto showAnimationData = base::take(_showAnimation);
	showChildren();
	_cache = Ui::GrabWidget(this);
	_showAnimation = base::take(showAnimationData);
	_a_show = base::take(showAnimation);
	if (_a_show.animating()) {
		hideChildren();
	}
}

void Widget::startOpacityAnimation(bool hiding) {
	_hiding = false;
	prepareCache();
	_hiding = hiding;
	hideChildren();
	_a_opacity.start([this] { opacityAnimationCallback(); }, _hiding ? 1. : 0., _hiding ? 0. : 1., st::emojiPanDuration);
}

void Widget::startShowAnimation() {
	if (!_a_show.animating()) {
		auto cache = base::take(_cache);
		auto opacityAnimation = base::take(_a_opacity);
		showChildren();
		auto image = grabForPanelAnimation();
		_a_opacity = base::take(opacityAnimation);
		_cache = base::take(_cache);

		_showAnimation = std::make_unique<Ui::PanelAnimation>(st::emojiPanAnimation, Ui::PanelAnimation::Origin::BottomLeft);
		auto inner = rect().marginsRemoved(st::emojiPanMargins);
		_showAnimation->setFinalImage(std::move(image), QRect(inner.topLeft() * cIntRetinaFactor(), inner.size() * cIntRetinaFactor()));
		_showAnimation->setCornerMasks(Images::CornersMask(ImageRoundRadius::Small));
		_showAnimation->start();
	}
	hideChildren();
	_a_show.start([this] { update(); }, 0., 1., st::emojiPanShowDuration);
}

QImage Widget::grabForPanelAnimation() {
	Ui::SendPendingMoveResizeEvents(this);
	auto result = QImage(size() * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(cRetinaFactor());
	result.fill(Qt::transparent);
	_inPanelGrab = true;
	render(&result);
	_inPanelGrab = false;
	return result;
}

void Widget::hideAnimated() {
	if (isHidden()) return;
	if (_hiding) return;

	startOpacityAnimation(true);
}

Widget::~Widget() = default;

void Widget::hideFinished() {
	hide();
	_controller->disableGifPauseReason(
		Window::GifPauseReason::InlineResults);

	_inner->hideFinished();
	_a_show.stop();
	_showAnimation.reset();
	_cache = QPixmap();
	_horizontal = false;
	_hiding = false;

	_scroll->scrollToY(0);
}

void Widget::showAnimated() {
	showStarted();
}

void Widget::showStarted() {
	if (isHidden()) {
		recountContentMaxHeight();
		_inner->preloadImages();
		show();
		_controller->enableGifPauseReason(
			Window::GifPauseReason::InlineResults);
		startShowAnimation();
	} else if (_hiding) {
		startOpacityAnimation(false);
	}
}

void Widget::onScroll() {
	auto st = _scroll->scrollTop();
	if (st + _scroll->height() > _scroll->scrollTopMax()) {
		onInlineRequest();
	}
	_inner->setVisibleTopBottom(st, st + _scroll->height());
}

style::margins Widget::innerPadding() const {
	return st::emojiPanMargins;
}

QRect Widget::innerRect() const {
	return rect().marginsRemoved(innerPadding());
}

QRect Widget::horizontalRect() const {
	return innerRect().marginsRemoved(style::margins(0, st::buttonRadius, 0, st::buttonRadius));
}

QRect Widget::verticalRect() const {
	return innerRect().marginsRemoved(style::margins(st::buttonRadius, 0, st::buttonRadius, 0));
}

void Widget::clearInlineBot() {
	inlineBotChanged();
}

bool Widget::overlaps(const QRect &globalRect) const {
	if (isHidden() || !_cache.isNull()) return false;

	auto testRect = QRect(mapFromGlobal(globalRect.topLeft()), globalRect.size());
	auto inner = rect().marginsRemoved(st::emojiPanMargins);
	return inner.marginsRemoved(QMargins(st::buttonRadius, 0, st::buttonRadius, 0)).contains(testRect)
		|| inner.marginsRemoved(QMargins(0, st::buttonRadius, 0, st::buttonRadius)).contains(testRect);
}

void Widget::inlineBotChanged() {
	if (!_inlineBot) {
		return;
	}

	if (!isHidden() && !_hiding) {
		hideAnimated();
	}

	_api.request(base::take(_inlineRequestId)).cancel();
	_inlineQuery = _inlineNextQuery = _inlineNextOffset = QString();
	_inlineBot = nullptr;
	_inlineCache.clear();
	_inner->inlineBotChanged();
	_inner->hideInlineRowsPanel();

	_requesting.fire(false);
}

void Widget::inlineResultsDone(const MTPmessages_BotResults &result) {
	_inlineRequestId = 0;
	_requesting.fire(false);

	auto it = _inlineCache.find(_inlineQuery);
	auto adding = (it != _inlineCache.cend());
	if (result.type() == mtpc_messages_botResults) {
		auto &d = result.c_messages_botResults();
		_controller->session().data().processUsers(d.vusers());

		auto &v = d.vresults().v;
		auto queryId = d.vquery_id().v;

		if (it == _inlineCache.cend()) {
			it = _inlineCache.emplace(
				_inlineQuery,
				std::make_unique<internal::CacheEntry>()).first;
		}
		auto entry = it->second.get();
		entry->nextOffset = qs(d.vnext_offset().value_or_empty());
		if (const auto switchPm = d.vswitch_pm()) {
			switchPm->match([&](const MTPDinlineBotSwitchPM &data) {
				entry->switchPmText = qs(data.vtext());
				entry->switchPmStartToken = qs(data.vstart_param());
			});
		}

		if (auto count = v.size()) {
			entry->results.reserve(entry->results.size() + count);
		}
		auto added = 0;
		for (const auto &res : v) {
			auto result = InlineBots::Result::Create(
				&_controller->session(),
				queryId,
				res);
			if (result) {
				++added;
				entry->results.push_back(std::move(result));
			}
		}

		if (!added) {
			entry->nextOffset = QString();
		}
	} else if (adding) {
		it->second->nextOffset = QString();
	}

	if (!showInlineRows(!adding)) {
		it->second->nextOffset = QString();
	}
	onScroll();
}

void Widget::queryInlineBot(UserData *bot, PeerData *peer, QString query) {
	bool force = false;
	_inlineQueryPeer = peer;
	if (bot != _inlineBot) {
		inlineBotChanged();
		_inlineBot = bot;
		force = true;
	}

	if (_inlineQuery != query || force) {
		if (_inlineRequestId) {
			_api.request(_inlineRequestId).cancel();
			_inlineRequestId = 0;
			_requesting.fire(false);
		}
		if (_inlineCache.find(query) != _inlineCache.cend()) {
			_inlineRequestTimer.cancel();
			_inlineQuery = _inlineNextQuery = query;
			showInlineRows(true);
		} else {
			_inlineNextQuery = query;
			_inlineRequestTimer.callOnce(internal::kInlineBotRequestDelay);
		}
	}
}

void Widget::onInlineRequest() {
	if (_inlineRequestId || !_inlineBot || !_inlineQueryPeer) return;
	_inlineQuery = _inlineNextQuery;

	QString nextOffset;
	auto it = _inlineCache.find(_inlineQuery);
	if (it != _inlineCache.cend()) {
		nextOffset = it->second->nextOffset;
		if (nextOffset.isEmpty()) {
			return;
		}
	}
	_requesting.fire(true);
	_inlineRequestId = _api.request(MTPmessages_GetInlineBotResults(
		MTP_flags(0),
		_inlineBot->inputUser,
		_inlineQueryPeer->input,
		MTPInputGeoPoint(),
		MTP_string(_inlineQuery),
		MTP_string(nextOffset)
	)).done([=](const MTPmessages_BotResults &result) {
		inlineResultsDone(result);
	}).fail([=](const RPCError &error) {
		// show error?
		_requesting.fire(false);
		_inlineRequestId = 0;
	}).handleAllErrors().send();
}

bool Widget::refreshInlineRows(int *added) {
	auto it = _inlineCache.find(_inlineQuery);
	const internal::CacheEntry *entry = nullptr;
	if (it != _inlineCache.cend()) {
		if (!it->second->results.empty() || !it->second->switchPmText.isEmpty()) {
			entry = it->second.get();
		}
		_inlineNextOffset = it->second->nextOffset;
	}
	if (!entry) prepareCache();
	auto result = _inner->refreshInlineRows(_inlineQueryPeer, _inlineBot, entry, false);
	if (added) *added = result;
	return (entry != nullptr);
}

int Widget::showInlineRows(bool newResults) {
	auto added = 0;
	auto clear = !refreshInlineRows(&added);
	if (newResults) {
		_scroll->scrollToY(0);
	}

	auto hidden = isHidden();
	if (!hidden && !clear) {
		recountContentMaxHeight();
	}
	if (clear) {
		if (!hidden) {
			hideAnimated();
		} else if (!_hiding) {
			_cache = QPixmap(); // clear after refreshInlineRows()
		}
	} else {
		if (hidden || _hiding) {
			showAnimated();
		}
	}

	return added;
}

void Widget::recountContentMaxHeight() {
	_contentMaxHeight = _inner->countHeight();
	updateContentHeight();
}

} // namespace Layout
} // namespace InlineBots
