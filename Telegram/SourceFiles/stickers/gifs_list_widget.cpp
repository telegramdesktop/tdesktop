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
#include "stickers/gifs_list_widget.h"

#include "styles/style_stickers.h"
#include "ui/widgets/buttons.h"
#include "ui/effects/ripple_animation.h"
#include "boxes/stickers_box.h"
#include "inline_bots/inline_bot_result.h"
#include "stickers/stickers.h"
#include "storage/localstorage.h"
#include "lang.h"
#include "mainwindow.h"

namespace ChatHelpers {
namespace {

constexpr auto kSaveChosenTabTimeout = 1000;
constexpr auto kStickersPanelPerRow = Stickers::kPanelPerRow;
constexpr auto kInlineItemsMaxPerRow = 5;

} // namespace

class GifsListWidget::Controller : public TWidget {
public:
	Controller(gsl::not_null<GifsListWidget*> parent);

private:
	gsl::not_null<GifsListWidget*> _pan;

};

GifsListWidget::Controller::Controller(gsl::not_null<GifsListWidget*> parent) : TWidget(parent)
, _pan(parent) {
}

GifsListWidget::GifsListWidget(QWidget *parent) : Inner(parent)
, _section(Section::Gifs) {
	resize(st::emojiPanWidth - st::emojiScroll.width - st::buttonRadius, countHeight());

	setMouseTracking(true);
	setAttribute(Qt::WA_OpaquePaintEvent);

	_previewTimer.setSingleShot(true);
	connect(&_previewTimer, SIGNAL(timeout()), this, SLOT(onPreview()));

	_updateInlineItems.setSingleShot(true);
	connect(&_updateInlineItems, SIGNAL(timeout()), this, SLOT(onUpdateInlineItems()));

	subscribe(AuthSession::CurrentDownloaderTaskFinished(), [this] {
		update();
	});
}

object_ptr<TWidget> GifsListWidget::createController() {
	return object_ptr<Controller>(this);
}

void GifsListWidget::setVisibleTopBottom(int visibleTop, int visibleBottom) {
	auto top = getVisibleTop();
	Inner::setVisibleTopBottom(visibleTop, visibleBottom);
	if (top != getVisibleTop()) {
		_lastScrolled = getms();
	}
}

int GifsListWidget::countHeight() {
	auto visibleHeight = getVisibleBottom() - getVisibleTop();
	if (visibleHeight <= 0) {
		visibleHeight = st::emojiPanMaxHeight - st::emojiCategory.height;
	}
	auto minimalLastHeight = (visibleHeight - st::stickerPanPadding);
	auto result = st::stickerPanPadding;
	if (_switchPmButton) {
		result += _switchPmButton->height() + st::inlineResultsSkip;
	}
	for (int i = 0, l = _inlineRows.count(); i < l; ++i) {
		result += _inlineRows[i].height;
	}
	return qMax(minimalLastHeight, result) + st::stickerPanPadding;
}

GifsListWidget::~GifsListWidget() {
	clearInlineRows(true);
	deleteUnusedGifLayouts();
	deleteUnusedInlineLayouts();
}

void GifsListWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);
	auto clip = e->rect();
	p.fillRect(clip, st::emojiPanBg);

	paintInlineItems(p, clip);
}

