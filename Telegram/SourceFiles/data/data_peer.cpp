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
#include "data/data_changes.h"
#include "data/data_photo.h"
#include "data/data_folder.h"
#include "data/data_session.h"
#include "data/data_file_origin.h"
#include "data/data_histories.h"
#include "base/unixtime.h"
#include "base/crc32hash.h"
#include "lang/lang_keys.h"
#include "apiwrap.h"
#include "boxes/confirm_box.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_app_config.h"
#include "mtproto/mtproto_config.h"
#include "core/application.h"
#include "mainwindow.h"
#include "window/window_session_controller.h"
#include "ui/image/image.h"
#include "ui/empty_userpic.h"
#include "ui/text/text_options.h"
#include "history/history.h"
#include "history/view/history_view_element.h"
#include "history/history_item.h"
#include "storage/file_download.h"
#include "storage/storage_facade.h"
#include "storage/storage_shared_media.h"
#include "facades.h" // Ui::showPeerProfile
#include "app.h"

namespace {

constexpr auto kUpdateFullPeerTimeout = crl::time(5000); // Not more than once in 5 seconds.
constexpr auto kUserpicSize = 160;

using UpdateFlag = Data::PeerUpdate::Flag;

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
		: base::crc32(name.constData(), name.size() * sizeof(QChar)));
}

} // namespace Data

PeerClickHandler::PeerClickHandler(not_null<PeerData*> peer)
: _peer(peer) {
}

void PeerClickHandler::onClick(ClickContext context) const {
	if (context.button != Qt::LeftButton) {
		return;
	}
	const auto &windows = _peer->session().windows();
	if (windows.empty()) {
		Core::App().domain().activate(&_peer->session().account());
		if (windows.empty()) {
			return;
		}
	}
	const auto window = windows.front();
	const auto currentPeer = window->activeChatCurrent().peer();
	if (_peer && _peer->isChannel() && currentPeer != _peer) {
		const auto clickedChannel = _peer->asChannel();
		if (!clickedChannel->isPublic()
			&& !clickedChannel->amIn()
			&& (!currentPeer->isChannel()
				|| currentPeer->asChannel()->linkedChat() != clickedChannel)) {
			Ui::show(Box<InformBox>(_peer->isMegagroup()
				? tr::lng_group_not_accessible(tr::now)
				: tr::lng_channel_not_accessible(tr::now)));
		} else {
			window->showPeerHistory(
				_peer,
				Window::SectionShow::Way::Forward);
		}
	} else {
		Ui::showPeerProfile(_peer);
	}
}

PeerData::PeerData(not_null<Data::Session*> owner, PeerId id)
: id(id)
, _owner(owner) {
	_nameText.setText(st::msgNameStyle, QString(), Ui::NameTextOptions());
}

Data::Session &PeerData::owner() const {
	return *_owner;
}

Main::Session &PeerData::session() const {
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
	_nameText.setText(st::msgNameStyle, name, Ui::NameTextOptions());
	_userpicEmpty = nullptr;

	auto flags = UpdateFlag::None | UpdateFlag::None;
	auto oldFirstLetters = base::flat_set<QChar>();
	const auto nameUpdated = (nameVersion++ > 1);
	if (nameUpdated) {
		oldFirstLetters = nameFirstLetters();
		flags |= UpdateFlag::Name;
	}
	if (isUser()) {
		if (asUser()->username != newUsername) {
			asUser()->username = newUsername;
			flags |= UpdateFlag::Username;
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
			flags |= UpdateFlag::Username;
		}
	}
	fillNames();
	if (nameUpdated) {
		session().changes().nameUpdated(this, std::move(oldFirstLetters));
	}
	if (flags) {
		session().changes().peerUpdated(this, flags);
	}
}

not_null<Ui::EmptyUserpic*> PeerData::ensureEmptyUserpic() const {
	if (!_userpicEmpty) {
		_userpicEmpty = std::make_unique<Ui::EmptyUserpic>(
			Data::PeerUserpicColor(id),
			name);
	}
	return _userpicEmpty.get();
}

ClickHandlerPtr PeerData::createOpenLink() {
	return std::make_shared<PeerClickHandler>(this);
}

