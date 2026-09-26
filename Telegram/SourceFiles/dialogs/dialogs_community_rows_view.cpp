/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/dialogs_community_rows_view.h"

#include "data/data_forum.h"
#include "dialogs/dialogs_row.h"
#include "history/history.h"

namespace Dialogs {

CommunityRowsView::CommunityRowsView() = default;

CommunityRowsView::~CommunityRowsView() = default;

void CommunityRowsView::setRepaint(Fn<void()> repaint) {
	_repaint = std::move(repaint);
}

void CommunityRowsView::clear() {
	_rows.clear();
	_tops.clear();
	_forumsLifetime.destroy();
}

void CommunityRowsView::add(not_null<History*> history, float64 narrowRatio) {
	if (const auto forum = history->peer->forum()) {
		forum->preloadTopics();
		if (_repaint) {
			forum->chatsListChanges(
			) | rpl::on_next(_repaint, _forumsLifetime);
		}
	}
	auto row = std::make_unique<Row>(Key(history), 0, 0);
	row->recountHeight(narrowRatio, FilterId());
	_rows.push_back(std::move(row));
}

void CommunityRowsView::finalize() {
	_tops.resize(_rows.size() + 1);
	auto top = 0;
	for (auto i = 0, count = int(_rows.size()); i != count; ++i) {
		_tops[i] = top;
		top += _rows[i]->height();
	}
	_tops.back() = top;
}

void CommunityRowsView::recountHeights(float64 narrowRatio) {
	for (const auto &row : _rows) {
		row->recountHeight(narrowRatio, FilterId());
	}
	finalize();
}

int CommunityRowsView::height() const {
	return _tops.empty() ? 0 : _tops.back();
}

int CommunityRowsView::indexByY(int y) const {
	if (_tops.empty() || y < 0 || y >= _tops.back()) {
		return -1;
	}
	const auto i = std::upper_bound(begin(_tops), end(_tops), y);
	return int(i - begin(_tops)) - 1;
}

int CommunityRowsView::rowTop(int index) const {
	Expects(index >= 0 && index < int(_rows.size()));

	return _tops[index];
}

Row *CommunityRowsView::rowAt(int index) const {
	return (index >= 0 && index < int(_rows.size()))
		? _rows[index].get()
		: nullptr;
}

bool CommunityRowsView::contains(not_null<History*> history) const {
	for (const auto &row : _rows) {
		if (row->history() == history) {
			return true;
		}
	}
	return false;
}

void CommunityRowsView::paint(
		Painter &p,
		QRect clip,
		Fn<void(not_null<Row*>, int index, int top)> paintRow) const {
	if (_rows.empty() || clip.top() >= height()) {
		return;
	}
	const auto from = std::max(indexByY(clip.top()), 0);
	const auto bottom = clip.top() + clip.height();
	for (auto i = from, count = int(_rows.size()); i != count; ++i) {
		const auto top = _tops[i];
		if (top >= bottom) {
			break;
		}
		paintRow(_rows[i].get(), i, top);
	}
}

} // namespace Dialogs