void GifsListWidget::paintInlineItems(Painter &p, QRect clip) {
	if (_inlineRows.isEmpty() && !_switchPmButton) {
		p.setFont(st::normalFont);
		p.setPen(st::noContactsColor);
		p.drawText(QRect(0, 0, width(), (height() / 3) * 2 + st::normalFont->height), lang(lng_inline_bot_no_results), style::al_center);
		return;
	}
	auto gifPaused = Ui::isLayerShown() || Ui::isMediaViewShown() || _previewShown || !App::wnd()->isActive();
	InlineBots::Layout::PaintContext context(getms(), false, gifPaused, false);

	auto top = st::stickerPanPadding;
	if (_switchPmButton) {
		top += _switchPmButton->height() + st::inlineResultsSkip;
	}

	auto fromx = rtl() ? (width() - clip.x() - clip.width()) : clip.x();
	auto tox = rtl() ? (width() - clip.x()) : (clip.x() + clip.width());
	for (auto row = 0, rows = _inlineRows.size(); row != rows; ++row) {
		auto &inlineRow = _inlineRows[row];
		if (top >= clip.top() + clip.height()) {
			break;
		}
		if (top + inlineRow.height > clip.top()) {
			auto left = st::inlineResultsLeft - st::buttonRadius;
			if (row == rows - 1) context.lastRow = true;
			for (int col = 0, cols = inlineRow.items.size(); col < cols; ++col) {
				if (left >= tox) break;

				auto item = inlineRow.items.at(col);
				auto w = item->width();
				if (left + w > fromx) {
					p.translate(left, top);
					item->paint(p, clip.translated(-left, -top), &context);
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

void GifsListWidget::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	_lastMousePos = e->globalPos();
	updateSelected();

	_pressed = _selected;
	ClickHandler::pressed();
	_previewTimer.start(QApplication::startDragTime());
}

void GifsListWidget::mouseReleaseEvent(QMouseEvent *e) {
	_previewTimer.stop();

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

	if (dynamic_cast<InlineBots::Layout::SendClickHandler*>(activated.data())) {
		int row = _selected / MatrixRowShift, column = _selected % MatrixRowShift;
		selectInlineResult(row, column);
	} else {
		App::activateClickHandler(activated, e->button());
	}
}

void GifsListWidget::selectInlineResult(int row, int column) {
	if (row >= _inlineRows.size() || column >= _inlineRows.at(row).items.size()) {
		return;
	}

	auto item = _inlineRows[row].items[column];
	if (auto photo = item->getPhoto()) {
		if (photo->medium->loaded() || photo->thumb->loaded()) {
			emit selected(photo);
		} else if (!photo->medium->loading()) {
			photo->thumb->loadEvenCancelled();
			photo->medium->loadEvenCancelled();
		}
	} else if (auto document = item->getDocument()) {
		if (document->loaded()) {
			emit selected(document);
		} else if (document->loading()) {
			document->cancel();
		} else {
			DocumentOpenClickHandler::doOpen(document, nullptr, ActionOnLoadNone);
		}
	} else if (auto inlineResult = item->getResult()) {
		if (inlineResult->onChoose(item)) {
			emit selected(inlineResult, _inlineBot);
		}
	}
}

void GifsListWidget::mouseMoveEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	updateSelected();
}

void GifsListWidget::leaveEventHook(QEvent *e) {
	clearSelection();
}

void GifsListWidget::leaveToChildEvent(QEvent *e, QWidget *child) {
	clearSelection();
}

void GifsListWidget::enterFromChildEvent(QEvent *e, QWidget *child) {
	_lastMousePos = QCursor::pos();
	updateSelected();
}

void GifsListWidget::clearSelection() {
	if (_selected >= 0) {
		int srow = _selected / MatrixRowShift, scol = _selected % MatrixRowShift;
		t_assert(srow >= 0 && srow < _inlineRows.size() && scol >= 0 && scol < _inlineRows.at(srow).items.size());
		ClickHandler::clearActive(_inlineRows.at(srow).items.at(scol));
		setCursor(style::cur_default);
	}
	_selected = _pressed = -1;
	update();
}

void GifsListWidget::hideFinish(bool completely) {
	clearSelection();
	if (completely) {
		auto itemForget = [](auto &item) {
			if (auto document = item->getDocument()) {
				document->forget();
			}
			if (auto photo = item->getPhoto()) {
				photo->forget();
			}
			if (auto result = item->getResult()) {
				result->forget();
			}
		};
		clearInlineRows(false);
		for_const (auto &item, _gifLayouts) {
			itemForget(item.second);
		}
		for_const (auto &item, _inlineLayouts) {
			itemForget(item.second);
		}
	}
}

bool GifsListWidget::inlineRowsAddItem(DocumentData *savedGif, InlineResult *result, InlineRow &row, int32 &sumWidth) {
	InlineItem *layout = nullptr;
	if (savedGif) {
		layout = layoutPrepareSavedGif(savedGif, (_inlineRows.size() * MatrixRowShift) + row.items.size());
	} else if (result) {
		layout = layoutPrepareInlineResult(result, (_inlineRows.size() * MatrixRowShift) + row.items.size());
	}
	if (!layout) return false;

	layout->preload();
	if (inlineRowFinalize(row, sumWidth, layout->isFullLine())) {
		layout->setPosition(_inlineRows.size() * MatrixRowShift);
	}

	sumWidth += layout->maxWidth();
	if (!row.items.isEmpty() && row.items.back()->hasRightSkip()) {
		sumWidth += st::inlineResultsSkip;
	}

	row.items.push_back(layout);
	return true;
}

bool GifsListWidget::inlineRowFinalize(InlineRow &row, int32 &sumWidth, bool force) {
	if (row.items.isEmpty()) return false;

	auto full = (row.items.size() >= kInlineItemsMaxPerRow);
	auto big = (sumWidth >= st::emojiPanWidth - st::emojiScroll.width - st::inlineResultsLeft);
	if (full || big || force) {
		_inlineRows.push_back(layoutInlineRow(row, (full || big) ? sumWidth : 0));
		row = InlineRow();
		row.items.reserve(kInlineItemsMaxPerRow);
		sumWidth = 0;
		return true;
	}
	return false;
}

void GifsListWidget::refreshSavedGifs() {
	if (_section == Section::Gifs) {
		clearInlineRows(false);

		auto &saved = cSavedGifs();
		if (!saved.isEmpty()) {
			_inlineRows.reserve(saved.size());
			auto row = InlineRow();
			row.items.reserve(kInlineItemsMaxPerRow);
			auto sumWidth = 0;
			for_const (auto &gif, saved) {
				inlineRowsAddItem(gif, 0, row, sumWidth);
			}
			inlineRowFinalize(row, sumWidth, true);
		}
		deleteUnusedGifLayouts();

		int32 h = countHeight();
		if (h != height()) resize(width(), h);

		update();
	}
	updateSelected();
}

void GifsListWidget::inlineBotChanged() {
	refreshInlineRows(nullptr, nullptr, true);
}

void GifsListWidget::clearInlineRows(bool resultsDeleted) {
	if (resultsDeleted) {
		_selected = _pressed = -1;
	} else {
		clearSelection();
		for_const (auto &row, _inlineRows) {
			for_const (auto &item, row.items) {
				item->setPosition(-1);
			}
		}
	}
	_inlineRows.clear();
}

InlineItem *GifsListWidget::layoutPrepareSavedGif(DocumentData *doc, int32 position) {
	auto it = _gifLayouts.find(doc);
	if (it == _gifLayouts.cend()) {
		if (auto layout = InlineItem::createLayoutGif(this, doc)) {
			it = _gifLayouts.emplace(doc, std::move(layout)).first;
			it->second->initDimensions();
		} else {
			return nullptr;
		}
	}
	if (!it->second->maxWidth()) return nullptr;

	it->second->setPosition(position);
	return it->second.get();
}

InlineItem *GifsListWidget::layoutPrepareInlineResult(InlineResult *result, int32 position) {
	auto it = _inlineLayouts.find(result);
	if (it == _inlineLayouts.cend()) {
		if (auto layout = InlineItem::createLayout(this, result, _inlineWithThumb)) {
			it = _inlineLayouts.emplace(result, std::move(layout)).first;
			it->second->initDimensions();
		} else {
			return nullptr;
		}
	}
	if (!it->second->maxWidth()) return nullptr;

	it->second->setPosition(position);
	return it->second.get();
}

void GifsListWidget::deleteUnusedGifLayouts() {
	if (_inlineRows.isEmpty() || _section != Section::Gifs) { // delete all
		_gifLayouts.clear();
	} else {
		for (auto i = _gifLayouts.begin(); i != _gifLayouts.cend();) {
			if (i->second->position() < 0) {
				i = _gifLayouts.erase(i);
			} else {
				++i;
			}
		}
	}
}

void GifsListWidget::deleteUnusedInlineLayouts() {
	if (_inlineRows.isEmpty() || _section == Section::Gifs) { // delete all
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

GifsListWidget::InlineRow &GifsListWidget::layoutInlineRow(InlineRow &row, int32 sumWidth) {
	auto count = int(row.items.size());
	t_assert(count <= kInlineItemsMaxPerRow);

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

void GifsListWidget::preloadImages() {
	for (auto row = 0, rows = _inlineRows.size(); row != rows; ++row) {
		for (auto col = 0, cols = _inlineRows[row].items.size(); col != cols; ++col) {
			_inlineRows[row].items[col]->preload();
		}
	}
}

void GifsListWidget::hideInlineRowsPanel() {
	clearInlineRows(false);
	_section = Section::Gifs;
	refreshSavedGifs();
	emit scrollToY(0);
	emit scrollUpdated();
}

void GifsListWidget::clearInlineRowsPanel() {
	clearInlineRows(false);
}

void GifsListWidget::refreshSwitchPmButton(const InlineCacheEntry *entry) {
	if (!entry || entry->switchPmText.isEmpty()) {
		_switchPmButton.destroy();
		_switchPmStartToken.clear();
	} else {
		if (!_switchPmButton) {
			_switchPmButton.create(this, QString(), st::switchPmButton);
			_switchPmButton->show();
			_switchPmButton->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
			connect(_switchPmButton, SIGNAL(clicked()), this, SLOT(onSwitchPm()));
		}
		_switchPmButton->setText(entry->switchPmText); // doesn't perform text.toUpper()
		_switchPmStartToken = entry->switchPmStartToken;
		auto buttonTop = st::stickerPanPadding;
		_switchPmButton->move(st::inlineResultsLeft - st::buttonRadius, buttonTop);
	}
	update();
}

int GifsListWidget::refreshInlineRows(UserData *bot, const InlineCacheEntry *entry, bool resultsDeleted) {
	_inlineBot = bot;
	refreshSwitchPmButton(entry);
	auto clearResults = [this, entry]() {
		if (!entry) {
			return true;
		}
		if (entry->results.empty() && entry->switchPmText.isEmpty()) {
			if (!_inlineBot) {
				return true;
			}
		}
		return false;
	};
	if (clearResults()) {
		if (resultsDeleted) {
			clearInlineRows(true);
			deleteUnusedInlineLayouts();
		}
		emit emptyInlineRows();
		return 0;
	}

	clearSelection();

	t_assert(_inlineBot != 0);
	_inlineBotTitle = lng_inline_bot_results(lt_inline_bot, _inlineBot->username.isEmpty() ? _inlineBot->name : ('@' + _inlineBot->username));

	_section = Section::Inlines;
	auto count = int(entry->results.size());
	auto from = validateExistingInlineRows(entry->results);
	auto added = 0;

	if (count) {
		_inlineRows.reserve(count);
		auto row = InlineRow();
		row.items.reserve(kInlineItemsMaxPerRow);
		auto sumWidth = 0;
		for (auto i = from; i != count; ++i) {
			if (inlineRowsAddItem(0, entry->results[i].get(), row, sumWidth)) {
				++added;
			}
		}
		inlineRowFinalize(row, sumWidth, true);
	}

	int32 h = countHeight();
	if (h != height()) resize(width(), h);
	update();

	_lastMousePos = QCursor::pos();
	updateSelected();

	return added;
}

int GifsListWidget::validateExistingInlineRows(const InlineResults &results) {
	int count = results.size(), until = 0, untilrow = 0, untilcol = 0;
	for (; until < count;) {
		if (untilrow >= _inlineRows.size() || _inlineRows[untilrow].items[untilcol]->getResult() != results[until].get()) {
			break;
		}
		++until;
		if (++untilcol == _inlineRows[untilrow].items.size()) {
			++untilrow;
			untilcol = 0;
		}
	}
	if (until == count) { // all items are layed out
		if (untilrow == _inlineRows.size()) { // nothing changed
			return until;
		}

		for (int i = untilrow, l = _inlineRows.size(), skip = untilcol; i < l; ++i) {
			for (int j = 0, s = _inlineRows[i].items.size(); j < s; ++j) {
				if (skip) {
					--skip;
				} else {
					_inlineRows[i].items[j]->setPosition(-1);
				}
			}
		}
		if (!untilcol) { // all good rows are filled
			_inlineRows.resize(untilrow);
			return until;
		}
		_inlineRows.resize(untilrow + 1);
		_inlineRows[untilrow].items.resize(untilcol);
		_inlineRows[untilrow] = layoutInlineRow(_inlineRows[untilrow]);
		return until;
	}
	if (untilrow && !untilcol) { // remove last row, maybe it is not full
		--untilrow;
		untilcol = _inlineRows[untilrow].items.size();
	}
	until -= untilcol;

	for (int i = untilrow, l = _inlineRows.size(); i < l; ++i) {
		for (int j = 0, s = _inlineRows[i].items.size(); j < s; ++j) {
			_inlineRows[i].items[j]->setPosition(-1);
		}
	}
	_inlineRows.resize(untilrow);

	if (_inlineRows.isEmpty()) {
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

void GifsListWidget::inlineItemLayoutChanged(const InlineItem *layout) {
	if (_selected < 0 || !isVisible()) {
		return;
	}

	int row = _selected / MatrixRowShift, col = _selected % MatrixRowShift;
	if (row < _inlineRows.size() && col < _inlineRows.at(row).items.size()) {
		if (layout == _inlineRows.at(row).items.at(col)) {
			updateSelected();
		}
	}
}

void GifsListWidget::inlineItemRepaint(const InlineItem *layout) {
	auto ms = getms();
	if (_lastScrolled + 100 <= ms) {
		update();
	} else {
		_updateInlineItems.start(_lastScrolled + 100 - ms);
	}
}

bool GifsListWidget::inlineItemVisible(const InlineItem *layout) {
	auto position = layout->position();
	if (position < 0 || !isVisible()) {
		return false;
	}

	auto row = position / MatrixRowShift;
	auto col = position % MatrixRowShift;
	t_assert((row < _inlineRows.size()) && (col < _inlineRows[row].items.size()));

	auto &inlineItems = _inlineRows[row].items;
	auto top = 0;
	for (auto i = 0; i != row; ++i) {
		top += _inlineRows[i].height;
	}

	return (top < getVisibleBottom()) && (top + _inlineRows[row].items[col]->height() > getVisibleTop());
}

void GifsListWidget::refreshRecent() {
	if (_section == Section::Gifs) {
		refreshSavedGifs();
	}
}

void GifsListWidget::updateSelected() {
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
	HistoryCursorState cursor = HistoryDefaultCursorState;
	if (sy >= 0) {
		row = 0;
		for (int rows = _inlineRows.size(); row < rows; ++row) {
			if (sy < _inlineRows.at(row).height) {
				break;
			}
			sy -= _inlineRows.at(row).height;
		}
	}
	if (sx >= 0 && row >= 0 && row < _inlineRows.size()) {
		auto &inlineItems = _inlineRows[row].items;
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
			inlineItems.at(col)->getState(lnk, cursor, sx, sy);
			lnkhost = inlineItems.at(col);
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
			t_assert(srow >= 0 && srow < _inlineRows.size() && scol >= 0 && scol < _inlineRows.at(srow).items.size());
			_inlineRows[srow].items[scol]->update();
		}
		_selected = sel;
		if (row >= 0 && col >= 0) {
			t_assert(row >= 0 && row < _inlineRows.size() && col >= 0 && col < _inlineRows.at(row).items.size());
			_inlineRows[row].items[col]->update();
		}
		if (_previewShown && _selected >= 0 && _pressed != _selected) {
			_pressed = _selected;
			if (row >= 0 && col >= 0) {
				auto layout = _inlineRows.at(row).items.at(col);
				if (auto previewDocument = layout->getPreviewDocument()) {
					Ui::showMediaPreview(previewDocument);
				} else if (auto previewPhoto = layout->getPreviewPhoto()) {
					Ui::showMediaPreview(previewPhoto);
				}
			}
		}
	}
	if (ClickHandler::setActive(lnk, lnkhost)) {
		setCursor(lnk ? style::cur_pointer : style::cur_default);
	}
}

void GifsListWidget::onPreview() {
	if (_pressed < 0) return;
	int row = _pressed / MatrixRowShift, col = _pressed % MatrixRowShift;
	if (row < _inlineRows.size() && col < _inlineRows.at(row).items.size()) {
		auto layout = _inlineRows.at(row).items.at(col);
		if (auto previewDocument = layout->getPreviewDocument()) {
			Ui::showMediaPreview(previewDocument);
			_previewShown = true;
		} else if (auto previewPhoto = layout->getPreviewPhoto()) {
			Ui::showMediaPreview(previewPhoto);
			_previewShown = true;
		}
	}
}

void GifsListWidget::onUpdateInlineItems() {
	auto ms = getms();
	if (_lastScrolled + 100 <= ms) {
		update();
	} else {
		_updateInlineItems.start(_lastScrolled + 100 - ms);
	}
}

void GifsListWidget::onSwitchPm() {
	if (_inlineBot && _inlineBot->botInfo) {
		_inlineBot->botInfo->startToken = _switchPmStartToken;
		Ui::showPeerHistory(_inlineBot, ShowAndStartBotMsgId);
	}
}

} // namespace ChatHelpers