void PeerData::setUserpic(PhotoId photoId, const ImageLocation &location) {
	_userpicPhotoId = photoId;
	_userpic.set(&session(), ImageWithLocation{ .location = location });
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
		session().changes().peerUpdated(this, UpdateFlag::Photo);
	}
}

Image *PeerData::currentUserpic(
		std::shared_ptr<Data::CloudImageView> &view) const {
	if (!_userpic.isCurrentView(view)) {
		view = _userpic.createView();
		_userpic.load(&session(), userpicOrigin());
	}
	const auto image = view ? view->image() : nullptr;
	if (image) {
		_userpicEmpty = nullptr;
	} else if (isNotificationsUser()) {
		static auto result = Image(
			Core::App().logoNoMargin().scaledToWidth(
				kUserpicSize,
				Qt::SmoothTransformation));
		return &result;
	}
	return image;
}

void PeerData::paintUserpic(
		Painter &p,
		std::shared_ptr<Data::CloudImageView> &view,
		int x,
		int y,
		int size) const {
	if (const auto userpic = currentUserpic(view)) {
		p.drawPixmap(x, y, userpic->pixCircled(size, size));
	} else {
		ensureEmptyUserpic()->paint(p, x, y, x + size + x, size);
	}
}

void PeerData::paintUserpicRounded(
		Painter &p,
		std::shared_ptr<Data::CloudImageView> &view,
		int x,
		int y,
		int size) const {
	if (const auto userpic = currentUserpic(view)) {
		p.drawPixmap(x, y, userpic->pixRounded(size, size, ImageRoundRadius::Small));
	} else {
		ensureEmptyUserpic()->paintRounded(p, x, y, x + size + x, size);
	}
}

void PeerData::paintUserpicSquare(
		Painter &p,
		std::shared_ptr<Data::CloudImageView> &view,
		int x,
		int y,
		int size) const {
	if (const auto userpic = currentUserpic(view)) {
		p.drawPixmap(x, y, userpic->pix(size, size));
	} else {
		ensureEmptyUserpic()->paintSquare(p, x, y, x + size + x, size);
	}
}

void PeerData::loadUserpic() {
	_userpic.load(&session(), userpicOrigin());
}

bool PeerData::hasUserpic() const {
	return !_userpic.empty();
}

std::shared_ptr<Data::CloudImageView> PeerData::activeUserpicView() {
	return _userpic.empty() ? nullptr : _userpic.activeView();
}

std::shared_ptr<Data::CloudImageView> PeerData::createUserpicView() {
	if (_userpic.empty()) {
		return nullptr;
	}
	auto result = _userpic.createView();
	_userpic.load(&session(), userpicPhotoOrigin());
	return result;
}

bool PeerData::useEmptyUserpic(
		std::shared_ptr<Data::CloudImageView> &view) const {
	return !currentUserpic(view);
}

InMemoryKey PeerData::userpicUniqueKey(
		std::shared_ptr<Data::CloudImageView> &view) const {
	return useEmptyUserpic(view)
		? ensureEmptyUserpic()->uniqueKey()
		: inMemoryKey(_userpic.location());
}

void PeerData::saveUserpic(
		std::shared_ptr<Data::CloudImageView> &view,
		const QString &path,
		int size) const {
	genUserpic(view, size).save(path, "PNG");
}

void PeerData::saveUserpicRounded(
		std::shared_ptr<Data::CloudImageView> &view,
		const QString &path,
		int size) const {
	genUserpicRounded(view, size).save(path, "PNG");
}

