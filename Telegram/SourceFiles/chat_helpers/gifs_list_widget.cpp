/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "chat_helpers/gifs_list_widget.h"

#include "data/data_photo.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "styles/style_chat_helpers.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"
#include "ui/effects/ripple_animation.h"
#include "boxes/stickers_box.h"
#include "inline_bots/inline_bot_result.h"
#include "chat_helpers/stickers.h"
#include "storage/localstorage.h"
#include "lang/lang_keys.h"
#include "mainwindow.h"
#include "window/window_controller.h"
#include "history/view/history_view_cursor_state.h"

namespace ChatHelpers {
namespace {

constexpr auto kSaveChosenTabTimeout = 1000;
constexpr auto kSearchRequestDelay = 400;
constexpr auto kInlineItemsMaxPerRow = 5;
constexpr auto kSearchBotUsername = str_const("gif");

} // namespace

class GifsListWidget::Footer : public TabbedSelector::InnerFooter {
public:
	Footer(not_null<GifsListWidget*> parent);

	void stealFocus();
	void returnFocus();
	void setLoading(bool loading) {
		_cancel->setLoadingAnimation(loading);
	}

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

	void processPanelHideFinished() override;

private:
	not_null<GifsListWidget*> _pan;

	object_ptr<Ui::InputField> _field;
	object_ptr<Ui::CrossButton> _cancel;

	QPointer<QWidget> _focusTakenFrom;

};

GifsListWidget::Footer::Footer(not_null<GifsListWidget*> parent) : InnerFooter(parent)
, _pan(parent)
, _field(this, st::gifsSearchField, langFactory(lng_gifs_search))
, _cancel(this, st::gifsSearchCancel) {
	connect(_field, &Ui::InputField::submitted, [=] {
		_pan->sendInlineRequest();
	});
	connect(_field, &Ui::InputField::cancelled, [=] {
		if (_field->getLastText().isEmpty()) {
			emit _pan->cancelled();
		} else {
			_field->setText(QString());
		}
	});
	connect(_field, &Ui::InputField::changed, [=] {
		_cancel->toggle(
			!_field->getLastText().isEmpty(),
			anim::type::normal);
		_pan->searchForGifs(_field->getLastText());
	});
	_cancel->setClickedCallback([=] {
		_field->setText(QString());
	});
}

void GifsListWidget::Footer::stealFocus() {
	if (!_focusTakenFrom) {
		_focusTakenFrom = QApplication::focusWidget();
	}
	_field->setFocus();
}

void GifsListWidget::Footer::returnFocus() {
	if (_focusTakenFrom) {
		if (_field->hasFocus()) {
			_focusTakenFrom->setFocus();
		}
		_focusTakenFrom = nullptr;
	}
}

void GifsListWidget::Footer::paintEvent(QPaintEvent *e) {
	Painter p(this);
	st::gifsSearchIcon.paint(p, st::gifsSearchIconPosition.x(), st::gifsSearchIconPosition.y(), width());
}

void GifsListWidget::Footer::resizeEvent(QResizeEvent *e) {
	auto fieldWidth = width()
		- st::gifsSearchFieldPosition.x()
		- st::gifsSearchCancelPosition.x()
		- st::gifsSearchCancel.width;
	_field->resizeToWidth(fieldWidth);
	_field->moveToLeft(st::gifsSearchFieldPosition.x(), st::gifsSearchFieldPosition.y());
	_cancel->moveToRight(st::gifsSearchCancelPosition.x(), st::gifsSearchCancelPosition.y());
}

void GifsListWidget::Footer::processPanelHideFinished() {
	// Preserve panel state through visibility toggles.
	//_field->setText(QString());
}

GifsListWidget::GifsListWidget(
	QWidget *parent,
	not_null<Window::Controller*> controller)
: Inner(parent, controller)
, _section(Section::Gifs) {
	setMouseTracking(true);
	setAttribute(Qt::WA_OpaquePaintEvent);

	_previewTimer.setSingleShot(true);
	connect(&_previewTimer, SIGNAL(timeout()), this, SLOT(onPreview()));

	_updateInlineItems.setSingleShot(true);
	connect(&_updateInlineItems, SIGNAL(timeout()), this, SLOT(onUpdateInlineItems()));

	_inlineRequestTimer.setSingleShot(true);
	connect(&_inlineRequestTimer, &QTimer::timeout, this, [this] { sendInlineRequest(); });

	Auth().data().savedGifsUpdated(
	) | rpl::start_with_next([this] {
		refreshSavedGifs();
	}, lifetime());
	subscribe(Auth().downloaderTaskFinished(), [this] {
		update();
	});
	subscribe(controller->gifPauseLevelChanged(), [this] {
		if (!this->controller()->isGifPausedAtLeastFor(Window::GifPauseReason::SavedGifs)) {
			update();
		}
	});
}

object_ptr<TabbedSelector::InnerFooter> GifsListWidget::createFooter() {
	Expects(_footer == nullptr);
	auto result = object_ptr<Footer>(this);
	_footer = result;
	return std::move(result);
}

void GifsListWidget::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	auto top = getVisibleTop();
	Inner::visibleTopBottomUpdated(visibleTop, visibleBottom);
	if (top != getVisibleTop()) {
		_lastScrolled = getms();
	}
	checkLoadMore();
}

