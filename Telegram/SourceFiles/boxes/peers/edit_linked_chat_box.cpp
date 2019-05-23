/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/edit_linked_chat_box.h"

#include "lang/lang_keys.h"
#include "data/data_channel.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "boxes/peer_list_box.h"
#include "styles/style_boxes.h"

namespace {

TextWithEntities BoldText(const QString &text) {
	auto result = TextWithEntities{ text };
	result.entities.push_back(
		EntityInText(EntityType::Bold, 0, text.size()));
	return result;
}

} // namespace

EditLinkedChatBox::EditLinkedChatBox(
	QWidget*,
	not_null<ChannelData*> channel,
	const std::vector<not_null<ChannelData*>> &chats,
	Fn<void(ChannelData*)> callback)
: _channel(channel)
, _content(setupContent(channel, nullptr, chats, callback)) {
}

EditLinkedChatBox::EditLinkedChatBox(
	QWidget*,
	not_null<ChannelData*> channel,
	not_null<ChannelData*> chat,
	Fn<void(ChannelData*)> callback)
: _channel(channel)
, _content(setupContent(channel, chat, {}, callback)) {
}

object_ptr<Ui::RpWidget> EditLinkedChatBox::setupContent(
		not_null<ChannelData*> channel,
		ChannelData *chat,
		const std::vector<not_null<ChannelData*>> &chats,
		Fn<void(ChannelData*)> callback) {
	Expects(channel->isBroadcast() || (chat != nullptr));

	auto result = object_ptr<Ui::VerticalLayout>(this);
	const auto about = result->add(
		object_ptr<Ui::FlatLabel>(
			result,
			QString(),
			Ui::FlatLabel::InitType::Simple,
			st::linkedChatAbout),
		st::linkedChatAboutPadding);
	about->setMarkedText([&]() -> TextWithEntities {
		if (!channel->isBroadcast()) {
			return lng_manage_linked_channel_about__generic<
				TextWithEntities
			>(lt_channel, BoldText(chat->name));
		} else if (chat != nullptr) {
			return lng_manage_discussion_group_about_chosen__generic<
				TextWithEntities
			>(lt_group, BoldText(chat->name));
		} else {
			return { lang(lng_manage_discussion_group_about) };
		}
	}());

	class ListController
		: public PeerListController
		, public PeerListContentDelegate {
	public:
		ListController(Fn<void(not_null<ChannelData*>)> callback)
		: _callback(std::move(callback)) {
		}
		void prepare() override {
		}
		void rowClicked(not_null<PeerListRow*> row) override {
			_callback(row->peer()->asChannel());
		}
		void peerListSetTitle(Fn<QString()> title) override {
		}
		void peerListSetAdditionalTitle(Fn<QString()> title) override {
		}
		bool peerListIsRowSelected(not_null<PeerData*> peer) override {
			return false;
		}
		int peerListSelectedRowsCount() override {
			return 0;
		}
		auto peerListCollectSelectedRows()
		-> std::vector<not_null<PeerData*>> override {
			return {};
		}
		void peerListScrollToTop() override {
		}
		void peerListAddSelectedRowInBunch(
			not_null<PeerData*> peer) override {
		}
		void peerListFinishSelectedRowsBunch() override {
		}
		void peerListSetDescription(
			object_ptr<Ui::FlatLabel> description) override {
		}

	private:
		Fn<void(not_null<ChannelData*>)> _callback;

	};
	const auto controller = Ui::CreateChild<ListController>(this, [=](not_null<ChannelData*> chat) {
		const auto onstack = callback;
		onstack(chat);
	});
	controller->setDelegate(controller);
	const auto list = result->add(object_ptr<PeerListContent>(
		this,
		controller,
		st::peerListBox));
	const auto createRow = [](not_null<ChannelData*> chat) {
		auto result = std::make_unique<PeerListRow>(chat);
		result->setCustomStatus(chat->isPublic()
			? ('@' + chat->username)
			: lang(lng_manage_discussion_group_private));
		return result;
	};
	if (chat) {
		list->appendRow(createRow(chat));
	} else {
		for (const auto chat : chats) {
			list->appendRow(createRow(chat));
		}
	}
	return result;
}

void EditLinkedChatBox::prepare() {
	setTitle(langFactory(_channel->isBroadcast()
		? lng_manage_discussion_group
		: lng_manage_linked_channel));

	setDimensionsToContent(
		st::boxWidth,
		setInnerWidget(std::move(_content)));

	addButton(langFactory(lng_close), [=] { closeBox(); });
}