QPixmap PeerData::genUserpic(
		std::shared_ptr<Data::CloudImageView> &view,
		int size) const {
	if (const auto userpic = currentUserpic(view)) {
		return userpic->pixCircled(size, size);
	}
	auto result = QImage(QSize(size, size) * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(cRetinaFactor());
	result.fill(Qt::transparent);
	{
		Painter p(&result);
		paintUserpic(p, view, 0, 0, size);
	}
	return App::pixmapFromImageInPlace(std::move(result));
}

QPixmap PeerData::genUserpicRounded(
		std::shared_ptr<Data::CloudImageView> &view,
		int size) const {
	if (const auto userpic = currentUserpic(view)) {
		return userpic->pixRounded(size, size, ImageRoundRadius::Small);
	}
	auto result = QImage(QSize(size, size) * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(cRetinaFactor());
	result.fill(Qt::transparent);
	{
		Painter p(&result);
		paintUserpicRounded(p, view, 0, 0, size);
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
	setUserpicChecked(photoId, location.match([&](
			const MTPDfileLocationToBeDeprecated &deprecated) {
		return ImageLocation(
			{ StorageFileLocation(
				dcId,
				isSelf() ? peerToUser(id) : 0,
				MTP_inputPeerPhotoFileLocation(
					MTP_flags(0),
					input,
					deprecated.vvolume_id(),
					deprecated.vlocal_id())) },
			kUserpicSize,
			kUserpicSize);
	}));
}

void PeerData::clearUserpic() {
	setUserpicChecked(PhotoId(), ImageLocation());
}

void PeerData::setUserpicChecked(
		PhotoId photoId,
		const ImageLocation &location) {
	if (_userpicPhotoId != photoId || _userpic.location() != location) {
		setUserpic(photoId, location);
		session().changes().peerUpdated(this, UpdateFlag::Photo);
		//if (const auto channel = asChannel()) { // #feed
		//	if (const auto feed = channel->feed()) {
		//		owner().notifyFeedUpdated(
		//			feed,
		//			Data::FeedUpdateFlag::ChannelPhoto);
		//	}
		//}
	}
}

auto PeerData::unavailableReasons() const
-> const std::vector<Data::UnavailableReason> & {
	static const auto result = std::vector<Data::UnavailableReason>();
	return result;
}

QString PeerData::computeUnavailableReason() const {
	const auto &list = unavailableReasons();
	const auto &config = session().account().appConfig();
	const auto skip = config.get<std::vector<QString>>(
		"ignore_restriction_reasons",
		std::vector<QString>());
	auto &&filtered = ranges::view::all(
		list
	) | ranges::view::filter([&](const Data::UnavailableReason &reason) {
		return !ranges::contains(skip, reason.reason);
	});
	const auto first = filtered.begin();
	return (first != filtered.end()) ? first->text : QString();
}

// This is duplicated in CanPinMessagesValue().
bool PeerData::canPinMessages() const {
	if (const auto user = asUser()) {
		return user->fullFlags() & MTPDuserFull::Flag::f_can_pin_message;
	} else if (const auto chat = asChat()) {
		return chat->amIn()
			&& !chat->amRestricted(ChatRestriction::f_pin_messages);
	} else if (const auto channel = asChannel()) {
		return channel->isMegagroup()
			? !channel->amRestricted(ChatRestriction::f_pin_messages)
			: ((channel->adminRights() & ChatAdminRight::f_edit_messages)
				|| channel->amCreator());
	}
	Unexpected("Peer type in PeerData::canPinMessages.");
}

bool PeerData::canEditMessagesIndefinitely() const {
	if (const auto user = asUser()) {
		return user->isSelf();
	} else if (const auto chat = asChat()) {
		return false;
	} else if (const auto channel = asChannel()) {
		return channel->isMegagroup()
			? channel->canPinMessages()
			: channel->canEditMessages();
	}
	Unexpected("Peer type in PeerData::canEditMessagesIndefinitely.");
}

bool PeerData::hasPinnedMessages() const {
	return _hasPinnedMessages;
}

void PeerData::setHasPinnedMessages(bool has) {
	_hasPinnedMessages = has;
	session().changes().peerUpdated(this, UpdateFlag::PinnedMessages);
}

bool PeerData::canExportChatHistory() const {
	if (isRepliesChat()) {
		return false;
	}
	if (const auto channel = asChannel()) {
		if (!channel->amIn() && channel->invitePeekExpires()) {
			return false;
		}
	}
	for (const auto &block : _owner->history(id)->blocks) {
		for (const auto &message : block->messages) {
			if (!message->data()->serviceMsg()) {
				return true;
			}
		}
	}
	if (const auto from = migrateFrom()) {
		return from->canExportChatHistory();
	}
	return false;
}

bool PeerData::setAbout(const QString &newAbout) {
	if (_about == newAbout) {
		return false;
	}
	_about = newAbout;
	session().changes().peerUpdated(this, UpdateFlag::About);
	return true;
}

void PeerData::checkFolder(FolderId folderId) {
	const auto folder = folderId
		? owner().folderLoaded(folderId)
		: nullptr;
	if (const auto history = owner().historyLoaded(this)) {
		if (folder && history->folder() != folder) {
			owner().histories().requestDialogEntry(history);
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
			const auto localized = tr::lng_saved_messages(tr::now);
			appendToIndex(english);
			if (localized != english) {
				appendToIndex(localized);
			}
		} else if (isRepliesChat()) {
			const auto english = qsl("Replies");
			const auto localized = tr::lng_replies_messages(tr::now);
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

ChannelData *PeerData::asBroadcast() {
	return isBroadcast() ? static_cast<ChannelData*>(this) : nullptr;
}

const ChannelData *PeerData::asBroadcast() const {
	return isBroadcast()
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

const Ui::Text::String &PeerData::topBarNameText() const {
	if (const auto to = migrateTo()) {
		return to->topBarNameText();
	} else if (const auto user = asUser()) {
		if (!user->phoneText.isEmpty()) {
			return user->phoneText;
		}
	}
	return _nameText;
}

const Ui::Text::String &PeerData::nameText() const {
	if (const auto to = migrateTo()) {
		return to->nameText();
	}
	return _nameText;
}

const QString &PeerData::shortName() const {
	if (const auto user = asUser()) {
		return user->firstName.isEmpty() ? user->lastName : user->firstName;
	}
	return name;
}

QString PeerData::userName() const {
	if (const auto user = asUser()) {
		return user->username;
	} else if (const auto channel = asChannel()) {
		return channel->username;
	}
	return QString();
}

bool PeerData::isVerified() const {
	if (const auto user = asUser()) {
		return user->isVerified();
	} else if (const auto channel = asChannel()) {
		return channel->isVerified();
	}
	return false;
}

bool PeerData::isScam() const {
	if (const auto user = asUser()) {
		return user->isScam();
	} else if (const auto channel = asChannel()) {
		return channel->isScam();
	}
	return false;
}

bool PeerData::isMegagroup() const {
	return isChannel() ? asChannel()->isMegagroup() : false;
}

bool PeerData::isBroadcast() const {
	return isChannel() ? asChannel()->isBroadcast() : false;
}

bool PeerData::isRepliesChat() const {
	constexpr auto kProductionId = peerFromUser(1271266957);
	constexpr auto kTestId = peerFromUser(708513);
	if (id != kTestId && id != kProductionId) {
		return false;
	}
	return ((session().mtp().environment() == MTP::Environment::Production)
		? kProductionId
		: kTestId) == id;
}

bool PeerData::canWrite() const {
	if (const auto user = asUser()) {
		return user->canWrite();
	} else if (const auto channel = asChannel()) {
		return channel->canWrite();
	} else if (const auto chat = asChat()) {
		return chat->canWrite();
	}
	return false;
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

bool PeerData::amAnonymous() const {
	return isBroadcast()
		|| (isChannel()
			&& (asChannel()->adminRights() & ChatAdminRight::f_anonymous));
}

bool PeerData::canRevokeFullHistory() const {
	if (const auto user = asUser()) {
		return !isSelf()
			&& (!user->isBot() || user->isSupport())
			&& session().serverConfig().revokePrivateInbox
			&& (session().serverConfig().revokePrivateTimeLimit == 0x7FFFFFFF);
	}
	return false;
}

bool PeerData::slowmodeApplied() const {
	if (const auto channel = asChannel()) {
		return !channel->amCreator()
			&& !channel->hasAdminRights()
			&& (channel->flags() & MTPDchannel::Flag::f_slowmode_enabled);
	}
	return false;
}

rpl::producer<bool> PeerData::slowmodeAppliedValue() const {
	using namespace rpl::mappers;
	const auto channel = asChannel();
	if (!channel) {
		return rpl::single(false);
	}

	auto hasAdminRights = channel->adminRightsValue(
	) | rpl::map([=] {
		return channel->hasAdminRights();
	}) | rpl::distinct_until_changed();

	auto slowmodeEnabled = channel->flagsValue(
	) | rpl::filter([=](const ChannelData::Flags::Change &change) {
		return (change.diff & MTPDchannel::Flag::f_slowmode_enabled) != 0;
	}) | rpl::map([=](const ChannelData::Flags::Change &change) {
		return (change.value & MTPDchannel::Flag::f_slowmode_enabled) != 0;
	}) | rpl::distinct_until_changed();

	return rpl::combine(
		std::move(hasAdminRights),
		std::move(slowmodeEnabled),
		!_1 && _2);
}

int PeerData::slowmodeSecondsLeft() const {
	if (const auto channel = asChannel()) {
		if (const auto seconds = channel->slowmodeSeconds()) {
			if (const auto last = channel->slowmodeLastMessage()) {
				const auto now = base::unixtime::now();
				return std::max(seconds - (now - last), 0);
			}
		}
	}
	return 0;
}

bool PeerData::canSendPolls() const {
	if (const auto user = asUser()) {
		return user->isBot()
			&& !user->isRepliesChat()
			&& !user->isSupport();
	} else if (const auto chat = asChat()) {
		return chat->canSendPolls();
	} else if (const auto channel = asChannel()) {
		return channel->canSendPolls();
	}
	return false;
}

void PeerData::setIsBlocked(bool is) {
	const auto status = is
		? BlockStatus::Blocked
		: BlockStatus::NotBlocked;
	if (_blockStatus != status) {
		_blockStatus = status;
		if (const auto user = asUser()) {
			const auto flags = user->fullFlags();
			if (is) {
				user->setFullFlags(flags | MTPDuserFull::Flag::f_blocked);
			} else {
				user->setFullFlags(flags & ~MTPDuserFull::Flag::f_blocked);
			}
		}
		session().changes().peerUpdated(this, UpdateFlag::IsBlocked);
	}
}

void PeerData::setLoadedStatus(LoadedStatus status) {
	_loadedStatus = status;
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

std::optional<QString> RestrictionError(
		not_null<PeerData*> peer,
		ChatRestriction restriction) {
	using Flag = ChatRestriction;
	if (const auto restricted = peer->amRestricted(restriction)) {
		const auto all = restricted.isWithEveryone();
		const auto channel = peer->asChannel();
		if (!all && channel) {
			auto restrictedUntil = channel->restrictedUntil();
			if (restrictedUntil > 0 && !ChannelData::IsRestrictedForever(restrictedUntil)) {
				auto restrictedUntilDateTime = base::unixtime::parse(channel->restrictedUntil());
				auto date = restrictedUntilDateTime.toString(qsl("dd.MM.yy"));
				auto time = restrictedUntilDateTime.toString(cTimeFormat());

				switch (restriction) {
				case Flag::f_send_polls:
					return tr::lng_restricted_send_polls_until(
						tr::now, lt_date, date, lt_time, time);
				case Flag::f_send_messages:
					return tr::lng_restricted_send_message_until(
						tr::now, lt_date, date, lt_time, time);
				case Flag::f_send_media:
					return tr::lng_restricted_send_media_until(
						tr::now, lt_date, date, lt_time, time);
				case Flag::f_send_stickers:
					return tr::lng_restricted_send_stickers_until(
						tr::now, lt_date, date, lt_time, time);
				case Flag::f_send_gifs:
					return tr::lng_restricted_send_gifs_until(
						tr::now, lt_date, date, lt_time, time);
				case Flag::f_send_inline:
				case Flag::f_send_games:
					return tr::lng_restricted_send_inline_until(
						tr::now, lt_date, date, lt_time, time);
				}
				Unexpected("Restriction in Data::RestrictionErrorKey.");
			}
		}
		switch (restriction) {
		case Flag::f_send_polls:
			return all
				? tr::lng_restricted_send_polls_all(tr::now)
				: tr::lng_restricted_send_polls(tr::now);
		case Flag::f_send_messages:
			return all
				? tr::lng_restricted_send_message_all(tr::now)
				: tr::lng_restricted_send_message(tr::now);
		case Flag::f_send_media:
			return all
				? tr::lng_restricted_send_media_all(tr::now)
				: tr::lng_restricted_send_media(tr::now);
		case Flag::f_send_stickers:
			return all
				? tr::lng_restricted_send_stickers_all(tr::now)
				: tr::lng_restricted_send_stickers(tr::now);
		case Flag::f_send_gifs:
			return all
				? tr::lng_restricted_send_gifs_all(tr::now)
				: tr::lng_restricted_send_gifs(tr::now);
		case Flag::f_send_inline:
		case Flag::f_send_games:
			return all
				? tr::lng_restricted_send_inline_all(tr::now)
				: tr::lng_restricted_send_inline(tr::now);
		}
		Unexpected("Restriction in Data::RestrictionErrorKey.");
	}
	return std::nullopt;
}

void SetTopPinnedMessageId(not_null<PeerData*> peer, MsgId messageId) {
	if (const auto channel = peer->asChannel()) {
		if (messageId <= channel->availableMinId()) {
			return;
		}
	}
	auto &session = peer->session();
	const auto hiddenId = session.settings().hiddenPinnedMessageId(peer->id);
	if (hiddenId != 0 && hiddenId != messageId) {
		session.settings().setHiddenPinnedMessageId(peer->id, 0);
		session.saveSettingsDelayed();
	}
	session.storage().add(Storage::SharedMediaAddExisting(
		peer->id,
		Storage::SharedMediaType::Pinned,
		messageId,
		{ messageId, ServerMaxMsgId }));
	peer->setHasPinnedMessages(true);
}

FullMsgId ResolveTopPinnedId(
		not_null<PeerData*> peer,
		PeerData *migrated) {
	const auto slice = peer->session().storage().snapshot(
		Storage::SharedMediaQuery(
			Storage::SharedMediaKey(
				peer->id,
				Storage::SharedMediaType::Pinned,
				ServerMaxMsgId - 1),
			1,
			1));
	const auto old = migrated
		? migrated->session().storage().snapshot(
			Storage::SharedMediaQuery(
				Storage::SharedMediaKey(
					migrated->id,
					Storage::SharedMediaType::Pinned,
					ServerMaxMsgId - 1),
				1,
				1))
		: Storage::SharedMediaResult{
			.count = 0,
			.skippedBefore = 0,
			.skippedAfter = 0,
		};
	if (!slice.messageIds.empty()) {
		return FullMsgId(peerToChannel(peer->id), slice.messageIds.back());
	} else if (!migrated || slice.count != 0 || old.messageIds.empty()) {
		return FullMsgId();
	} else {
		return FullMsgId(0, old.messageIds.back());
	}
}

FullMsgId ResolveMinPinnedId(
		not_null<PeerData*> peer,
		PeerData *migrated) {
	const auto slice = peer->session().storage().snapshot(
		Storage::SharedMediaQuery(
			Storage::SharedMediaKey(
				peer->id,
				Storage::SharedMediaType::Pinned,
				1),
			1,
			1));
	const auto old = migrated
		? migrated->session().storage().snapshot(
			Storage::SharedMediaQuery(
				Storage::SharedMediaKey(
					migrated->id,
					Storage::SharedMediaType::Pinned,
					1),
				1,
				1))
		: Storage::SharedMediaResult{
			.count = 0,
			.skippedBefore = 0,
			.skippedAfter = 0,
		};
	if (!old.messageIds.empty()) {
		return FullMsgId(0, old.messageIds.front());
	} else if (old.count == 0 && !slice.messageIds.empty()) {
		return FullMsgId(peerToChannel(peer->id), slice.messageIds.front());
	} else {
		return FullMsgId();
	}
}

std::optional<int> ResolvePinnedCount(
		not_null<PeerData*> peer,
		PeerData *migrated) {
	const auto slice = peer->session().storage().snapshot(
		Storage::SharedMediaQuery(
			Storage::SharedMediaKey(
				peer->id,
				Storage::SharedMediaType::Pinned,
				0),
			0,
			0));
	const auto old = migrated
		? migrated->session().storage().snapshot(
			Storage::SharedMediaQuery(
				Storage::SharedMediaKey(
					migrated->id,
					Storage::SharedMediaType::Pinned,
					0),
				0,
				0))
		: Storage::SharedMediaResult{
			.count = 0,
			.skippedBefore = 0,
			.skippedAfter = 0,
	};
	return (slice.count.has_value() && old.count.has_value())
		? std::make_optional(*slice.count + *old.count)
		: std::nullopt;
}

} // namespace Data
