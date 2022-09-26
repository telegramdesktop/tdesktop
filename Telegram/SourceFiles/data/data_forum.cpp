/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_forum.h"

#include "data/data_channel.h"
#include "data/data_session.h"
#include "data/data_forum_topic.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "base/random.h"
#include "apiwrap.h"
#include "lang/lang_keys.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/input_fields.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"

namespace Data {
namespace {

constexpr auto kTopicsFirstLoad = 20;
constexpr auto kTopicsPerPage = 500;

} // namespace

Forum::Forum(not_null<History*> history)
: _history(history)
, _topicsList(&_history->session(), FilterId(0), rpl::single(1)) {
	Expects(_history->peer->isChannel());
}

Forum::~Forum() {
	if (_requestId) {
		_history->session().api().request(_requestId).cancel();
	}
}

not_null<History*> Forum::history() const {
	return _history;
}

not_null<ChannelData*> Forum::channel() const {
	return _history->peer->asChannel();
}

not_null<Dialogs::MainList*> Forum::topicsList() {
	return &_topicsList;
}

void Forum::requestTopics() {
	if (_allLoaded || _requestId) {
		return;
	}
	const auto firstLoad = !_offsetDate;
	const auto loadCount = firstLoad ? kTopicsFirstLoad : kTopicsPerPage;
	const auto api = &_history->session().api();
	_requestId = api->request(MTPchannels_GetForumTopics(
		MTP_flags(0),
		channel()->inputChannel,
		MTPstring(), // q
		MTP_int(_offsetDate),
		MTP_int(_offsetId),
		MTP_int(_offsetTopicId),
		MTP_int(loadCount)
	)).done([=](const MTPmessages_ForumTopics &result) {
		const auto &data = result.data();
		const auto owner = &channel()->owner();
		owner->processUsers(data.vusers());
		owner->processChats(data.vchats());
		owner->processMessages(data.vmessages(), NewMessageType::Existing);
		channel()->ptsReceived(data.vpts().v);
		const auto &list = data.vtopics().v;
		for (const auto &topic : list) {
			const auto rootId = MsgId(topic.data().vid().v);
			const auto i = _topics.find(rootId);
			const auto creating = (i == end(_topics));
			const auto raw = creating
				? _topics.emplace(
					rootId,
					std::make_unique<ForumTopic>(_history, rootId)
				).first->second.get()
				: i->second.get();
			raw->applyTopic(topic);
			if (creating) {
				raw->addToChatList(FilterId(), topicsList());
			}
			if (const auto last = raw->lastServerMessage()) {
				_offsetDate = last->date();
				_offsetId = last->id;
			}
			_offsetTopicId = rootId;
		}
		if (list.isEmpty() || list.size() == data.vcount().v) {
			_allLoaded = true;
		}
		_requestId = 0;
		_chatsListChanges.fire({});
		if (_allLoaded) {
			_chatsListLoadedEvents.fire({});
		}
	}).fail([=](const MTP::Error &error) {
		_allLoaded = true;
		_requestId = 0;
	}).send();
}

void Forum::applyTopicAdded(MsgId rootId, const QString &title) {
	if (const auto i = _topics.find(rootId); i != end(_topics)) {
		i->second->applyTitle(title);
	} else {
		const auto raw = _topics.emplace(
			rootId,
			std::make_unique<ForumTopic>(_history, rootId)
		).first->second.get();
		raw->applyTitle(title);
		raw->addToChatList(FilterId(), topicsList());
		_chatsListChanges.fire({});
	}
}

void Forum::applyTopicRemoved(MsgId rootId) {
	//if (const auto i = _topics.find(rootId)) {
	//	_topics.erase(i);
	//}
}

ForumTopic *Forum::topicFor(not_null<HistoryItem*> item) {
	return topicFor(item->topicRootId());
}

ForumTopic *Forum::topicFor(MsgId rootId) {
	if (rootId != ForumTopic::kGeneralId) {
		if (const auto i = _topics.find(rootId); i != end(_topics)) {
			return i->second.get();
		}
	} else {
		// #TODO forum lang
		applyTopicAdded(rootId, "General! Created.");
		return _topics.find(rootId)->second.get();
	}
	return nullptr;
}

rpl::producer<> Forum::chatsListChanges() const {
	return _chatsListChanges.events();
}

rpl::producer<> Forum::chatsListLoadedEvents() const {
	return _chatsListLoadedEvents.events();
}

void ShowAddForumTopic(
		not_null<Window::SessionController*> controller,
		not_null<ChannelData*> forum) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(u"New Topic"_q));

		const auto title = box->addRow(
			object_ptr<Ui::InputField>(
				box,
				st::defaultInputField,
				rpl::single(u"Topic Title"_q))); // #TODO forum lang
		const auto message = box->addRow(
			object_ptr<Ui::InputField>(
				box,
				st::newGroupDescription,
				Ui::InputField::Mode::MultiLine,
				rpl::single(u"Message"_q))); // #TODO forum lang
		box->setFocusCallback([=] {
			title->setFocusFast();
		});
		box->addButton(tr::lng_create_group_create(), [=] {
			if (!forum->isForum()) {
				box->closeBox();
				return;
			} else if (title->getLastText().trimmed().isEmpty()) {
				title->setFocus();
				return;
			} else if (message->getLastText().trimmed().isEmpty()) {
				message->setFocus();
				return;
			}
			const auto randomId = base::RandomValue<uint64>();
			const auto api = &forum->session().api();
			api->request(MTPchannels_CreateForumTopic(
				MTP_flags(0),
				forum->inputChannel,
				MTP_string(title->getLastText().trimmed()),
				MTPInputMedia(),
				MTP_string(message->getLastText().trimmed()),
				MTP_long(randomId),
				MTPVector<MTPMessageEntity>(),
				MTPInputPeer() // send_as
			)).done([=](const MTPUpdates &result) {
				api->applyUpdates(result, randomId);
				box->closeBox();
			}).fail([=](const MTP::Error &error) {
				api->sendMessageFail(error, forum, randomId);
			}).send();
		});
		box->addButton(tr::lng_cancel(), [=] {
			box->closeBox();
		});
	}), Ui::LayerOption::KeepOther);
}

} // namespace Data
