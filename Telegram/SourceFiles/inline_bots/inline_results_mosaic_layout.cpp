/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "inline_bots/inline_results_mosaic_layout.h"

#include "styles/style_chat_helpers.h"

namespace InlineBots {
namespace Layout {
namespace {

constexpr auto kInlineItemsMaxPerRow = 5;

} // namespace

void MosaicLayout::setFullWidth(int w) {
	_width = w;
}

bool MosaicLayout::empty() const {
	return _rows.empty();
}

int MosaicLayout::rowsCount() const {
	return _rows.size();
}

int MosaicLayout::countDesiredHeight(int newWidth) {
	auto result = 0;
	for (int i = 0, l = _rows.size(); i < l; ++i) {
		layoutRow(_rows[i], newWidth);
		result += _rows[i].height;
	}
	return result;
}

not_null<ItemBase*> MosaicLayout::itemAt(int row, int column) const {
	Expects((row >= 0)
		&& (row < _rows.size())
		&& (column >= 0)
		&& (column < _rows[row].items.size()));
	return _rows[row].items[column];
}

ItemBase *MosaicLayout::maybeItemAt(int row, int column) const {
	if ((row >= 0)
		&& (row < _rows.size())
		&& (column >= 0)
		&& (column < _rows[row].items.size())) {
		return _rows[row].items[column];
	}
	return nullptr;
}

void MosaicLayout::addItems(const std::vector<ItemBase*> &items) {
	_rows.reserve(items.size());
	auto row = Row();
	row.items.reserve(kInlineItemsMaxPerRow);
	auto sumWidth = 0;
	for (const auto &item : items) {
		addItem(item, row, sumWidth);
	}
	rowFinalize(row, sumWidth, true);
}

void MosaicLayout::addItem(ItemBase *item, Row &row, int32 &sumWidth) {
	item->preload();

	item->setPosition(_rows.size() * MatrixRowShift + row.items.size());
	if (rowFinalize(row, sumWidth, item->isFullLine())) {
		item->setPosition(_rows.size() * MatrixRowShift);
	}

	sumWidth += item->maxWidth();
	if (!row.items.empty() && row.items.back()->hasRightSkip()) {
		sumWidth += st::inlineResultsSkip;
	}

	row.items.push_back(item);
}

bool MosaicLayout::rowFinalize(Row &row, int32 &sumWidth, bool force) {
	if (row.items.empty()) {
		return false;
	}

	auto full = (row.items.size() >= kInlineItemsMaxPerRow);

	// Currently use the same GIFs layout for all widget sizes.
//	auto big = (sumWidth >= st::roundRadiusSmall + width() - st::inlineResultsLeft);
	auto big = (sumWidth >= st::emojiPanWidth - st::inlineResultsLeft);
	if (full || big || force) {
		row.maxWidth = (full || big) ? sumWidth : 0;
		layoutRow(
			row,
			_width);
		_rows.push_back(row);
		row = Row();
		row.items.reserve(kInlineItemsMaxPerRow);
		sumWidth = 0;
		return true;
	}
	return false;
}

void MosaicLayout::layoutRow(Row &row, int fullWidth) {
	auto count = int(row.items.size());
	Assert(count <= kInlineItemsMaxPerRow);

	// enumerate items in the order of growing maxWidth()
	// for that sort item indices by maxWidth()
	int indices[kInlineItemsMaxPerRow];
	for (auto i = 0; i != count; ++i) {
		indices[i] = i;
	}
	std::sort(indices, indices + count, [&](int a, int b) {
		return row.items[a]->maxWidth() < row.items[b]->maxWidth();
	});

	auto desiredWidth = row.maxWidth;
	row.height = 0;
	int availw = fullWidth - (st::inlineResultsLeft - st::roundRadiusSmall);
	for (int i = 0; i < count; ++i) {
		const auto index = indices[i];
		const auto &item = row.items[index];
		const auto w = desiredWidth
			? (item->maxWidth() * availw / desiredWidth)
			: item->maxWidth();
		auto actualw = std::max(w, st::inlineResultsMinWidth);
		row.height = std::max(row.height, item->resizeGetHeight(actualw));
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

void MosaicLayout::paint(
		Painter &p,
		int top,
		int startLeft,
		const QRect &clip,
		PaintContext &context) {
	const auto fromx = rtl() ? (_width - clip.x() - clip.width()) : clip.x();
	const auto tox = rtl() ? (_width - clip.x()) : (clip.x() + clip.width());
	const auto rows = _rows.size();
	for (auto row = 0; row != rows; ++row) {
		auto &inlineRow = _rows[row];
		if (top >= clip.top() + clip.height()) {
			break;
		}
		if (top + inlineRow.height > clip.top()) {
			auto left = startLeft;
			if (row == rows - 1) {
				context.lastRow = true;
			}
			for (int col = 0, cols = inlineRow.items.size(); col < cols; ++col) {
				if (left >= tox) {
					break;
				}

				const auto &item = inlineRow.items[col];
				const auto w = item->width();
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

void MosaicLayout::clearRows(bool resultsDeleted) {
	if (resultsDeleted) {
		// _selected = _pressed = -1;
	} else {
		// clearSelection();
		for (const auto &row : _rows) {
			for (const auto &item : row.items) {
				item->setPosition(-1);
			}
		}
	}
	_rows.clear();
}

void MosaicLayout::preloadImages() {
	for (const auto &row : _rows) {
		for (const auto &item : row.items) {
			item->preload();
		}
	}
}

int MosaicLayout::validateExistingRows(const Results &results) {
	int count = results.size(), until = 0, untilrow = 0, untilcol = 0;
	while (until < count) {
		if (untilrow >= _rows.size()
			|| _rows[untilrow].items[untilcol]->getResult() != results[until].get()) {
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
		layoutRow(_rows[untilrow], _width);
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

	// if (_rows.isEmpty()) {
	// 	_inlineWithThumb = false;
	// 	for (int i = until; i < count; ++i) {
	// 		if (results.at(i)->hasThumbDisplay()) {
	// 			_inlineWithThumb = true;
	// 			break;
	// 		}
	// 	}
	// }
	return until;
}

} // namespace Layout
} // namespace InlineBots
