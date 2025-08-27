/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_chat_switch_process.h"

#include "core/shortcuts.h"
#include "data/components/recent_peers.h"
#include "data/data_forum_topic.h"
#include "data/data_peer.h"
#include "data/data_saved_sublist.h"
#include "data/data_thread.h"
#include "info/profile/info_profile_cover.h"
#include "main/main_session.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/shadow.h"
#include "ui/controls/userpic_button.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_window.h"

namespace Window {
namespace {

class Button final : public Ui::AbstractButton {
public:
	Button(
		QWidget *parent,
		not_null<Data::Thread*> thread,
		base::flat_map<not_null<PeerData*>, Ui::PeerUserpicView> &userpics);

	[[nodiscard]] rpl::producer<> selectRequests() const;

	void setSelected(
		bool selected,
		anim::type animated = anim::type::normal);

private:
	void setup(
		not_null<Data::Thread*> thread,
		base::flat_map<not_null<PeerData*>, Ui::PeerUserpicView> &userpics);

	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;

	rpl::event_stream<> _selectRequests;
	Ui::Animations::Simple _overAnimation;
	bool _selected = false;

};

Button::Button(
	QWidget *parent,
	not_null<Data::Thread*> thread,
	base::flat_map<not_null<PeerData*>, Ui::PeerUserpicView> &userpics)
: AbstractButton(parent) {
	setup(thread, userpics);
}

void Button::setup(
		not_null<Data::Thread*> thread,
		base::flat_map<not_null<PeerData*>, Ui::PeerUserpicView> &userpics) {
	resize(st::chatSwitchSize);

	auto userpicSt = &st::chatSwitchUserpic;
	const auto userpicSize = userpicSt->size;
	if (const auto topic = thread->asTopic()) {
		using namespace Info::Profile;
		const auto userpic = Ui::CreateChild<TopicIconButton>(
			this,
			topic,
			[] { return true; }); // paused
		userpic->show();
		userpic->move(
			((width() - userpic->width()) / 2),
			st::chatSwitchUserpicTop);
		userpic->setAttribute(Qt::WA_TransparentForMouseEvents);

		userpicSt = &st::chatSwitchUserpicSmall;
	} else if (const auto sublist = thread->asSublist()) {
		const auto sublistPeer = sublist->sublistPeer();
		const auto userpic = Ui::CreateChild<Ui::UserpicButton>(
			this,
			sublistPeer,
			st::chatSwitchUserpicSublist);
		userpic->show();
		userpic->move(
			((width() - userpicSize.width()) / 2),
			st::chatSwitchUserpicTop);
		userpic->setAttribute(Qt::WA_TransparentForMouseEvents);
		userpics.emplace(sublistPeer, sublistPeer->createUserpicView());

		userpicSt = &st::chatSwitchUserpicSmall;
	}
	const auto peer = thread->peer();
	const auto userpic = Ui::CreateChild<Ui::UserpicButton>(
		this,
		peer,
		*userpicSt);
	userpic->show();
	userpic->move(
		(((width() - userpicSize.width()) / 2)
			+ (userpicSize.width() - userpicSt->size.width())),
		(st::chatSwitchUserpicTop
			+ (userpicSize.height() - userpicSt->size.height())));
	userpic->setAttribute(Qt::WA_TransparentForMouseEvents);
	userpics.emplace(peer, peer->createUserpicView());

	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		this,
		thread->chatListName(),
		st::chatSwitchNameLabel);
	label->setBreakEverywhere(true);
	label->show();
	label->resizeToNaturalWidth(
		width() - 2 * st::chatSwitchNameSkip);
	label->move(
		(width() - label->width()) / 2,
		(height() + userpic->y() + userpic->height() - label->height()) / 2);

	show();
}

rpl::producer<> Button::selectRequests() const {
	return _selectRequests.events();
}

void Button::setSelected(bool selected, anim::type animated) {
	if (_selected == selected) {
		if (animated == anim::type::instant) {
			_overAnimation.stop();
		}
		return;
	}
	_selected = selected;
	if (animated == anim::type::instant) {
		_overAnimation.stop();
		update();
	} else {
		_overAnimation.start(
			[=] { update(); },
			selected ? 0. : 1.,
			selected ? 1. : 0.,
			st::slideWrapDuration);
	}
}

void Button::paintEvent(QPaintEvent *e) {
	const auto selected = _selected ? 1. : 0.;
	const auto selection = _overAnimation.value(selected);
	if (selection <= 0.) {
		return;
	}

	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto radius = st::boxRadius;
	const auto line = st::chatSwitchSelectLine;
	auto pen = st::defaultRoundCheckbox.bgActive->p;
	pen.setWidthF(line * selection);
	p.setPen(pen);
	const auto r = QRectF(rect()).marginsRemoved(
		{ line / 2., line / 2., line / 2., line / 2. });
	p.drawRoundedRect(r, radius, radius);
}

void Button::mouseMoveEvent(QMouseEvent *e) {
	if (!_selected) {
		_selectRequests.fire({});
	}
}

} // namespace

struct ChatSwitchProcess::Entry {
	std::unique_ptr<Button> button;
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

ChatSwitchProcess::~ChatSwitchProcess() {
	_session->recentPeers().chatOpenKeepUserpics(std::move(_userpics));
}

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
		_entries[_selected].button->setSelected(false);
	}
	_selected = index;
	if (_selected >= 0) {
		_entries[_selected].button->setSelected(true);
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

	const auto find = [=](Button *button) {
		return ranges::find(_entries, button, [](const Entry &entry) {
			return entry.button.get();
		});
	};
	for (const auto &thread : _list) {
		auto button = std::make_unique<Button>(
			_view.get(),
			thread,
			_userpics);
		const auto raw = button.get();

		raw->selectRequests() | rpl::start_with_next([=] {
			const auto i = find(raw);
			setSelected(int(i - begin(_entries)));
		}, raw->lifetime());

		raw->setClickedCallback([=] {
			_chosen.fire_copy(thread);
		});

		_entries.push_back({ .button = std::move(button) });

		auto destroyed = thread->asTopic()
			? thread->asTopic()->destroyed()
			: thread->asSublist()
			? thread->asSublist()->destroyed()
			: nullptr;
		if (!destroyed) {
			continue;
		}
		std::move(destroyed) | rpl::start_with_next([=] {
			_list.erase(ranges::remove(_list, thread), end(_list));
			if (const auto i = find(raw); i != end(_entries)) {
				const auto index = int(i - begin(_entries));
				if (_selected > index) {
					--_selected;
				} else if (_selected == index) {
					_selected = -1;
				}

				_entries.erase(i);
				layout(_widget->size());
			}
		}, raw->lifetime());
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
