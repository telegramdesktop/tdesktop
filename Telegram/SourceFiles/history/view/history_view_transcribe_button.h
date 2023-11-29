/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animations.h"

namespace Ui {
struct ChatPaintContext;
class InfiniteRadialAnimation;
class RippleAnimation;
} // namespace Ui

namespace HistoryView {

using PaintContext = Ui::ChatPaintContext;

class TranscribeButton final {
public:
	explicit TranscribeButton(not_null<HistoryItem*> item, bool roundview);
	~TranscribeButton();

	[[nodiscard]] QSize size() const;

	void setOpened(bool opened, Fn<void()> update);
	void setLoading(bool loading, Fn<void()> update);
	void paint(QPainter &p, int x, int y, const PaintContext &context);
	void addRipple(Fn<void()> callback);
	void stopRipple() const;

	[[nodiscard]] ClickHandlerPtr link();
	[[nodiscard]] bool contains(const QPoint &p);

private:
	[[nodiscard]] bool hasLock() const;

	const not_null<HistoryItem*> _item;
	const bool _roundview = false;
	const QSize _size;

	mutable std::unique_ptr<Ui::InfiniteRadialAnimation> _animation;
	std::unique_ptr<Ui::RippleAnimation> _ripple;
	ClickHandlerPtr _link;
	QString _text;
	Ui::Animations::Simple _openedAnimation;
	bool _loading = false;
	bool _opened = false;
	QPoint _lastPaintedPoint;
	QPoint _lastStatePoint;

};

} // namespace HistoryView
