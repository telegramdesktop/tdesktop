/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_pinned_bar.h"

#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "data/data_session.h"
#include "data/data_changes.h"
#include "data/data_poll.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/buttons.h"
#include "history/history_item.h"
#include "history/history.h"
#include "apiwrap.h"
#include "styles/style_chat.h"

namespace HistoryView {
namespace {

[[nodiscard]] rpl::producer<Ui::MessageBarContent> ContentByItem(
		not_null<HistoryItem*> item) {
	return item->history()->session().changes().messageFlagsValue(
		item,
		Data::MessageUpdate::Flag::Edited
	) | rpl::map([=] {
		const auto media = item->media();
		const auto poll = media ? media->poll() : nullptr;
		return Ui::MessageBarContent{
			.id = item->id,
			.title = ((item->id < item->history()->peer->topPinnedMessageId())
				? tr::lng_pinned_previous(tr::now)
				: !poll
				? tr::lng_pinned_message(tr::now)
				: poll->quiz()
				? tr::lng_pinned_quiz(tr::now)
				: tr::lng_pinned_poll(tr::now)),
			.text = item->inReplyText(),
		};
	});
}

[[nodiscard]] rpl::producer<Ui::MessageBarContent> ContentByItemId(
		not_null<Main::Session*> session,
		FullMsgId id,
		bool alreadyLoaded = false) {
	if (!id) {
		return rpl::single(Ui::MessageBarContent());
	} else if (const auto item = session->data().message(id)) {
		return ContentByItem(item);
	} else if (alreadyLoaded) {
		return rpl::single(Ui::MessageBarContent()); // Deleted message?..
	}
	auto load = rpl::make_producer<Ui::MessageBarContent>([=](auto consumer) {
		consumer.put_next(Ui::MessageBarContent{
			.text = tr::lng_contacts_loading(tr::now),
		});
		const auto channel = id.channel
			? session->data().channel(id.channel).get()
			: nullptr;
		const auto callback = [=](ChannelData *channel, MsgId id) {
			consumer.put_done();
		};
		session->api().requestMessageData(channel, id.msg, callback);
		return rpl::lifetime();
	});
	return std::move(
		load
	) | rpl::then(rpl::deferred([=] {
		return ContentByItemId(session, id, true);
	}));
}

} // namespace

PinnedBar::PinnedBar(
	not_null<QWidget*> parent,
	not_null<Main::Session*> session,
	rpl::producer<FullMsgId> itemId,
	bool withClose)
: _wrap(parent, object_ptr<Ui::RpWidget>(parent))
, _close(withClose
	? std::make_unique<Ui::IconButton>(
		_wrap.entity(),
		st::historyReplyCancel)
	: nullptr)
, _shadow(std::make_unique<Ui::PlainShadow>(_wrap.parentWidget())) {
	_wrap.hide(anim::type::instant);
	_shadow->hide();

	_wrap.entity()->paintRequest(
	) | rpl::start_with_next([=](QRect clip) {
		QPainter(_wrap.entity()).fillRect(clip, st::historyPinnedBg);
	}, lifetime());
	_wrap.setAttribute(Qt::WA_OpaquePaintEvent);

	rpl::duplicate(
		itemId
	) | rpl::distinct_until_changed(
	) | rpl::map([=](FullMsgId id) {
		return ContentByItemId(session, id);
	}) | rpl::flatten_latest(
	) | rpl::filter([=](const Ui::MessageBarContent &content) {
		return !content.title.isEmpty() || !content.text.text.isEmpty();
	}) | rpl::start_with_next([=](Ui::MessageBarContent &&content) {
		const auto creating = !_bar;
		if (creating) {
			createControls();
		}
		_bar->set(std::move(content));
		if (creating) {
			_bar->finishAnimating();
		}
	}, lifetime());

	std::move(
		itemId
	) | rpl::map([=](FullMsgId id) {
		return !id;
	}) | rpl::start_with_next([=](bool hidden) {
		_shouldBeShown = !hidden;
		if (!_forceHidden) {
			_wrap.toggle(_shouldBeShown, anim::type::normal);
		} else if (!_shouldBeShown) {
			_bar = nullptr;
		}
	}, lifetime());
}

void PinnedBar::createControls() {
	Expects(!_bar);

	_bar = std::make_unique<Ui::MessageBar>(
		_wrap.entity(),
		st::defaultMessageBar);
	if (_close) {
		_close->raise();
	}

	_bar->widget()->move(0, 0);
	_bar->widget()->show();
	_wrap.entity()->resize(_wrap.entity()->width(), _bar->widget()->height());

	_wrap.geometryValue(
	) | rpl::start_with_next([=](QRect rect) {
		_shadow->setGeometry(
			rect.x(),
			rect.y() + rect.height(),
			rect.width(),
			st::lineWidth);
		_bar->widget()->resizeToWidth(
			rect.width() - (_close ? _close->width() : 0));
		const auto hidden = _wrap.isHidden() || !rect.height();
		if (_shadow->isHidden() != hidden) {
			_shadow->setVisible(!hidden);
		}
		if (_close) {
			_close->moveToRight(0, 0);
		}
	}, _bar->widget()->lifetime());

	_wrap.shownValue(
	) | rpl::skip(
		1
	) | rpl::filter([=](bool shown) {
		return !shown && !_forceHidden;
	}) | rpl::start_with_next([=] {
		_bar = nullptr;
	}, _bar->widget()->lifetime());

	Ensures(_bar != nullptr);
}

void PinnedBar::show() {
	if (!_forceHidden) {
		return;
	}
	_forceHidden = false;
	if (_shouldBeShown) {
		_wrap.show(anim::type::instant);
		_shadow->show();
	}
}

void PinnedBar::hide() {
	if (_forceHidden) {
		return;
	}
	_forceHidden = true;
	_wrap.hide(anim::type::instant);
	_shadow->hide();
}

void PinnedBar::raise() {
	_wrap.raise();
	_shadow->raise();
}

void PinnedBar::move(int x, int y) {
	_wrap.move(x, y);
}

void PinnedBar::resizeToWidth(int width) {
	_wrap.entity()->resizeToWidth(width);
}

int PinnedBar::height() const {
	return !_forceHidden
		? _wrap.height()
		: _shouldBeShown
		? st::historyReplyHeight
		: 0;
}

rpl::producer<int> PinnedBar::heightValue() const {
	return _wrap.heightValue();
}

rpl::producer<> PinnedBar::closeClicks() const {
	return !_close
		? (rpl::never<>() | rpl::type_erased())
		: (_close->clicks() | rpl::map([] { return rpl::empty_value(); }));
}

} // namespace HistoryView