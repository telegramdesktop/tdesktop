/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/dialogs_community_chats_list.h"

#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_community.h"
#include "data/data_session.h"
#include "dialogs/ui/dialogs_layout.h"
#include "dialogs/dialogs_entry.h"
#include "dialogs/dialogs_row.h"
#include "history/history.h"
#include "main/main_session.h"
#include "ui/painter.h"
#include "window/window_session_controller.h"
#include "styles/style_dialogs.h"

namespace Dialogs {
namespace {

[[nodiscard]] const style::DialogRow &ComputeCommunityInfoSt(
		not_null<const Row*> row) {
	const auto &base = Row::ComputeSt(row->entry(), FilterId());
	if (&base == &st::taggedForumDialogRow) {
		return st::communityInfoTaggedForumDialogRow;
	} else if (&base == &st::forumDialogRow) {
		return st::communityInfoForumDialogRow;
	} else if (&base == &st::taggedDialogRow) {
		return st::communityInfoTaggedDialogRow;
	}
	return st::communityInfoDialogRow;
}

} // namespace

CommunityChatsList::CommunityChatsList(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	not_null<Data::CommunityInfo*> community,
	CommunityChatsKind kind)
: RpWidget(parent)
, _controller(controller)
, _community(community)
, _kind(kind)
, _st(&st::communityInfoDialogRow) {
	setMouseTracking(true);
	_view.setRepaint([=] { update(); });

	// linkedPeersValue() fires immediately on subscription, which performs
	// the first rebuild below.
	_community->linkedPeersValue(
	) | rpl::on_next([=] {
		rebuild();
	}, lifetime());

	// Member chats are not MainList rows here, so they repaint via the
	// community's refreshed() signal (fired on aggregate/unread changes).
	_community->refreshed(
	) | rpl::on_next([=] {
		update();
	}, lifetime());

	using Event = Data::Session::ChatListEntryRefresh;
	_controller->session().data().chatListEntryRefreshes(
	) | rpl::filter([=](const Event &event) {
		const auto history = event.key.history();
		return !event.filterId
			&& history
			&& (history->communityListInfo() == _community)
			&& (history->peer != _community->channel())
			&& (event.existenceChanged
				|| (_kind == CommunityChatsKind::Joined));
	}) | rpl::on_next([=] {
		rebuild();
	}, lifetime());

	using Flag = Data::EntryUpdate::Flag;
	_controller->session().changes().entryUpdates(
		Flag::Height | Flag::Repaint
	) | rpl::filter([=](const Data::EntryUpdate &update) {
		const auto history = update.entry->asHistory();
		return history && _view.contains(history);
	}) | rpl::on_next([=](const Data::EntryUpdate &entryUpdate) {
		if (entryUpdate.flags & Flag::Height) {
			_view.recountHeights(0.);
			resizeToWidth(width());
		} else {
			update();
		}
	}, lifetime());
}

CommunityChatsList::~CommunityChatsList() = default;

void CommunityChatsList::rebuild() {
	const auto wasCount = _view.size();
	_view.clear();
	setSelected(-1);
	setPressed(-1);
	const auto owner = &_controller->session().data();
	const auto add = [&](not_null<History*> history) {
		_view.add(history, 0.);
	};
	if (_kind == CommunityChatsKind::Joined) {
		auto joined = std::vector<not_null<History*>>();
		joined.reserve(_community->linkedPeers().size());
		for (const auto &linked : _community->linkedPeers()) {
			if (Data::CommunityChatJoined(linked.peer)) {
				joined.push_back(owner->history(linked.peer));
			}
		}
		ranges::stable_sort(joined, ranges::greater(), [](
				not_null<History*> history) {
			return std::pair(
				history->sortKeyInChatList(FilterId()),
				history->chatListTimeId());
		});
		for (const auto &history : joined) {
			add(history);
		}
	} else {
		for (const auto &linked : _community->linkedPeers()) {
			if (linked.peer->isUser()) {
				continue;
			} else if (Data::CommunityChatJoined(linked.peer)) {
				continue;
			} else if (!Data::IsCommunityChatViewable(linked)) {
				continue;
			}
			add(owner->history(linked.peer));
		}
	}
	_view.finalize();
	_count = _view.size();
	if (_view.size() != wasCount) {
		resizeToWidth(width());
	}
	update();
	if (underMouse()) {
		updateSelected(mapFromGlobal(QCursor::pos()));
	}
}

int CommunityChatsList::resizeGetHeight(int newWidth) {
	return _view.height();
}

void CommunityChatsList::paintEvent(QPaintEvent *e) {
	Painter p(this);

	const auto clip = e->rect();
	p.fillRect(clip, st::dialogsBg);
	if (_view.empty()) {
		return;
	}
	const auto paused = _controller->isGifPausedAtLeastFor(
		Window::GifPauseReason::Any);
	auto context = Ui::PaintContext{
		.st = _st,
		.community = _community,
		.currentBg = st::dialogsBg,
		.now = crl::now(),
		.width = width(),
		.paused = paused,
		.insideCommunity = true,
	};
	const auto pressed = (_pressed >= 0);
	_view.paint(p, clip, [&](not_null<Row*> row, int index, int top) {
		context.active = false;
		context.selected = pressed
			? (_pressed == index)
			: (_selected == index);
		context.st = &ComputeCommunityInfoSt(row);
		p.translate(0, top);
		Ui::RowPainter::Paint(p, row.get(), nullptr, context);
		p.translate(0, -top);
	});
}

void CommunityChatsList::updateSelected(QPoint local) {
	const auto selected = (local.y() >= 0) ? _view.indexByY(local.y()) : -1;
	setSelected(selected);
	setCursor((_selected >= 0) ? style::cur_pointer : style::cur_default);
}

void CommunityChatsList::setSelected(int selected) {
	if (_selected == selected) {
		return;
	}
	if (const auto row = _view.rowAt(_selected)) {
		update(0, _view.rowTop(_selected), width(), row->height());
	}
	_selected = selected;
	if (const auto row = _view.rowAt(_selected)) {
		update(0, _view.rowTop(_selected), width(), row->height());
	}
}

void CommunityChatsList::setPressed(int pressed) {
	if (_pressed == pressed) {
		return;
	}
	if (const auto row = _view.rowAt(_pressed)) {
		row->stopLastRipple();
	}
	_pressed = pressed;
}

void CommunityChatsList::mouseMoveEvent(QMouseEvent *e) {
	updateSelected(e->pos());
}

void CommunityChatsList::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	updateSelected(e->pos());
	setPressed(_selected);
	if (const auto row = _view.rowAt(_pressed)) {
		const auto top = _view.rowTop(_pressed);
		const auto height = row->height();
		row->addRipple(
			e->pos() - QPoint(0, top),
			QSize(width(), height),
			[=] { update(0, top, width(), height); });
	}
}

void CommunityChatsList::mouseReleaseEvent(QMouseEvent *e) {
	const auto pressed = _pressed;
	setPressed(-1);
	if (pressed >= 0 && pressed == _selected) {
		if (const auto row = _view.rowAt(pressed)) {
			if (const auto history = row->history()) {
				_chatChosen.fire_copy(history);
			}
		}
	}
}

void CommunityChatsList::enterEventHook(QEnterEvent *e) {
	updateSelected(mapFromGlobal(QCursor::pos()));
}

void CommunityChatsList::leaveEventHook(QEvent *e) {
	setSelected(-1);
	setCursor(style::cur_default);
}

rpl::producer<not_null<History*>> CommunityChatsList::chatChosen() const {
	return _chatChosen.events();
}

rpl::producer<int> CommunityChatsList::countValue() const {
	return _count.value();
}

} // namespace Dialogs
