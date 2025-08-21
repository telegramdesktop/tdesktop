/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_chat_switch_process.h"

#include "core/shortcuts.h"
#include "data/components/recent_peers.h"
#include "data/data_thread.h"
#include "main/main_session.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/shadow.h"
#include "ui/controls/userpic_button.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "styles/style_layers.h"
#include "styles/style_window.h"

namespace Window {

struct ChatSwitchProcess::Entry {
	not_null<Ui::AbstractButton*> button;
	Ui::Animations::Simple overAnimation;
};

ChatSwitchProcess::ChatSwitchProcess(
	not_null<Ui::RpWidget*> geometry,
	not_null<Main::Session*> session,
	Data::Thread *opened)
: _session(session)
, _widget(std::make_unique<Ui::RpWidget>(
	geometry->parentWidget() ? geometry->parentWidget() : geometry))
, _view(Ui::CreateChild<Ui::RpWidget>(_widget.get()))\
, _bg(st::boxRadius, st::boxBg) {
	setupWidget(geometry);
	setupContent(opened);
	setupView();
}

ChatSwitchProcess::~ChatSwitchProcess() = default;

rpl::producer<not_null<Data::Thread*>> ChatSwitchProcess::chosen() const {
	return _chosen.events();
}

rpl::producer<> ChatSwitchProcess::closeRequests() const {
	return _closeRequests.events();
}

void ChatSwitchProcess::process(const Request &request) {
	Expects(_selected < int(_list.size()));

	const auto count = int(_list.size());
	if (request.action == Qt::Key_Escape) {
		_closeRequests.fire({});
	} else if (request.action == Qt::Key_Enter) {
		if (_selected >= 0) {
			_chosen.fire_copy(_list[_selected]);
		} else {
			_closeRequests.fire({});
		}
	} else if (request.action == Qt::Key_Tab) {
		if (_selected < 0 || _selected + 1 >= count) {
			setSelected(0);
		} else {
			setSelected(_selected + 1);
		}
	} else if (request.action == Qt::Key_Backtab) {
		if (_selected <= 0) {
			setSelected(count - 1);
		} else {
			setSelected(_selected - 1);
		}
	}
}

void ChatSwitchProcess::setSelected(int index) {
	if (_selected == index || _list.empty()) {
		return;
	}
	if (_selected >= 0) {
		auto &entry = _entries[_selected];
		const auto raw = entry.button.get();
		entry.overAnimation.start([=] {
			raw->update();
		}, 1., 0., st::slideWrapDuration);
	}
	_selected = index;
	if (_selected >= 0) {
		auto &entry = _entries[_selected];
		const auto raw = entry.button.get();
		entry.overAnimation.start([=] {
			raw->update();
		}, 0., 1., st::slideWrapDuration);
	}
}

void ChatSwitchProcess::setupWidget(not_null<Ui::RpWidget*> geometry) {
	geometry->geometryValue(
	) | rpl::start_with_next([=](QRect value) {
		const auto parent = geometry->parentWidget();
		_widget->setGeometry((parent == _widget->parentWidget())
			? value
			: QRect(QPoint(), value.size()));
	}, _widget->lifetime());

	_widget->events() | rpl::start_with_next([=](not_null<QEvent*> e) {
		if (e->type() == QEvent::MouseButtonPress) {
			_closeRequests.fire({});
		}
	}, _widget->lifetime());

	_widget->show();
}

void ChatSwitchProcess::setupContent(Data::Thread *opened) {
	_list = _session->recentPeers().collectChatOpenHistory();
	if (opened) {
		const auto i = ranges::find(_list, not_null(opened));
		if (i == end(_list)) {
			_list.insert(begin(_list), opened);
		} else if (i != begin(_list)) {
			ranges::rotate(begin(_list), i, i + 1);
		}
		_selected = 0;
	}

	auto index = 0;
	for (const auto &thread : _list) {
		const auto button = Ui::CreateChild<Ui::AbstractButton>(_view.get());
		button->resize(st::chatSwitchSize);
		button->paintRequest() | rpl::start_with_next([=] {
			const auto selection = _entries[index].overAnimation.value(
				(index == _selected) ? 1. : 0.);
			if (selection > 0.) {
				auto p = QPainter(button);
				auto hq = PainterHighQualityEnabler(p);
				const auto radius = st::boxRadius;
				const auto line = st::chatSwitchSelectLine;
				auto pen = st::defaultRoundCheckbox.bgActive->p;
				pen.setWidthF(line * selection);
				p.setPen(pen);
				const auto r = QRectF(button->rect()).marginsRemoved(
					{ line / 2., line / 2., line / 2., line / 2. });
				p.drawRoundedRect(r, radius, radius);
			}
		}, button->lifetime());
		button->setClickedCallback([=] {
			_chosen.fire_copy(thread);
		});
		button->events() | rpl::start_with_next([=](not_null<QEvent*> e) {
			if (e->type() == QEvent::MouseMove) {
				setSelected(index);
			}
		}, button->lifetime());
		button->show();

		const auto userpic = Ui::CreateChild<Ui::UserpicButton>(
			button,
			thread->peer(),
			st::chatSwitchUserpic);
		userpic->show();
		userpic->move(
			((button->width() - userpic->width()) / 2),
			st::chatSwitchUserpicTop);
		userpic->setAttribute(Qt::WA_TransparentForMouseEvents);

		const auto label = Ui::CreateChild<Ui::FlatLabel>(
			button,
			thread->chatListName(),
			st::chatSwitchNameLabel);
		label->setBreakEverywhere(true);
		label->show();
		label->resizeToNaturalWidth(
			button->width() - 2 * st::chatSwitchNameSkip);
		label->move(
			(button->width() - label->width()) / 2,
			(button->height()
				+ userpic->y()
				+ userpic->height()
				- label->height()) / 2);

		_entries.push_back({ .button = button });

		++index;
	}
}

void ChatSwitchProcess::setupView() {
	_widget->sizeValue() | rpl::start_with_next([=](QSize size) {
		layout(size);
	}, _view->lifetime());
	_view->show();

	_view->paintRequest() | rpl::start_with_next([=](QRect clip) {
		if (_outer.isEmpty()) {
			return;
		}
		auto p = QPainter(_view);
		p.translate(-_shadowed.topLeft());
		Ui::Shadow::paint(p, _outer, _view->width(), st::boxRoundShadow);
		_bg.paint(p, _outer);
	}, _view->lifetime());

	_view->events() | rpl::start_with_next([=](not_null<QEvent*> e) {
		if (e->type() == QEvent::MouseButtonPress) {
			e->accept();
		}
	}, _view->lifetime());
}

void ChatSwitchProcess::layout(QSize size) {
	const auto full = QRect(QPoint(), size);
	const auto outer = full.marginsRemoved(st::chatSwitchMargins);
	auto inner = outer.marginsRemoved(st::chatSwitchPadding);
	const auto available = inner.width();
	const auto canPerRow = (available / st::chatSwitchSize.width());
	if (canPerRow < 1 || _list.empty()) {
		return;
	}
	const auto count = int(_list.size());
	const auto rows = (count + canPerRow - 1) / canPerRow;
	const auto minPerRow = count / rows;
	const auto wideRows = (count - (minPerRow * rows));
	const auto maxPerRow = wideRows ? (minPerRow + 1) : minPerRow;
	const auto narrowShift = wideRows ? (st::chatSwitchSize.width() / 2) : 0;
	const auto width = maxPerRow * st::chatSwitchSize.width();
	const auto height = rows * st::chatSwitchSize.height();

	size = QSize(width, height);
	_inner = QRect(
		(full.width() - width) / 2,
		(full.height() - height) / 2,
		width,
		height);
	_outer = _inner.marginsAdded(st::chatSwitchPadding);

	const auto padding = st::boxRoundShadow.extend + st::chatSwitchPadding;

	auto index = 0;
	auto top = padding.top();
	for (auto row = 0; row != rows; ++row) {
		const auto columns = (row < wideRows) ? maxPerRow : minPerRow;
		auto left = padding.left() + ((row < wideRows) ? 0 : narrowShift);
		for (auto column = 0; column != columns; ++column) {
			auto &entry = _entries[index++];
			entry.button->moveToLeft(left, top, _inner.width());
			left += st::chatSwitchSize.width();
		}
		top += st::chatSwitchSize.height();
	}

	_shadowed = _outer.marginsAdded(st::boxRoundShadow.extend);
	_view->setGeometry(_shadowed);
}

rpl::lifetime &ChatSwitchProcess::lifetime() {
	return _lifetime;
}

} // namespace Window
