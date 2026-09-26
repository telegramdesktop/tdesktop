/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class History;
class Painter;

namespace Dialogs {

class Row;

class CommunityRowsView final {
public:
	CommunityRowsView();
	~CommunityRowsView();

	void setRepaint(Fn<void()> repaint);

	void clear();
	void add(not_null<History*> history, float64 narrowRatio);
	void finalize();

	void recountHeights(float64 narrowRatio);

	[[nodiscard]] bool empty() const {
		return _rows.empty();
	}
	[[nodiscard]] int size() const {
		return int(_rows.size());
	}
	[[nodiscard]] int height() const;
	[[nodiscard]] int indexByY(int y) const;
	[[nodiscard]] int rowTop(int index) const;
	[[nodiscard]] Row *rowAt(int index) const;
	[[nodiscard]] bool contains(not_null<History*> history) const;

	void paint(
		Painter &p,
		QRect clip,
		Fn<void(not_null<Row*>, int index, int top)> paintRow) const;

private:
	Fn<void()> _repaint;
	std::vector<std::unique_ptr<Row>> _rows;
	std::vector<int> _tops;
	rpl::lifetime _forumsLifetime;

};

} // namespace Dialogs
