/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export/data/export_data_types.h"

#include <QtCore/QDateTime>

namespace App { // Hackish..
QString formatPhone(QString phone);
} // namespace App

namespace Export {
namespace Data {
namespace {

constexpr auto kUserPeerIdShift = (1ULL << 32);
constexpr auto kChatPeerIdShift = (2ULL << 32);

} // namespace

PeerId UserPeerId(int32 userId) {
	return kUserPeerIdShift | uint32(userId);
}

PeerId ChatPeerId(int32 chatId) {
	return kChatPeerIdShift | uint32(chatId);
}

int32 BarePeerId(PeerId peerId) {
	return int32(peerId & 0xFFFFFFFFULL);
}

PeerId ParsePeerId(const MTPPeer &data) {
	switch (data.type()) {
	case mtpc_peerUser:
		return UserPeerId(data.c_peerUser().vuser_id.v);
	case mtpc_peerChat:
		return ChatPeerId(data.c_peerChat().vchat_id.v);
	case mtpc_peerChannel:
		return ChatPeerId(data.c_peerChannel().vchannel_id.v);
	}
	Unexpected("Type in ParsePeerId.");
}

Utf8String ParseString(const MTPstring &data) {
	return data.v;
}

FileLocation ParseLocation(const MTPFileLocation &data) {
	switch (data.type()) {
	case mtpc_fileLocation: {
		const auto &location = data.c_fileLocation();
		return {
			location.vdc_id.v,
			MTP_inputFileLocation(
				location.vvolume_id,
				location.vlocal_id,
				location.vsecret)
		};
	} break;
	case mtpc_fileLocationUnavailable: {
		const auto &location = data.c_fileLocationUnavailable();
		return {
			0,
			MTP_inputFileLocation(
				location.vvolume_id,
				location.vlocal_id,
				location.vsecret)
		};
	} break;
	}
	Unexpected("Type in ParseLocation.");
}

File ParseMaxImage(
		const MTPVector<MTPPhotoSize> &data,
		const QString &suggestedPath) {
	auto result = File();
	result.suggestedPath = suggestedPath;

	auto maxArea = int64(0);
	for (const auto &size : data.v) {
		switch (size.type()) {
		case mtpc_photoSize: {
			const auto &fields = size.c_photoSize();
			const auto area = fields.vw.v * int64(fields.vh.v);
			if (area > maxArea) {
				result.location = ParseLocation(fields.vlocation);
				result.size = fields.vsize.v;
				result.content = QByteArray();
				maxArea = area;
			}
		} break;

		case mtpc_photoCachedSize: {
			const auto &fields = size.c_photoCachedSize();
			const auto area = fields.vw.v * int64(fields.vh.v);
			if (area > maxArea) {
				result.location = ParseLocation(fields.vlocation);
				result.size = fields.vbytes.v.size();
				result.content = fields.vbytes.v;
				maxArea = area;
			}
		} break;
		}
	}
	return result;
}

Photo ParsePhoto(const MTPPhoto &data, const QString &suggestedPath) {
	auto result = Photo();
	switch (data.type()) {
	case mtpc_photo: {
		const auto &photo = data.c_photo();
		result.id = photo.vid.v;
		result.date = photo.vdate.v;
		result.image = ParseMaxImage(photo.vsizes, suggestedPath);
	} break;

	case mtpc_photoEmpty: {
		const auto &photo = data.c_photoEmpty();
		result.id = photo.vid.v;
	} break;

	default: Unexpected("Photo type in ParsePhoto.");
	}
	return result;
}

Utf8String FormatDateTime(
		TimeId date,
		QChar dateSeparator,
		QChar timeSeparator,
		QChar separator) {
	const auto value = QDateTime::fromTime_t(date);
	return (QString("%1") + dateSeparator + "%2" + dateSeparator + "%3"
		+ separator + "%4" + timeSeparator + "%5" + timeSeparator + "%6"
	).arg(value.date().year()
	).arg(value.date().month(), 2, 10, QChar('0')
	).arg(value.date().day(), 2, 10, QChar('0')
	).arg(value.time().hour(), 2, 10, QChar('0')
	).arg(value.time().minute(), 2, 10, QChar('0')
	).arg(value.time().second(), 2, 10, QChar('0')
	).toUtf8();
}

UserpicsSlice ParseUserpicsSlice(const MTPVector<MTPPhoto> &data) {
	const auto &list = data.v;
	auto result = UserpicsSlice();
	result.list.reserve(list.size());
	for (const auto &photo : list) {
		const auto suggestedPath = "PersonalPhotos/Photo_"
			+ (photo.type() == mtpc_photo
				? QString::fromUtf8(
					FormatDateTime(photo.c_photo().vdate.v, '_', '_', '_'))
				: "Empty")
			+ ".jpg";
		result.list.push_back(ParsePhoto(photo, suggestedPath));
	}
	return result;
}

User ParseUser(const MTPUser &data) {
	auto result = User();
	switch (data.type()) {
	case mtpc_user: {
		const auto &fields = data.c_user();
		result.id = fields.vid.v;
		if (fields.has_first_name()) {
			result.firstName = ParseString(fields.vfirst_name);
		}
		if (fields.has_last_name()) {
			result.lastName = ParseString(fields.vlast_name);
		}
		if (fields.has_phone()) {
			result.phoneNumber = ParseString(fields.vphone);
		}
		if (fields.has_username()) {
			result.username = ParseString(fields.vusername);
		}
		if (fields.has_access_hash()) {
			result.input = MTP_inputUser(fields.vid, fields.vaccess_hash);
		} else {
			result.input = MTP_inputUserEmpty();
		}
	} break;

	case mtpc_userEmpty: {
		const auto &fields = data.c_userEmpty();
		result.id = fields.vid.v;
		result.input = MTP_inputUserEmpty();
	} break;

	default: Unexpected("Type in ParseUser.");
	}
	return result;
}

std::map<int32, User> ParseUsersList(const MTPVector<MTPUser> &data) {
	auto result = std::map<int32, User>();
	for (const auto &user : data.v) {
		auto parsed = ParseUser(user);
		result.emplace(parsed.id, std::move(parsed));
	}
	return result;
}

Chat ParseChat(const MTPChat &data) {
	auto result = Chat();
	switch (data.type()) {
	case mtpc_chat: {
		const auto &fields = data.c_chat();
		result.id = fields.vid.v;
		result.title = ParseString(fields.vtitle);
		result.input = MTP_inputPeerChat(MTP_int(result.id));
	} break;

	case mtpc_chatEmpty: {
		const auto &fields = data.c_chatEmpty();
		result.id = fields.vid.v;
		result.input = MTP_inputPeerChat(MTP_int(result.id));
	} break;

	case mtpc_chatForbidden: {
		const auto &fields = data.c_chatForbidden();
		result.id = fields.vid.v;
		result.title = ParseString(fields.vtitle);
		result.input = MTP_inputPeerChat(MTP_int(result.id));
	} break;

	case mtpc_channel: {
		const auto &fields = data.c_channel();
		result.id = fields.vid.v;
		result.broadcast = fields.is_broadcast();
		result.title = ParseString(fields.vtitle);
		if (fields.has_username()) {
			result.username = ParseString(fields.vusername);
		}
		result.input = MTP_inputPeerChannel(
			MTP_int(result.id),
			fields.vaccess_hash);
	} break;

	case mtpc_channelForbidden: {
		const auto &fields = data.c_channelForbidden();
		result.id = fields.vid.v;
		result.broadcast = fields.is_broadcast();
		result.title = ParseString(fields.vtitle);
		result.input = MTP_inputPeerChannel(
			MTP_int(result.id),
			fields.vaccess_hash);
	} break;

	default: Unexpected("Type in ParseChat.");
	}
	return result;
}

std::map<int32, Chat> ParseChatsList(const MTPVector<MTPChat> &data) {
	auto result = std::map<int32, Chat>();
	for (const auto &chat : data.v) {
		auto parsed = ParseChat(chat);
		result.emplace(parsed.id, std::move(parsed));
	}
	return result;
}

const User *Peer::user() const {
	return base::get_if<User>(&data);
}
const Chat *Peer::chat() const {
	return base::get_if<Chat>(&data);
}

PeerId Peer::id() const {
	if (const auto user = this->user()) {
		return UserPeerId(user->id);
	} else if (const auto chat = this->chat()) {
		return ChatPeerId(chat->id);
	}
	Unexpected("Variant in Peer::id.");
}

Utf8String Peer::name() const {
	if (const auto user = this->user()) {
		return user->firstName + ' ' + user->lastName;
	} else if (const auto chat = this->chat()) {
		return chat->title;
	}
	Unexpected("Variant in Peer::id.");
}

MTPInputPeer Peer::input() const {
	if (const auto user = this->user()) {
		if (user->input.type() == mtpc_inputUser) {
			const auto &input = user->input.c_inputUser();
			return MTP_inputPeerUser(input.vuser_id, input.vaccess_hash);
		}
		return MTP_inputPeerEmpty();
	} else if (const auto chat = this->chat()) {
		return chat->input;
	}
	Unexpected("Variant in Peer::id.");
}

std::map<PeerId, Peer> ParsePeersLists(
		const MTPVector<MTPUser> &users,
		const MTPVector<MTPChat> &chats) {
	auto result = std::map<PeerId, Peer>();
	for (const auto &user : users.v) {
		auto parsed = ParseUser(user);
		result.emplace(UserPeerId(parsed.id), Peer{ std::move(parsed) });
	}
	for (const auto &chat : chats.v) {
		auto parsed = ParseChat(chat);
		result.emplace(ChatPeerId(parsed.id), Peer{ std::move(parsed) });
	}
	return result;
}

Message ParseMessage(const MTPMessage &data) {
	auto result = Message();
	switch (data.type()) {
	case mtpc_message: {
		const auto &fields = data.c_message();
		result.id = fields.vid.v;
		result.date = fields.vdate.v;
	} break;

	case mtpc_messageService: {
		const auto &fields = data.c_messageService();
		result.id = fields.vid.v;
		result.date = fields.vdate.v;
	} break;

	case mtpc_messageEmpty: {
		const auto &fields = data.c_messageEmpty();
		result.id = fields.vid.v;
	} break;
	}
	return result;
}

std::map<int32, Message> ParseMessagesList(
		const MTPVector<MTPMessage> &data) {
	auto result = std::map<int32, Message>();
	for (const auto &message : data.v) {
		auto parsed = ParseMessage(message);
		result.emplace(parsed.id, std::move(parsed));
	}
	return result;
}

PersonalInfo ParsePersonalInfo(const MTPUserFull &data) {
	Expects(data.type() == mtpc_userFull);

	const auto &fields = data.c_userFull();
	auto result = PersonalInfo();
	result.user = ParseUser(fields.vuser);
	if (fields.has_about()) {
		result.bio = ParseString(fields.vabout);
	}
	return result;
}

ContactsList ParseContactsList(const MTPcontacts_Contacts &data) {
	Expects(data.type() == mtpc_contacts_contacts);

	auto result = ContactsList();
	const auto &contacts = data.c_contacts_contacts();
	const auto map = ParseUsersList(contacts.vusers);
	result.list.reserve(contacts.vcontacts.v.size());
	for (const auto &contact : contacts.vcontacts.v) {
		const auto userId = contact.c_contact().vuser_id.v;
		if (const auto i = map.find(userId); i != end(map)) {
			result.list.push_back(i->second);
		} else {
			result.list.push_back(User());
		}
	}
	return result;
}

std::vector<int> SortedContactsIndices(const ContactsList &data) {
	const auto names = ranges::view::all(
		data.list
	) | ranges::view::transform([](const Data::User &user) {
		return (QString::fromUtf8(user.firstName)
			+ ' '
			+ QString::fromUtf8(user.lastName)).toLower();
	}) | ranges::to_vector;

	auto indices = ranges::view::ints(0, int(data.list.size()))
		| ranges::to_vector;
	ranges::sort(indices, [&](int i, int j) {
		return names[i] < names[j];
	});
	return indices;
}

Session ParseSession(const MTPAuthorization &data) {
	Expects(data.type() == mtpc_authorization);

	const auto &fields = data.c_authorization();
	auto result = Session();
	result.platform = ParseString(fields.vplatform);
	result.deviceModel = ParseString(fields.vdevice_model);
	result.systemVersion = ParseString(fields.vsystem_version);
	result.applicationName = ParseString(fields.vapp_name);
	result.applicationVersion = ParseString(fields.vapp_version);
	result.created = fields.vdate_created.v;
	result.lastActive = fields.vdate_active.v;
	result.ip = ParseString(fields.vip);
	result.country = ParseString(fields.vcountry);
	result.region = ParseString(fields.vregion);
	return result;
}

SessionsList ParseSessionsList(const MTPaccount_Authorizations &data) {
	Expects(data.type() == mtpc_account_authorizations);

	auto result = SessionsList();
	const auto &list = data.c_account_authorizations().vauthorizations.v;
	result.list.reserve(list.size());
	for (const auto &session : list) {
		result.list.push_back(ParseSession(session));
	}
	return result;
}

void AppendParsedDialogs(DialogsInfo &to, const MTPmessages_Dialogs &data) {
//	const auto process = [&](const MTPDmessages_dialogs &data) {
	const auto process = [&](const auto &data) {
		const auto peers = ParsePeersLists(data.vusers, data.vchats);
		const auto messages = ParseMessagesList(data.vmessages);
		to.list.reserve(to.list.size() + data.vdialogs.v.size());
		for (const auto &dialog : data.vdialogs.v) {
			if (dialog.type() != mtpc_dialog) {
				continue;
			}
			const auto &fields = dialog.c_dialog();

			auto info = DialogInfo();
			const auto peerId = ParsePeerId(fields.vpeer);
			const auto peerIt = peers.find(peerId);
			if (peerIt != end(peers)) {
				const auto &peer = peerIt->second;
				info.type = peer.user()
					? DialogInfo::Type::Personal
					: peer.chat()->broadcast
					? DialogInfo::Type::Channel
					: peer.chat()->username.isEmpty()
					? DialogInfo::Type::PrivateGroup
					: DialogInfo::Type::PublicGroup;
				info.name = peer.name();
				info.input = peer.input();
			}
			info.topMessageId = fields.vtop_message.v;
			const auto messageIt = messages.find(info.topMessageId);
			if (messageIt != end(messages)) {
				const auto &message = messageIt->second;
				info.topMessageDate = message.date;
			}
			to.list.push_back(std::move(info));
		}
	};
	switch (data.type()) {
	case mtpc_messages_dialogs:
		process(data.c_messages_dialogs());
		break;

	case mtpc_messages_dialogsSlice:
		process(data.c_messages_dialogsSlice());
		break;

	default: Unexpected("Type in AppendParsedChats.");
	}
}

Utf8String FormatPhoneNumber(const Utf8String &phoneNumber) {
	return phoneNumber.isEmpty()
		? Utf8String()
		: App::formatPhone(QString::fromUtf8(phoneNumber)).toUtf8();
}

} // namespace Data
} // namespace Export
