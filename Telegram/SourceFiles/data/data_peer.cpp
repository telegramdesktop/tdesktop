/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_peer.h"

#include "data/data_user.h"
#include "data/data_chat.h"
#include "data/data_channel.h"
#include "data/data_photo.h"
#include "data/data_folder.h"
#include "data/data_session.h"
#include "lang/lang_keys.h"
#include "observer_peer.h"
#include "apiwrap.h"
#include "boxes/confirm_box.h"
#include "auth_session.h"
#include "core/application.h"
#include "mainwindow.h"
#include "window/window_session_controller.h"
#include "ui/image/image.h"
#include "ui/empty_userpic.h"
#include "ui/text_options.h"
#include "history/history.h"
#include "history/view/history_view_element.h"
#include "history/history_item.h"

namespace {

constexpr auto kUpdateFullPeerTimeout = crl::time(5000); // Not more than once in 5 seconds.
constexpr auto kUserpicSize = 160;

using UpdateFlag = Notify::PeerUpdate::Flag;

} // namespace

namespace Data {

int PeerColorIndex(int32 bareId) {
	const auto index = std::abs(bareId) % 7;
	const int map[] = { 0, 7, 4, 1, 6, 3, 5 };
	return map[index];
}

int PeerColorIndex(PeerId peerId) {
	return PeerColorIndex(peerToBareInt(peerId));
}

style::color PeerUserpicColor(PeerId peerId) {
	const style::color colors[] = {
		st::historyPeer1UserpicBg,
		st::historyPeer2UserpicBg,
		st::historyPeer3UserpicBg,
		st::historyPeer4UserpicBg,
		st::historyPeer5UserpicBg,
		st::historyPeer6UserpicBg,
		st::historyPeer7UserpicBg,
		st::historyPeer8UserpicBg,
	};
	return colors[PeerColorIndex(peerId)];
}

PeerId FakePeerIdForJustName(const QString &name) {
	return peerFromUser(name.isEmpty()
		? 777
		: hashCrc32(name.constData(), name.size() * sizeof(QChar)));
}

} // namespace Data

PeerClickHandler::PeerClickHandler(not_null<PeerData*> peer)
: _peer(peer) {
}

void PeerClickHandler::onClick(ClickContext context) const {
	if (context.button == Qt::LeftButton && App::wnd()) {
		const auto controller = App::wnd()->sessionController();
		if (_peer
			&& _peer->isChannel()
			&& controller->activeChatCurrent().peer() != _peer) {
			if (!_peer->asChannel()->isPublic() && !_peer->asChannel()->amIn()) {
				Ui::show(Box<InformBox>(lang(_peer->isMegagroup()
					? lng_group_not_accessible
					: lng_channel_not_accessible)));
			} else {
				controller->showPeerHistory(
					_peer,
					Window::SectionShow::Way::Forward);
			}
		} else {
			Ui::showPeerProfile(_peer);
		}
	}
}

PeerData::PeerData(not_null<Data::Session*> owner, PeerId id)
: id(id)
, _owner(owner)
, _userpicEmpty(createEmptyUserpic()) {
	nameText.setText(st::msgNameStyle, QString(), Ui::NameTextOptions());
}

Data::Session &PeerData::owner() const {
	return *_owner;
}

AuthSession &PeerData::session() const {
	return _owner->session();
}

Main::Account &PeerData::account() const {
	return session().account();
}

void PeerData::updateNameDelayed(
		const QString &newName,
		const QString &newNameOrPhone,
		const QString &newUsername) {
	if (name == newName && nameVersion > 1) {
		if (isUser()) {
			if (asUser()->nameOrPhone == newNameOrPhone
				&& asUser()->username == newUsername) {
				return;
			}
		} else if (isChannel()) {
			if (asChannel()->username == newUsername) {
				return;
			}
		} else if (isChat()) {
			return;
		}
	}
	name = newName;
	nameText.setText(st::msgNameStyle, name, Ui::NameTextOptions());
	refreshEmptyUserpic();
	Notify::PeerUpdate update(this);
	if (nameVersion++ > 1) {
		update.flags |= UpdateFlag::NameChanged;
		update.oldNameFirstLetters = nameFirstLetters();
	}
	if (isUser()) {
		if (asUser()->username != newUsername) {
			asUser()->username = newUsername;
			update.flags |= UpdateFlag::UsernameChanged;
		}
		asUser()->setNameOrPhone(newNameOrPhone);
	} else if (isChannel()) {
		if (asChannel()->username != newUsername) {
			asChannel()->username = newUsername;
			if (newUsername.isEmpty()) {
				asChannel()->removeFlags(
					MTPDchannel::Flag::f_username);
			} else {
				asChannel()->addFlags(MTPDchannel::Flag::f_username);
			}
			update.flags |= UpdateFlag::UsernameChanged;
		}
	}
	fillNames();
	if (update.flags) {
		Notify::PeerUpdated().notify(update, true);
	}
}

