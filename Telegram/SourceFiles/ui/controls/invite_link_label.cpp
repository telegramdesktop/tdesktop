/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/controls/invite_link_label.h"

#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/popup_menu.h"
#include "styles/style_info.h"

namespace Ui {

InviteLinkLabel::InviteLinkLabel(
	not_null<QWidget*> parent,
	rpl::producer<QString> text,
	Fn<base::unique_qptr<PopupMenu>()> createMenu)
: _outer(std::in_place, parent) {
	_outer->resize(_outer->width(), st::inviteLinkFieldHeight);
	const auto label = CreateChild<FlatLabel>(
		_outer.get(),
		std::move(text),
		createMenu ? st::defaultFlatLabel : st::inviteLinkFieldLabel);
	label->setAttribute(Qt::WA_TransparentForMouseEvents);

	const auto button = createMenu
		? CreateChild<IconButton>(_outer.get(), st::inviteLinkThreeDots)
		: (IconButton*)(nullptr);

	_outer->widthValue(
	) | rpl::start_with_next([=](int width) {
		const auto margin = st::inviteLinkFieldMargin;
		const auto labelWidth = width - margin.left() - margin.right();
		label->resizeToWidth(labelWidth);
		label->moveToLeft(
			createMenu
				? margin.left()
				: (width - labelWidth) / 2,
			margin.top());
		if (button) {
			button->moveToRight(0, 0);
		}
	}, _outer->lifetime());

	_outer->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(_outer.get());
		p.setPen(Qt::NoPen);
		p.setBrush(st::filterInputInactiveBg);
		{
			PainterHighQualityEnabler hq(p);
			p.drawRoundedRect(
				_outer->rect(),
				st::inviteLinkFieldRadius,
				st::inviteLinkFieldRadius);
		}
	}, _outer->lifetime());

	_outer->setCursor(style::cur_pointer);

	if (createMenu) {
		rpl::merge(
			button->clicks() | rpl::to_empty,
			_outer->events(
			) | rpl::filter([=](not_null<QEvent*> event) {
				return (event->type() == QEvent::MouseButtonPress)
					&& (static_cast<QMouseEvent*>(event.get())->button()
						== Qt::RightButton);
			}) | rpl::to_empty
		) | rpl::start_with_next([=] {
			if (_menu) {
				_menu = nullptr;
			} else if ((_menu = createMenu())) {
				_menu->popup(QCursor::pos());
			}
		}, _outer->lifetime());
	}
}

object_ptr<RpWidget> InviteLinkLabel::take() {
	return object_ptr<RpWidget>::fromRaw(_outer.get());
}

rpl::producer<> InviteLinkLabel::clicks() {
	return _outer->events(
	) | rpl::filter([=](not_null<QEvent*> event) {
		return (event->type() == QEvent::MouseButtonPress)
			&& (static_cast<QMouseEvent*>(event.get())->button()
				== Qt::LeftButton);
	}) | rpl::map([=](not_null<QEvent*> event) {
		return _outer->events(
		) | rpl::filter([=](not_null<QEvent*> event) {
			return (event->type() == QEvent::MouseButtonRelease)
				&& (static_cast<QMouseEvent*>(event.get())->button()
					== Qt::LeftButton);
		}) | rpl::take(1) | rpl::filter([=](not_null<QEvent*> event) {
			return (_outer->rect().contains(
				static_cast<QMouseEvent*>(event.get())->pos()));
		});
	}) | rpl::flatten_latest() | rpl::to_empty;
}

rpl::lifetime &InviteLinkLabel::lifetime() {
	return _outer->lifetime();
}

} // namespace Ui
