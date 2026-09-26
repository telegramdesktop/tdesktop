/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class Painter;

// Alphabetical section headers ("A", "B", ..., "#") shown above the first
// row of every letter group. Pure layout/geometry helper: it owns the
// per-row prefix counts and answers position / hit-test queries in
// coordinates relative to the top of the rows region.
class PeerListSectionHeaders final {
public:
	void rebuild(int count, int rowHeight, Fn<QString(int index)> section);
	void clear();

	[[nodiscard]] bool empty() const {
		return !_count;
	}
	[[nodiscard]] int total() const {
		return _total;
	}

	// Whether a header is drawn above the row with the given shown index.
	[[nodiscard]] bool hasHeader(int index) const;

	// Top of the row content, relative to the top of the rows region.
	[[nodiscard]] int contentTop(int index) const;

	// Full height of the rows region including all headers.
	[[nodiscard]] int fullHeight() const;

	// Shown row index at the given y (relative to the rows region top), or
	// -1 when y falls into a header band or outside any row content.
	[[nodiscard]] int rowFromY(int y) const;

	void paint(Painter &p, int outerWidth, const QString &text) const;

private:
	std::vector<int> _prefix;
	int _total = 0;
	int _count = 0;
	int _rowHeight = 0;

};