std::unique_ptr<Ui::EmptyUserpic> PeerData::createEmptyUserpic() const {
	return std::make_unique<Ui::EmptyUserpic>(
		Data::PeerUserpicColor(id),
		name);
}

void PeerData::refreshEmptyUserpic() const {
	_userpicEmpty = useEmptyUserpic() ? createEmptyUserpic() : nullptr;
}

ClickHandlerPtr PeerData::createOpenLink() {
	return std::make_shared<PeerClickHandler>(this);
}

void PeerData::setUserpic(
		PhotoId photoId,
		const StorageImageLocation &location,
		ImagePtr userpic) {
	_userpicPhotoId = photoId;
	_userpic = userpic;
	_userpicLocation = location;
	refreshEmptyUserpic();
}

void PeerData::setUserpicPhoto(const MTPPhoto &data) {
	const auto photoId = data.match([&](const MTPDphoto &data) {
		const auto photo = owner().processPhoto(data);
		photo->peer = this;
		return photo->id;
	}, [](const MTPDphotoEmpty &data) {
		return PhotoId(0);
	});
	if (_userpicPhotoId != photoId) {
		_userpicPhotoId = photoId;
		Notify::peerUpdatedDelayed(this, UpdateFlag::PhotoChanged);
	}
}

ImagePtr PeerData::currentUserpic() const {
	if (_userpic) {
		_userpic->load(userpicOrigin());
		if (_userpic->loaded()) {
			if (!useEmptyUserpic()) {
				_userpicEmpty = nullptr;
			}
			return _userpic;
		}
	}
	if (!_userpicEmpty) {
		refreshEmptyUserpic();
	}
	return ImagePtr();
}

void PeerData::paintUserpic(Painter &p, int x, int y, int size) const {
	if (auto userpic = currentUserpic()) {
		p.drawPixmap(x, y, userpic->pixCircled(userpicOrigin(), size, size));
	} else {
		_userpicEmpty->paint(p, x, y, x + size + x, size);
	}
}

void PeerData::paintUserpicRounded(Painter &p, int x, int y, int size) const {
	if (auto userpic = currentUserpic()) {
		p.drawPixmap(x, y, userpic->pixRounded(userpicOrigin(), size, size, ImageRoundRadius::Small));
	} else {
		_userpicEmpty->paintRounded(p, x, y, x + size + x, size);
	}
}

void PeerData::paintUserpicSquare(Painter &p, int x, int y, int size) const {
	if (auto userpic = currentUserpic()) {
		p.drawPixmap(x, y, userpic->pix(userpicOrigin(), size, size));
	} else {
		_userpicEmpty->paintSquare(p, x, y, x + size + x, size);
	}
}

void PeerData::loadUserpic() {
	_userpic->load(userpicOrigin());
}

bool PeerData::userpicLoaded() const {
	return _userpic->loaded();
}

bool PeerData::useEmptyUserpic() const {
	return !_userpicLocation.valid()
		|| !_userpic
		|| !_userpic->loaded();
}

InMemoryKey PeerData::userpicUniqueKey() const {
	if (useEmptyUserpic()) {
		if (!_userpicEmpty) {
			refreshEmptyUserpic();
		}
		return _userpicEmpty->uniqueKey();
	}
	return inMemoryKey(_userpicLocation);
}

void PeerData::saveUserpic(const QString &path, int size) const {
	genUserpic(size).save(path, "PNG");
}

void PeerData::saveUserpicRounded(const QString &path, int size) const {
	genUserpicRounded(size).save(path, "PNG");
}