void GifsListWidget::checkLoadMore() {
	auto visibleHeight = (getVisibleBottom() - getVisibleTop());
	if (getVisibleBottom() + visibleHeight > height()) {
		sendInlineRequest();
	}
}

int GifsListWidget::countDesiredHeight(int newWidth) {
	auto result = st::stickerPanPadding;
	for (int i = 0, l = _rows.count(); i < l; ++i) {
		layoutInlineRow(_rows[i], newWidth);
		result += _rows[i].height;
	}
	return result + st::stickerPanPadding;
}

GifsListWidget::~GifsListWidget() {
	clearInlineRows(true);
	deleteUnusedGifLayouts();
	deleteUnusedInlineLayouts();
}

void GifsListWidget::cancelGifsSearch() {
	_footer->setLoading(false);
	if (_inlineRequestId) {
		request(_inlineRequestId).cancel();
		_inlineRequestId = 0;
	}
	_inlineRequestTimer.stop();
	_inlineQuery = _inlineNextQuery = _inlineNextOffset = QString();
	_inlineCache.clear();
	refreshInlineRows(nullptr, true);
}

void GifsListWidget::inlineResultsDone(const MTPmessages_BotResults &result) {
	_footer->setLoading(false);
	_inlineRequestId = 0;

	auto it = _inlineCache.find(_inlineQuery);
	auto adding = (it != _inlineCache.cend());
	if (result.type() == mtpc_messages_botResults) {
		auto &d = result.c_messages_botResults();
		App::feedUsers(d.vusers);

		auto &v = d.vresults.v;
		auto queryId = d.vquery_id.v;

		if (it == _inlineCache.cend()) {
			it = _inlineCache.emplace(_inlineQuery, std::make_unique<InlineCacheEntry>()).first;
		}
		auto entry = it->second.get();
		entry->nextOffset = qs(d.vnext_offset);
		if (auto count = v.size()) {
			entry->results.reserve(entry->results.size() + count);
		}
		auto added = 0;
		for_const (const auto &res, v) {
			if (auto result = InlineBots::Result::create(queryId, res)) {
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
	checkLoadMore();
}

void GifsListWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);
	auto clip = e->rect();
	p.fillRect(clip, st::emojiPanBg);

	paintInlineItems(p, clip);
}

void GifsListWidget::paintInlineItems(Painter &p, QRect clip) {
	if (_rows.isEmpty()) {
		p.setFont(st::normalFont);
		p.setPen(st::noContactsColor);
		auto text = lang(_inlineQuery.isEmpty() ? lng_gifs_no_saved : lng_inline_bot_no_results);
		p.drawText(QRect(0, 0, width(), (height() / 3) * 2 + st::normalFont->height), text, style::al_center);
		return;
	}
	auto gifPaused = controller()->isGifPausedAtLeastFor(Window::GifPauseReason::SavedGifs);
	InlineBots::Layout::PaintContext context(getms(), false, gifPaused, false);

	auto top = st::stickerPanPadding;
	auto fromx = rtl() ? (width() - clip.x() - clip.width()) : clip.x();
	auto tox = rtl() ? (width() - clip.x()) : (clip.x() + clip.width());
	for (auto row = 0, rows = _rows.size(); row != rows; ++row) {
		auto &inlineRow = _rows[row];
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

	if (dynamic_cast<InlineBots::Layout::SendClickHandler*>(activated.get())) {
		int row = _selected / MatrixRowShift, column = _selected % MatrixRowShift;
		selectInlineResult(row, column);
	} else {
		App::activateClickHandler(activated, e->button());
	}
}

void GifsListWidget::selectInlineResult(int row, int column) {
	if (row >= _rows.size() || column >= _rows[row].items.size()) {
		return;
	}

	auto item = _rows[row].items[column];
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
			emit selected(inlineResult, _searchBot);
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
		Assert(srow >= 0 && srow < _rows.size() && scol >= 0 && scol < _rows[srow].items.size());
		ClickHandler::clearActive(_rows[srow].items[scol]);
		setCursor(style::cur_default);
	}
	_selected = _pressed = -1;
	update();
}

TabbedSelector::InnerFooter *GifsListWidget::getFooter() const {
	return _footer;
}

void GifsListWidget::processHideFinished() {
	clearSelection();
}

void GifsListWidget::processPanelHideFinished() {
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
	// Preserve panel state through visibility toggles.
	//clearInlineRows(false);
	for_const (auto &item, _gifLayouts) {
		itemForget(item.second);
	}
	for_const (auto &item, _inlineLayouts) {
		itemForget(item.second);
	}
}

bool GifsListWidget::inlineRowsAddItem(DocumentData *savedGif, InlineResult *result, Row &row, int32 &sumWidth) {
	LayoutItem *layout = nullptr;
	if (savedGif) {
		layout = layoutPrepareSavedGif(savedGif, (_rows.size() * MatrixRowShift) + row.items.size());
	} else if (result) {
		layout = layoutPrepareInlineResult(result, (_rows.size() * MatrixRowShift) + row.items.size());
	}
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

bool GifsListWidget::inlineRowFinalize(Row &row, int32 &sumWidth, bool force) {
	if (row.items.isEmpty()) return false;

	auto full = (row.items.size() >= kInlineItemsMaxPerRow);

	// Currently use the same GIFs layout for all widget sizes.
//	auto big = (sumWidth >= st::buttonRadius + width() - st::inlineResultsLeft);
	auto big = (sumWidth >= st::emojiPanWidth - st::inlineResultsLeft);
	if (full || big || force) {
		row.maxWidth = (full || big) ? sumWidth : 0;
		layoutInlineRow(
			row,
			width());
		_rows.push_back(row);
		row = Row();
		row.items.reserve(kInlineItemsMaxPerRow);
		sumWidth = 0;
		return true;
	}
	return false;
}

void GifsListWidget::refreshSavedGifs() {
	if (_section == Section::Gifs) {
		clearInlineRows(false);

		auto &saved = Auth().data().savedGifs();
		if (!saved.isEmpty()) {
			_rows.reserve(saved.size());
			auto row = Row();
			row.items.reserve(kInlineItemsMaxPerRow);
			auto sumWidth = 0;
			for_const (auto &gif, saved) {
				inlineRowsAddItem(gif, 0, row, sumWidth);
			}
			inlineRowFinalize(row, sumWidth, true);
		}
		deleteUnusedGifLayouts();

		resizeToWidth(width());
		update();
	}

	if (isVisible()) {
		updateSelected();
	} else {
		preloadImages();
	}
}

void GifsListWidget::clearInlineRows(bool resultsDeleted) {
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

GifsListWidget::LayoutItem *GifsListWidget::layoutPrepareSavedGif(DocumentData *doc, int32 position) {
	auto it = _gifLayouts.find(doc);
	if (it == _gifLayouts.cend()) {
		if (auto layout = LayoutItem::createLayoutGif(this, doc)) {
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

GifsListWidget::LayoutItem *GifsListWidget::layoutPrepareInlineResult(InlineResult *result, int32 position) {
	auto it = _inlineLayouts.find(result);
	if (it == _inlineLayouts.cend()) {
		if (auto layout = LayoutItem::createLayout(this, result, _inlineWithThumb)) {
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
	if (_rows.isEmpty() || _section != Section::Gifs) { // delete all
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
	if (_rows.isEmpty() || _section == Section::Gifs) { // delete all
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

void GifsListWidget::layoutInlineRow(Row &row, int fullWidth) {
	auto count = int(row.items.size());
	Assert(count <= kInlineItemsMaxPerRow);

	// enumerate items in the order of growing maxWidth()
	// for that sort item indices by maxWidth()
	int indices[kInlineItemsMaxPerRow];
	for (auto i = 0; i != count; ++i) {
		indices[i] = i;
	}
	std::sort(indices, indices + count, [&](int a, int b) {
		return row.items[a]->maxWidth()
			< row.items[b]->maxWidth();
	});

	auto desiredWidth = row.maxWidth;
	row.height = 0;
	int availw = fullWidth - (st::inlineResultsLeft - st::buttonRadius);
	for (int i = 0; i < count; ++i) {
		const auto index = indices[i];
		const auto &item = row.items[index];
		const auto w = desiredWidth
			? (item->maxWidth() * availw / desiredWidth)
			: item->maxWidth();
		auto actualw = qMax(w, st::inlineResultsMinWidth);
		row.height = qMax(row.height, item->resizeGetHeight(actualw));
		if (desiredWidth) {
			availw -= actualw;
			desiredWidth -= row.items[index]->maxWidth();
			if (index > 0 && row.items[index - 1]->hasRightSkip()) {
				availw -= st::inlineResultsSkip;
				desiredWidth -= st::inlineResultsSkip;
			}
		}
	}
}

void GifsListWidget::preloadImages() {
	for (auto row = 0, rows = _rows.size(); row != rows; ++row) {
		for (auto col = 0, cols = _rows[row].items.size(); col != cols; ++col) {
			_rows[row].items[col]->preload();
		}
	}
}

void GifsListWidget::switchToSavedGifs() {
	clearInlineRows(false);
	_section = Section::Gifs;
	refreshSavedGifs();
	emit scrollToY(0);
	emit scrollUpdated();
}

int GifsListWidget::refreshInlineRows(const InlineCacheEntry *entry, bool resultsDeleted) {
	if (!entry) {
		if (resultsDeleted) {
			clearInlineRows(true);
			deleteUnusedInlineLayouts();
		}
		switchToSavedGifs();
		return 0;
	}

	clearSelection();

	_section = Section::Inlines;
	auto count = int(entry->results.size());
	auto from = validateExistingInlineRows(entry->results);
	auto added = 0;
	if (count) {
		_rows.reserve(count);
		auto row = Row();
		row.items.reserve(kInlineItemsMaxPerRow);
		auto sumWidth = 0;
		for (auto i = from; i != count; ++i) {
			if (inlineRowsAddItem(0, entry->results[i].get(), row, sumWidth)) {
				++added;
			}
		}
		inlineRowFinalize(row, sumWidth, true);
	}

	resizeToWidth(width());
	update();

	_lastMousePos = QCursor::pos();
	updateSelected();

	return added;
}

int GifsListWidget::validateExistingInlineRows(const InlineResults &results) {
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
		_rows[untilrow].maxWidth = std::accumulate(
			_rows[untilrow].items.begin(),
			_rows[untilrow].items.end(),
			0,
			[](int w, auto &row) { return w + row->maxWidth(); });
		layoutInlineRow(_rows[untilrow], width());
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

void GifsListWidget::inlineItemLayoutChanged(const InlineBots::Layout::ItemBase *layout) {
	if (_selected < 0 || !isVisible()) {
		return;
	}

	int row = _selected / MatrixRowShift, col = _selected % MatrixRowShift;
	if (row < _rows.size() && col < _rows[row].items.size()) {
		if (layout == _rows[row].items[col]) {
			updateSelected();
		}
	}
}

void GifsListWidget::inlineItemRepaint(const InlineBots::Layout::ItemBase *layout) {
	auto ms = getms();
	if (_lastScrolled + 100 <= ms) {
		update();
	} else {
		_updateInlineItems.start(_lastScrolled + 100 - ms);
	}
}

bool GifsListWidget::inlineItemVisible(const InlineBots::Layout::ItemBase *layout) {
	auto position = layout->position();
	if (position < 0 || !isVisible()) {
		return false;
	}

	auto row = position / MatrixRowShift;
	auto col = position % MatrixRowShift;
	Assert((row < _rows.size()) && (col < _rows[row].items.size()));

	auto &inlineItems = _rows[row].items;
	auto top = 0;
	for (auto i = 0; i != row; ++i) {
		top += _rows[i].height;
	}

	return (top < getVisibleBottom()) && (top + _rows[row].items[col]->height() > getVisibleTop());
}

void GifsListWidget::afterShown() {
	if (_footer) {
		_footer->stealFocus();
	}
}

void GifsListWidget::beforeHiding() {
	if (_footer) {
		_footer->returnFocus();
	}
}

bool GifsListWidget::refreshInlineRows(int32 *added) {
	auto it = _inlineCache.find(_inlineQuery);
	const InlineCacheEntry *entry = nullptr;
	if (it != _inlineCache.cend()) {
		entry = it->second.get();
		_inlineNextOffset = it->second->nextOffset;
	}
	auto result = refreshInlineRows(entry, false);
	if (added) *added = result;
	return (entry != nullptr);
}

int32 GifsListWidget::showInlineRows(bool newResults) {
	auto added = 0;
	auto clear = !refreshInlineRows(&added);
	if (newResults) {
		scrollToY(0);
	}
	return added;
}

void GifsListWidget::searchForGifs(const QString &query) {
	if (query.isEmpty()) {
		cancelGifsSearch();
		return;
	}

	if (_inlineQuery != query) {
		_footer->setLoading(false);
		if (_inlineRequestId) {
			request(_inlineRequestId).cancel();
			_inlineRequestId = 0;
		}
		if (_inlineCache.find(query) != _inlineCache.cend()) {
			_inlineRequestTimer.stop();
			_inlineQuery = _inlineNextQuery = query;
			showInlineRows(true);
		} else {
			_inlineNextQuery = query;
			_inlineRequestTimer.start(kSearchRequestDelay);
		}
	}

	if (!_searchBot && !_searchBotRequestId) {
		auto username = str_const_toString(kSearchBotUsername);
		_searchBotRequestId = request(MTPcontacts_ResolveUsername(MTP_string(username))).done([this](const MTPcontacts_ResolvedPeer &result) {
			Expects(result.type() == mtpc_contacts_resolvedPeer);
			auto &data = result.c_contacts_resolvedPeer();
			App::feedUsers(data.vusers);
			App::feedChats(data.vchats);
			if (auto peer = App::peerLoaded(peerFromMTP(data.vpeer))) {
				if (auto user = peer->asUser()) {
					_searchBot = user;
				}
			}
		}).send();
	}
}

void GifsListWidget::sendInlineRequest() {
	if (_inlineRequestId || !_inlineQueryPeer || _inlineNextQuery.isEmpty()) {
		return;
	}

	if (!_searchBot) {
		// Wait for the bot being resolved.
		_footer->setLoading(true);
		_inlineRequestTimer.start(kSearchRequestDelay);
		return;
	}
	_inlineRequestTimer.stop();
	_inlineQuery = _inlineNextQuery;

	auto nextOffset = QString();
	auto it = _inlineCache.find(_inlineQuery);
	if (it != _inlineCache.cend()) {
		nextOffset = it->second->nextOffset;
		if (nextOffset.isEmpty()) {
			_footer->setLoading(false);
			return;
		}
	}

	_footer->setLoading(true);
	_inlineRequestId = request(MTPmessages_GetInlineBotResults(MTP_flags(0), _searchBot->inputUser, _inlineQueryPeer->input, MTPInputGeoPoint(), MTP_string(_inlineQuery), MTP_string(nextOffset))).done([this](const MTPmessages_BotResults &result, mtpRequestId requestId) {
		inlineResultsDone(result);
	}).fail([this](const RPCError &error) {
		// show error?
		_footer->setLoading(false);
		_inlineRequestId = 0;
	}).handleAllErrors().send();
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
			int width = inlineItems[col]->width();
			if (sx < width) {
				break;
			}
			sx -= width;
			if (inlineItems[col]->hasRightSkip()) {
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
			Assert(srow >= 0 && srow < _rows.size() && scol >= 0 && scol < _rows[srow].items.size());
			_rows[srow].items[scol]->update();
		}
		_selected = sel;
		if (row >= 0 && col >= 0) {
			Assert(row >= 0 && row < _rows.size() && col >= 0 && col < _rows[row].items.size());
			_rows[row].items[col]->update();
		}
		if (_previewShown && _selected >= 0 && _pressed != _selected) {
			_pressed = _selected;
			if (row >= 0 && col >= 0) {
				auto layout = _rows[row].items[col];
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
	if (row < _rows.size() && col < _rows[row].items.size()) {
		auto layout = _rows[row].items[col];
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

} // namespace ChatHelpers
