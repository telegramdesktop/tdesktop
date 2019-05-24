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
#include "info/profile/info_profile_button.h"
#include "info/profile/info_profile_values.h"
#include "boxes/peer_list_box.h"
#include "styles/style_boxes.h"
#include "styles/style_info.h"

namespace {

TextWithEntities BoldText(const QString &text) {
	auto result = TextWithEntities{ text };
	result.entities.push_back(
		EntityInText(EntityType::Bold, 0, text.size()));
	return result;
}

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
		const auto onstack = _callback;
		onstack(row->peer()->asChannel());
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

object_ptr<Ui::RpWidget> SetupList(
		not_null<QWidget*> parent,
		not_null<ChannelData*> channel,
		ChannelData *chat,
		const std::vector<not_null<ChannelData*>> &chats,
		Fn<void(ChannelData*)> callback) {
	const auto already = (chat != nullptr);
	const auto selected = [=](not_null<ChannelData*> chat) {
		if (already) {
			Ui::showPeerHistory(chat, ShowAtUnreadMsgId);
		} else {
			callback(chat);
		}
	};
	const auto controller = Ui::CreateChild<ListController>(
		parent.get(),
		selected);
	controller->setDelegate(controller);
	auto list = object_ptr<PeerListContent>(
		parent,
		controller,
		st::peerListBox);
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
	return std::move(list);
}

object_ptr<Ui::RpWidget> SetupAbout(
		not_null<QWidget*> parent,
		not_null<ChannelData*> channel,
		ChannelData *chat) {
	auto about = object_ptr<Ui::FlatLabel>(
		parent,
		QString(),
		Ui::FlatLabel::InitType::Simple,
		st::linkedChatAbout);
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
	return std::move(about);
}

object_ptr<Ui::RpWidget> SetupCreateGroup(
		not_null<QWidget*> parent,
		not_null<ChannelData*> channel) {
	auto result = object_ptr<Info::Profile::Button>(
		parent,
		Lang::Viewer(
			lng_manage_discussion_group_create
		) | Info::Profile::ToUpperValue(),
		st::infoCreateLinkedChatButton);
	result->addClickHandler([=] {

	});
	return result;
}

object_ptr<Ui::RpWidget> SetupUnlink(
		not_null<QWidget*> parent,
		not_null<ChannelData*> channel,
		Fn<void(ChannelData*)> callback) {
	auto result = object_ptr<Info::Profile::Button>(
		parent,
		Lang::Viewer(channel->isBroadcast()
			? lng_manage_discussion_group_unlink
			: lng_manage_linked_channel_unlink
		) | Info::Profile::ToUpperValue(),
		st::infoUnlinkChatButton);
	result->addClickHandler([=] {
		callback(nullptr);
	});
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
	result->add(
		SetupAbout(result, channel, chat),
		st::linkedChatAboutPadding);
	if (!chat) {
		result->add(SetupCreateGroup(result, channel));
	}
	result->add(SetupList(result, channel, chat, chats, callback));
	if (chat) {
		result->add(SetupUnlink(result, channel, callback));
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
