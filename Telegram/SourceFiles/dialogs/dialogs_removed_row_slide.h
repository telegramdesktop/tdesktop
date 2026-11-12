/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animations.h"

namespace Dialogs {

class Entry;
class IndexedList;

class RemovedRowSlide final {
public:
	explicit RemovedRowSlide(Fn<void()> update);

	void block(not_null<Entry*> entry);
	void prepare(
		not_null<Entry*> entry,
		not_null<const IndexedList*> list,
		int top,
		QImage image);
	void start(not_null<Entry*> entry);
	void clearPending();

	[[nodiscard]] int top() const;
	[[nodiscard]] int height(not_null<const IndexedList*> list) const;
	void paint(QPainter &p, int width) const;

private:
	struct Pending {
		Entry *entry = nullptr;
		const IndexedList *list = nullptr;
		int top = 0;
		QImage image;
	};

	Fn<void()> _update;
	Ui::Animations::Simple _animation;
	Entry *_blocked = nullptr;
	Pending _pending;
	Pending _shown;

};

} // namespace Dialogs
