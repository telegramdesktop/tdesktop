/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"
#include "data/data_messages.h"

namespace Ui {
class RpWidget;
} // namespace Ui

namespace HistoryView {

class Element;

class ElementOverlayHost final {
public:
	using ItemTopFn = Fn<int(not_null<const Element*>)>;
	using PositionFn = Fn<bool(not_null<Element*>, int itemTop)>;
	using MediaStateFn = Fn<void(not_null<Element*>, bool active)>;

	explicit ElementOverlayHost(
		not_null<QWidget*> container,
		ItemTopFn itemTopFn);

	void show(
		not_null<Element*> view,
		FullMsgId context,
		base::unique_qptr<Ui::RpWidget> widget,
		rpl::producer<> closeRequests,
		PositionFn positionFn,
		MediaStateFn mediaStateFn = nullptr,
		Fn<void()> submitFn = nullptr);

	void hide();
	void viewGone(not_null<const Element*> view);
	void updatePosition();
	void handleClickOutside(QPoint clickPos);
	void triggerSubmit(FullMsgId context);
	void setHiddenCallback(Fn<void()> callback);

	[[nodiscard]] bool active() const;
	[[nodiscard]] FullMsgId context() const;

private:
	void cleanup(bool notifyMedia);

	const not_null<QWidget*> _container;
	const ItemTopFn _itemTopFn;

	base::unique_qptr<Ui::RpWidget> _widget;
	Element *_view = nullptr;
	FullMsgId _context;
	PositionFn _positionFn;
	MediaStateFn _mediaStateFn;
	Fn<void()> _submitFn;
	Fn<void()> _hiddenCallback;
	rpl::lifetime _closeLifetime;
};

} // namespace HistoryView