QPixmap PeerData::genUserpic(int size) const {
	if (auto userpic = currentUserpic()) {
		return userpic->pixCircled(userpicOrigin(), size, size);
	}
	auto result = QImage(QSize(size, size) * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(cRetinaFactor());
	result.fill(Qt::transparent);
	{
		Painter p(&result);
		paintUserpic(p, 0, 0, size);
	}
	return App::pixmapFromImageInPlace(std::move(result));
}

QPixmap PeerData::genUserpicRounded(int size) const {
	if (auto userpic = currentUserpic()) {
		return userpic->pixRounded(userpicOrigin(), size, size, ImageRoundRadius::Small);
	}
	auto result = QImage(QSize(size, size) * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(cRetinaFactor());
	result.fill(Qt::transparent);
	{
		Painter p(&result);
		paintUserpicRounded(p, 0, 0, size);
	}
	return App::pixmapFromImageInPlace(std::move(result));
}

Data::FileOrigin PeerData::userpicOrigin() const {
	return Data::FileOriginPeerPhoto(id);
}

Data::FileOrigin PeerData::userpicPhotoOrigin() const {
	return (isUser() && userpicPhotoId())
		? Data::FileOriginUserPhoto(bareId(), userpicPhotoId())
		: Data::FileOrigin();
}

void PeerData::updateUserpic(
		PhotoId photoId,
		MTP::DcId dcId,
		const MTPFileLocation &location) {
	const auto size = kUserpicSize;
	const auto loc = location.match([&](
			const MTPDfileLocationToBeDeprecated &deprecated) {
		return StorageImageLocation(
			StorageFileLocation(
				dcId,
				isSelf() ? peerToUser(id) : 0,
				MTP_inputPeerPhotoFileLocation(
					MTP_flags(0),
					input,
					deprecated.vvolume_id,
					deprecated.vlocal_id)),
			size,
			size);
	});
	setUserpicChecked(photoId, loc, Images::Create(loc));
}

void PeerData::clearUserpic() {
	const auto photoId = PhotoId(0);
	const auto loc = StorageImageLocation();
	const auto photo = [&] {
		if (isNotificationsUser()) {
			auto image = Core::App().logoNoMargin().scaledToWidth(
				kUserpicSize,
				Qt::SmoothTransformation);
			return _userpic
				? _userpic
				: Images::Create(std::move(image), "PNG");
		}
		return ImagePtr();
	}();
	setUserpicChecked(photoId, loc, photo);
}

void PeerData::setUserpicChecked(
		PhotoId photoId,
		const StorageImageLocation &location,
		ImagePtr userpic) {
	if (_userpicPhotoId != photoId
		|| _userpic.get() != userpic.get()
		|| _userpicLocation != location) {
		setUserpic(photoId, location, userpic);
		Notify::peerUpdatedDelayed(this, UpdateFlag::PhotoChanged);
		//if (const auto channel = asChannel()) { // #feed
		//	if (const auto feed = channel->feed()) {
		//		owner().notifyFeedUpdated(
		//			feed,
		//			Data::FeedUpdateFlag::ChannelPhoto);
		//	}
		//}
	}
}

bool PeerData::canPinMessages() const {
	if (const auto user = asUser()) {
		return user->fullFlags() & MTPDuserFull::Flag::f_can_pin_message;
	} else if (const auto chat = asChat()) {
		return chat->amIn() && !chat->amRestricted(ChatRestriction::f_pin_messages);
	} else if (const auto channel = asChannel()) {
		return channel->isMegagroup()
			? !channel->amRestricted(ChatRestriction::f_pin_messages)
			: ((channel->adminRights() & ChatAdminRight::f_edit_messages)
				|| channel->amCreator());
	}
	Unexpected("Peer type in PeerData::canPinMessages.");
}

void PeerData::setPinnedMessageId(MsgId messageId) {
	const auto min = [&] {
		if (const auto channel = asChannel()) {
			return channel->availableMinId();
		}
		return MsgId(0);
	}();
	messageId = (messageId > min) ? messageId : MsgId(0);
	if (_pinnedMessageId != messageId) {
		_pinnedMessageId = messageId;
		Notify::peerUpdatedDelayed(
			this,
			Notify::PeerUpdate::Flag::PinnedMessageChanged);
	}
}

bool PeerData::canExportChatHistory() const {
	for (const auto &block : _owner->history(id)->blocks) {
		for (const auto &message : block->messages) {
			if (!message->data()->serviceMsg()) {
				return true;
			}
		}
	}
	return false;
}

bool PeerData::setAbout(const QString &newAbout) {
	if (_about == newAbout) {
		return false;
	}
	_about = newAbout;
	Notify::peerUpdatedDelayed(this, UpdateFlag::AboutChanged);
	return true;
}

void PeerData::checkFolder(FolderId folderId) {
	const auto folder = folderId
		? owner().folderLoaded(folderId)
		: nullptr;
	if (const auto history = owner().historyLoaded(this)) {
		if (folder && history->folder() != folder) {
			session().api().requestDialogEntry(history);
		}
	}
}

void PeerData::fillNames() {
	_nameWords.clear();
	_nameFirstLetters.clear();
	auto toIndexList = QStringList();
	auto appendToIndex = [&](const QString &value) {
		if (!value.isEmpty()) {
			toIndexList.push_back(TextUtilities::RemoveAccents(value));
		}
	};

	appendToIndex(name);
	const auto appendTranslit = !toIndexList.isEmpty()
		&& cRussianLetters().match(toIndexList.front()).hasMatch();
	if (appendTranslit) {
		appendToIndex(translitRusEng(toIndexList.front()));
	}
	if (const auto user = asUser()) {
		if (user->nameOrPhone != name) {
			appendToIndex(user->nameOrPhone);
		}
		appendToIndex(user->username);
		if (isSelf()) {
			const auto english = qsl("Saved messages");
			const auto localized = lang(lng_saved_messages);
			appendToIndex(english);
			if (localized != english) {
				appendToIndex(localized);
			}
		}
	} else if (const auto channel = asChannel()) {
		appendToIndex(channel->username);
	}
	auto toIndex = toIndexList.join(' ');
	toIndex += ' ' + rusKeyboardLayoutSwitch(toIndex);

	const auto namesList = TextUtilities::PrepareSearchWords(toIndex);
	for (const auto &name : namesList) {
		_nameWords.insert(name);
		_nameFirstLetters.insert(name[0]);
	}
}

PeerData::~PeerData() = default;

void PeerData::updateFull() {
	if (!_lastFullUpdate
		|| crl::now() > _lastFullUpdate + kUpdateFullPeerTimeout) {
		updateFullForced();
	}
}

void PeerData::updateFullForced() {
	session().api().requestFullPeer(this);
	if (const auto channel = asChannel()) {
		if (!channel->amCreator() && !channel->inviter) {
			session().api().requestSelfParticipant(channel);
		}
	}
}

void PeerData::fullUpdated() {
	_lastFullUpdate = crl::now();
}

UserData *PeerData::asUser() {
	return isUser() ? static_cast<UserData*>(this) : nullptr;
}

const UserData *PeerData::asUser() const {
	return isUser() ? static_cast<const UserData*>(this) : nullptr;
}

ChatData *PeerData::asChat() {
	return isChat() ? static_cast<ChatData*>(this) : nullptr;
}

const ChatData *PeerData::asChat() const {
	return isChat() ? static_cast<const ChatData*>(this) : nullptr;
}

ChannelData *PeerData::asChannel() {
	return isChannel() ? static_cast<ChannelData*>(this) : nullptr;
}

const ChannelData *PeerData::asChannel() const {
	return isChannel()
		? static_cast<const ChannelData*>(this)
		: nullptr;
}

ChannelData *PeerData::asMegagroup() {
	return isMegagroup() ? static_cast<ChannelData*>(this) : nullptr;
}

const ChannelData *PeerData::asMegagroup() const {
	return isMegagroup()
		? static_cast<const ChannelData*>(this)
		: nullptr;
}

ChatData *PeerData::asChatNotMigrated() {
	if (const auto chat = asChat()) {
		return chat->migrateTo() ? nullptr : chat;
	}
	return nullptr;
}

const ChatData *PeerData::asChatNotMigrated() const {
	if (const auto chat = asChat()) {
		return chat->migrateTo() ? nullptr : chat;
	}
	return nullptr;
}

ChannelData *PeerData::asChannelOrMigrated() {
	if (const auto channel = asChannel()) {
		return channel;
	}
	return migrateTo();
}

const ChannelData *PeerData::asChannelOrMigrated() const {
	if (const auto channel = asChannel()) {
		return channel;
	}
	return migrateTo();
}

ChatData *PeerData::migrateFrom() const {
	if (const auto megagroup = asMegagroup()) {
		return megagroup->amIn()
			? megagroup->getMigrateFromChat()
			: nullptr;
	}
	return nullptr;
}

ChannelData *PeerData::migrateTo() const {
	if (const auto chat = asChat()) {
		if (const auto result = chat->getMigrateToChannel()) {
			return result->amIn() ? result : nullptr;
		}
	}
	return nullptr;
}

not_null<PeerData*> PeerData::migrateToOrMe() {
	if (const auto channel = migrateTo()) {
		return channel;
	}
	return this;
}

not_null<const PeerData*> PeerData::migrateToOrMe() const {
	if (const auto channel = migrateTo()) {
		return channel;
	}
	return this;
}

const Text &PeerData::dialogName() const {
	return migrateTo()
		? migrateTo()->dialogName()
		: (isUser() && !asUser()->phoneText.isEmpty())
			? asUser()->phoneText
			: nameText;
}

const QString &PeerData::shortName() const {
	return isUser() ? asUser()->firstName : name;
}

QString PeerData::userName() const {
	return isUser()
		? asUser()->username
		: isChannel()
			? asChannel()->username
			: QString();
}

bool PeerData::isVerified() const {
	return isUser()
		? asUser()->isVerified()
		: isChannel()
			? asChannel()->isVerified()
			: false;
}

bool PeerData::isMegagroup() const {
	return isChannel() ? asChannel()->isMegagroup() : false;
}

bool PeerData::canWrite() const {
	return isChannel()
		? asChannel()->canWrite()
		: isChat()
			? asChat()->canWrite()
			: isUser()
				? asUser()->canWrite()
				: false;
}

Data::RestrictionCheckResult PeerData::amRestricted(
		ChatRestriction right) const {
	using Result = Data::RestrictionCheckResult;
	const auto allowByAdminRights = [](auto right, auto chat) -> bool {
		if (right == ChatRestriction::f_invite_users) {
			return chat->adminRights() & ChatAdminRight::f_invite_users;
		} else if (right == ChatRestriction::f_change_info) {
			return chat->adminRights() & ChatAdminRight::f_change_info;
		} else if (right == ChatRestriction::f_pin_messages) {
			return chat->adminRights() & ChatAdminRight::f_pin_messages;
		} else {
			return chat->hasAdminRights();
		}
	};
	if (const auto channel = asChannel()) {
		const auto defaultRestrictions = channel->defaultRestrictions()
			| (channel->isPublic()
				? (ChatRestriction::f_pin_messages | ChatRestriction::f_change_info)
				: ChatRestrictions(0));
		return (channel->amCreator() || allowByAdminRights(right, channel))
			? Result::Allowed()
			: (defaultRestrictions & right)
			? Result::WithEveryone()
			: (channel->restrictions() & right)
			? Result::Explicit()
			: Result::Allowed();
	} else if (const auto chat = asChat()) {
		return (chat->amCreator() || allowByAdminRights(right, chat))
			? Result::Allowed()
			: (chat->defaultRestrictions() & right)
			? Result::WithEveryone()
			: Result::Allowed();
	}
	return Result::Allowed();
}

bool PeerData::canRevokeFullHistory() const {
	return isUser()
		&& Global::RevokePrivateInbox()
		&& (Global::RevokePrivateTimeLimit() == 0x7FFFFFFF);
}

namespace Data {

std::vector<ChatRestrictions> ListOfRestrictions() {
	using Flag = ChatRestriction;

	return {
		Flag::f_send_messages,
		Flag::f_send_media,
		Flag::f_send_stickers
		| Flag::f_send_gifs
		| Flag::f_send_games
		| Flag::f_send_inline,
		Flag::f_embed_links,
		Flag::f_send_polls,
		Flag::f_invite_users,
		Flag::f_pin_messages,
		Flag::f_change_info,
	};
}

std::optional<LangKey> RestrictionErrorKey(
		not_null<PeerData*> peer,
		ChatRestriction restriction) {
	using Flag = ChatRestriction;
	if (const auto restricted = peer->amRestricted(restriction)) {
		const auto all = restricted.isWithEveryone();
		switch (restriction) {
		case Flag::f_send_polls:
			return all
				? lng_restricted_send_polls_all
				: lng_restricted_send_polls;
		case Flag::f_send_messages:
			return all
				? lng_restricted_send_message_all
				: lng_restricted_send_message;
		case Flag::f_send_media:
			return all
				? lng_restricted_send_media_all
				: lng_restricted_send_media;
		case Flag::f_send_stickers:
			return all
				? lng_restricted_send_stickers_all
				: lng_restricted_send_stickers;
		case Flag::f_send_gifs:
			return all
				? lng_restricted_send_gifs_all
				: lng_restricted_send_gifs;
		case Flag::f_send_inline:
		case Flag::f_send_games:
			return all
				? lng_restricted_send_inline_all
				: lng_restricted_send_inline;
		}
		Unexpected("Restriction in Data::RestrictionErrorKey.");
	}
	return std::nullopt;
}

} // namespace Data
