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
#include "boxes/confirm_box.h"
#include "boxes/add_contact_box.h"
#include "styles/style_boxes.h"
#include "styles/style_info.h"

namespace {

constexpr auto kEnableSearchRowsCount = 10;

TextWithEntities BoldText(const QString &text) {
	auto result = TextWithEntities{ text };
	result.entities.push_back(
		EntityInText(EntityType::Bold, 0, text.size()));
	return result;
}

class Controller : public PeerListController {
public:
	Controller(
		not_null<ChannelData*> channel,
		ChannelData *chat,
		const std::vector<not_null<ChannelData*>> &chats,
		Fn<void(ChannelData*)> callback);

	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	int contentWidth() const override;

private:
	not_null<ChannelData*> _channel;
	ChannelData *_chat = nullptr;
	std::vector<not_null<ChannelData*>> _chats;
	Fn<void(ChannelData*)> _callback;

};

Controller::Controller(
	not_null<ChannelData*> channel,
	ChannelData *chat,
	const std::vector<not_null<ChannelData*>> &chats,
	Fn<void(ChannelData*)> callback)
: _channel(channel)
, _chat(chat)
, _chats(std::move(chats))
, _callback(std::move(callback)) {
}

int Controller::contentWidth() const {
	return st::boxWidth;
}

void Controller::prepare() {
	const auto appendRow = [&](not_null<ChannelData*> chat) {
		if (delegate()->peerListFindRow(chat->id)) {
			return;
		}
		auto row = std::make_unique<PeerListRow>(chat);
		row->setCustomStatus(chat->isPublic()
			? ('@' + chat->username)
			: lang(lng_manage_discussion_group_private));
		delegate()->peerListAppendRow(std::move(row));
	};
	if (_chat) {
		appendRow(_chat);
	} else {
		for (const auto chat : _chats) {
			appendRow(chat);
		}
		if (_chats.size() >= kEnableSearchRowsCount) {
			delegate()->peerListSetSearchMode(PeerListSearchMode::Enabled);
		}
	}
}

void Controller::rowClicked(not_null<PeerListRow*> row) {
	if (_chat != nullptr) {
		Ui::showPeerHistory(_chat, ShowAtUnreadMsgId);
		return;
	}
	const auto chat = row->peer()->asChannel();
	auto text = lng_manage_discussion_group_sure__generic<
		TextWithEntities
	>(
		lt_group,
		BoldText(chat->name),
		lt_channel,
		BoldText(_channel->name));
	if (!_channel->isPublic()) {
		text.append(
			"\n\n" + lang(lng_manage_linked_channel_private));
	}
	const auto box = std::make_shared<QPointer<BoxContent>>();
	const auto sure = [=] {
		if (*box) {
			(*box)->closeBox();
		}
		const auto onstack = _callback;
		onstack(chat);
	};
	*box = Ui::show(
		Box<ConfirmBox>(
			text,
			lang(lng_manage_discussion_group_link),
			sure),
		LayerOption::KeepOther);
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

object_ptr<Ui::RpWidget> SetupFooter(
		not_null<QWidget*> parent,
		not_null<ChannelData*> channel) {
	return object_ptr<Ui::FlatLabel>(
		parent,
		lang(channel->isBroadcast()
			? lng_manage_discussion_group_posted
			: lng_manage_linked_channel_posted),
		Ui::FlatLabel::InitType::Simple,
		st::linkedChatAbout);
}

object_ptr<Ui::RpWidget> SetupCreateGroup(
		not_null<QWidget*> parent,
		not_null<ChannelData*> channel,
		Fn<void(ChannelData*)> callback) {
	Expects(channel->isBroadcast());

	auto result = object_ptr<Info::Profile::Button>(
		parent,
		Lang::Viewer(
			lng_manage_discussion_group_create
		) | Info::Profile::ToUpperValue(),
		st::infoCreateLinkedChatButton);
	result->addClickHandler([=] {
		const auto guarded = crl::guard(parent, callback);
		Ui::show(
			Box<GroupInfoBox>(
				GroupInfoBox::Type::Megagroup,
				channel->name + " Chat",
				guarded),
			LayerOption::KeepOther);
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

object_ptr<BoxContent> EditLinkedChatBox(
		not_null<ChannelData*> channel,
		ChannelData *chat,
		std::vector<not_null<ChannelData*>> &&chats,
		Fn<void(ChannelData*)> callback) {
	Expects(channel->isBroadcast() || (chat != nullptr));

	const auto init = [=](not_null<PeerListBox*> box) {
		auto above = object_ptr<Ui::VerticalLayout>(box);
		above->add(
			SetupAbout(above, channel, chat),
			st::linkedChatAboutPadding);
		if (!chat) {
			above->add(SetupCreateGroup(above, channel, callback));
		}
		box->peerListSetAboveWidget(std::move(above));

		auto below = object_ptr<Ui::VerticalLayout>(box);
		if (chat) {
			below->add(SetupUnlink(below, channel, callback));
		}
		below->add(
			SetupFooter(below, channel),
			st::linkedChatAboutPadding);
		box->peerListSetBelowWidget(std::move(below));

		box->setTitle(langFactory(channel->isBroadcast()
			? lng_manage_discussion_group
			: lng_manage_linked_channel));
		box->addButton(langFactory(lng_close), [=] { box->closeBox(); });
	};
	auto controller = std::make_unique<Controller>(
		channel,
		chat,
		std::move(chats),
		std::move(callback));
	return Box<PeerListBox>(std::move(controller), init);
}

} // namespace

object_ptr<BoxContent> EditLinkedChatBox(
		not_null<ChannelData*> channel,
		std::vector<not_null<ChannelData*>> &&chats,
		Fn<void(ChannelData*)> callback) {
	return EditLinkedChatBox(channel, nullptr, std::move(chats), callback);
}

object_ptr<BoxContent> EditLinkedChatBox(
		not_null<ChannelData*> channel,
		not_null<ChannelData*> chat,
		Fn<void(ChannelData*)> callback) {
	return EditLinkedChatBox(channel, chat, {}, callback);
}
