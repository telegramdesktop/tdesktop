/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/ui/dialogs_stories_content.h"

#include "data/data_changes.h"
#include "data/data_session.h"
#include "data/data_stories.h"
#include "data/data_user.h"
#include "dialogs/ui/dialogs_stories_list.h"
#include "main/main_session.h"
#include "ui/painter.h"

#include "history/history.h" // #TODO stories testing

namespace Dialogs::Stories {
namespace {

class PeerUserpic final : public Userpic {
public:
	explicit PeerUserpic(not_null<PeerData*> peer);

	QImage image(int size) override;
	void subscribeToUpdates(Fn<void()> callback) override;

private:
	struct Subscribed {
		explicit Subscribed(Fn<void()> callback)
		: callback(std::move(callback)) {
		}

		Ui::PeerUserpicView view;
		Fn<void()> callback;
		InMemoryKey key;
		rpl::lifetime photoLifetime;
		rpl::lifetime downloadLifetime;
	};

	[[nodiscard]] bool waitingUserpicLoad() const;
	void processNewPhoto();

	const not_null<PeerData*> _peer;
	QImage _frame;
	std::unique_ptr<Subscribed> _subscribed;

};

class State final {
public:
	explicit State(not_null<Data::Stories*> data);

	[[nodiscard]] Content next();

private:
	const not_null<Data::Stories*> _data;
	base::flat_map<not_null<UserData*>, std::shared_ptr<Userpic>> _userpics;

};

PeerUserpic::PeerUserpic(not_null<PeerData*> peer)
: _peer(peer) {
}

QImage PeerUserpic::image(int size) {
	Expects(_subscribed != nullptr);

	const auto good = (_frame.width() == size * _frame.devicePixelRatio());
	const auto key = _peer->userpicUniqueKey(_subscribed->view);
	if (!good || (_subscribed->key != key && !waitingUserpicLoad())) {
		_subscribed->key = key;
		_frame = QImage(
			QSize(size, size) * style::DevicePixelRatio(),
			QImage::Format_ARGB32_Premultiplied);
		_frame.setDevicePixelRatio(style::DevicePixelRatio());
		_frame.fill(Qt::transparent);

		auto p = Painter(&_frame);
		_peer->paintUserpic(p, _subscribed->view, 0, 0, size);
	}
	return _frame;
}

bool PeerUserpic::waitingUserpicLoad() const {
	return _peer->hasUserpic() && _peer->useEmptyUserpic(_subscribed->view);
}

void PeerUserpic::subscribeToUpdates(Fn<void()> callback) {
	if (!callback) {
		_subscribed = nullptr;
		return;
	}
	_subscribed = std::make_unique<Subscribed>(std::move(callback));

	_peer->session().changes().peerUpdates(
		_peer,
		Data::PeerUpdate::Flag::Photo
	) | rpl::start_with_next([=] {
		_subscribed->callback();
		processNewPhoto();
	}, _subscribed->photoLifetime);

	processNewPhoto();
}

void PeerUserpic::processNewPhoto() {
	Expects(_subscribed != nullptr);

	if (!waitingUserpicLoad()) {
		_subscribed->downloadLifetime.destroy();
		return;
	}
	_peer->session().downloaderTaskFinished(
	) | rpl::filter([=] {
		return !waitingUserpicLoad();
	}) | rpl::start_with_next([=] {
		_subscribed->callback();
		_subscribed->downloadLifetime.destroy();
	}, _subscribed->downloadLifetime);
}

State::State(not_null<Data::Stories*> data)
: _data(data) {
}

Content State::next() {
	auto result = Content();
#if 1 // #TODO stories testing
	const auto &all = _data->all();
	result.users.reserve(all.size());
	for (const auto &list : all) {
		auto userpic = std::shared_ptr<Userpic>();
		const auto user = list.user;
#else
	const auto list = _data->owner().chatsList();
	const auto &all = list->indexed()->all();
	result.users.reserve(all.size());
	for (const auto &entry : all) {
		if (const auto history = entry->history()) {
			if (const auto user = history->peer->asUser(); user && !user->isBot()) {
				auto userpic = std::shared_ptr<Userpic>();
#endif
		if (const auto i = _userpics.find(user); i != end(_userpics)) {
			userpic = i->second;
		} else {
			userpic = std::make_shared<PeerUserpic>(user);
			_userpics.emplace(user, userpic);
		}
		result.users.push_back({
			.id = uint64(user->id.value),
			.name = user->shortName(),
			.userpic = std::move(userpic),
#if 1 // #TODO stories testing
			.unread = list.unread(),
#else
			.unread = history->chatListBadgesState().unread
		});
	}
#endif
		});
	}
	return result;
}

} // namespace

rpl::producer<Content> ContentForSession(not_null<Main::Session*> session) {
	return [=](auto consumer) {
		auto result = rpl::lifetime();
		const auto stories = &session->data().stories();
		const auto state = result.make_state<State>(stories);
		rpl::single(
			rpl::empty
		) | rpl::then(
#if 1 // #TODO stories testing
			stories->allChanged()
#else
			rpl::merge(
				session->data().chatsListChanges(
				) | rpl::filter(
					rpl::mappers::_1 == nullptr
				) | rpl::to_empty,
				session->data().unreadBadgeChanges())
#endif
		) | rpl::start_with_next([=] {
			consumer.put_next(state->next());
		}, result);
		return result;
	};
}

} // namespace Dialogs::Stories
