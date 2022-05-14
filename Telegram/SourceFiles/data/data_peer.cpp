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
#include "data/data_cloud_themes.h"
#include "base/unixtime.h"
#include "base/crc32hash.h"
#include "lang/lang_keys.h"
#include "apiwrap.h"
#include "api/api_chat_participants.h"
#include "ui/boxes/confirm_box.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_app_config.h"
#include "mtproto/mtproto_config.h"
#include "core/application.h"
#include "core/click_handler_types.h"
#include "window/window_session_controller.h"
#include "window/main_window.h" // Window::LogoNoMargin.
#include "ui/image/image.h"
#include "ui/empty_userpic.h"
#include "ui/text/text_options.h"
#include "ui/toasts/common_toasts.h"
#include "ui/ui_utility.h"
#include "history/history.h"
#include "history/view/history_view_element.h"
#include "history/history_item.h"
#include "storage/file_download.h"
#include "storage/storage_facade.h"
#include "storage/storage_shared_media.h"

namespace {

constexpr auto kUpdateFullPeerTimeout = crl::time(5000); // Not more than once in 5 seconds.
constexpr auto kUserpicSize = 160;

using UpdateFlag = Data::PeerUpdate::Flag;

} // namespace

namespace Data {

int PeerColorIndex(BareId bareId) {
	const auto index = bareId % 7;
	const int map[] = { 0, 7, 4, 1, 6, 3, 5 };
	return map[index];
}

int PeerColorIndex(PeerId peerId) {
	return PeerColorIndex(peerId.value & PeerId::kChatTypeMask);
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
	constexpr auto kShift = (0xFEULL << 32);
	const auto base = name.isEmpty()
		? 777
		: base::crc32(name.constData(), name.size() * sizeof(QChar));
	return peerFromUser(kShift + std::abs(base));
}

bool ApplyBotMenuButton(
		not_null<BotInfo*> info,
		const MTPBotMenuButton *button) {
	auto text = QString();
	auto url = QString();
	if (button) {
		button->match([&](const MTPDbotMenuButton &data) {
			text = qs(data.vtext());
			url = qs(data.vurl());
		}, [&](const auto &) {
		});
	}
	const auto changed = (info->botMenuButtonText != text)
		|| (info->botMenuButtonUrl != url);

	info->botMenuButtonText = text;
	info->botMenuButtonUrl = url;

	return changed;
}

} // namespace Data

PeerClickHandler::PeerClickHandler(not_null<PeerData*> peer)
: _peer(peer) {
	setProperty(kPeerLinkPeerIdProperty, peer->id.value);
}

void PeerClickHandler::onClick(ClickContext context) const {
	if (context.button != Qt::LeftButton) {
		return;
	}
	const auto my = context.other.value<ClickHandlerContext>();
	const auto window = [&]() -> Window::SessionController* {
		if (const auto controller = my.sessionWindow.get()) {
			return controller;
		}
		const auto &windows = _peer->session().windows();
		if (windows.empty()) {
			_peer->session().domain().activate(&_peer->session().account());
			if (windows.empty()) {
				return nullptr;
			}
		}
		return windows.front();
	}();
	if (window) {
		window->showPeer(_peer);
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
				asChannel()->removeFlags(ChannelDataFlag::Username);
			} else {
				asChannel()->addFlags(ChannelDataFlag::Username);
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

void PeerData::setUserpic(
		PhotoId photoId,
		const ImageLocation &location,
		bool hasVideo) {
	_userpicPhotoId = photoId;
	_userpicHasVideo = hasVideo;
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
			Window::LogoNoMargin().scaledToWidth(
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
		const auto circled = Images::Option::RoundCircle;
		p.drawPixmap(
			x,
			y,
			userpic->pix(size, size, { .options = circled }));
	} else {
		ensureEmptyUserpic()->paint(p, x, y, x + size + x, size);
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
	generateUserpicImage(view, size * cIntRetinaFactor()).save(path, "PNG");
}

void PeerData::saveUserpicRounded(
		std::shared_ptr<Data::CloudImageView> &view,
		const QString &path,
		int size) const {
	generateUserpicImage(
		view,
		size * cIntRetinaFactor(),
		ImageRoundRadius::Small).save(path, "PNG");
}

QPixmap PeerData::genUserpic(
		std::shared_ptr<Data::CloudImageView> &view,
		int size) const {
	if (const auto userpic = currentUserpic(view)) {
		const auto circle = Images::Option::RoundCircle;
		return userpic->pix(size, size, { .options = circle });
	}
	const auto ratio = style::DevicePixelRatio();
	auto result = QImage(
		QSize(size, size) * ratio,
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(ratio);
	result.fill(Qt::transparent);
	{
		Painter p(&result);
		paintUserpic(p, view, 0, 0, size);
	}
	return Ui::PixmapFromImage(std::move(result));
}

QImage PeerData::generateUserpicImage(
		std::shared_ptr<Data::CloudImageView> &view,
		int size) const {
	return generateUserpicImage(view, size, ImageRoundRadius::Ellipse);
}

QImage PeerData::generateUserpicImage(
		std::shared_ptr<Data::CloudImageView> &view,
		int size,
		ImageRoundRadius radius) const {
	if (const auto userpic = currentUserpic(view)) {
		const auto options = (radius == ImageRoundRadius::Ellipse)
			? Images::Option::RoundCircle
			: (radius == ImageRoundRadius::None)
			? Images::Option()
			: Images::Option::RoundSmall;
		return userpic->pixNoCache(
			{ size, size },
			{ .options = options }).toImage();
	}
	auto result = QImage(
		QSize(size, size),
		QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::transparent);
	{
		Painter p(&result);
		if (radius == ImageRoundRadius::Ellipse) {
			ensureEmptyUserpic()->paint(p, 0, 0, size, size);
		} else if (radius == ImageRoundRadius::None) {
			ensureEmptyUserpic()->paintSquare(p, 0, 0, size, size);
		} else {
			ensureEmptyUserpic()->paintRounded(p, 0, 0, size, size);
		}
	}
	return result;
}

Data::FileOrigin PeerData::userpicOrigin() const {
	return Data::FileOriginPeerPhoto(id);
}

Data::FileOrigin PeerData::userpicPhotoOrigin() const {
	return (isUser() && userpicPhotoId())
		? Data::FileOriginUserPhoto(peerToUser(id).bare, userpicPhotoId())
		: Data::FileOrigin();
}

void PeerData::updateUserpic(
		PhotoId photoId,
		MTP::DcId dcId,
		bool hasVideo) {
	setUserpicChecked(
		photoId,
		ImageLocation(
			{ StorageFileLocation(
				dcId,
				isSelf() ? peerToUser(id) : UserId(),
				MTP_inputPeerPhotoFileLocation(
					MTP_flags(0),
					input,
					MTP_long(photoId))) },
			kUserpicSize,
			kUserpicSize),
		hasVideo);
}

void PeerData::clearUserpic() {
	setUserpicChecked(PhotoId(), ImageLocation(), false);
}

void PeerData::setUserpicChecked(
		PhotoId photoId,
		const ImageLocation &location,
		bool hasVideo) {
	if (_userpicPhotoId != photoId
		|| _userpic.location() != location
		|| _userpicHasVideo != hasVideo) {
		const auto known = !userpicPhotoUnknown();
		setUserpic(photoId, location, hasVideo);
		session().changes().peerUpdated(this, UpdateFlag::Photo);
		if (known && isPremium() && userpicPhotoUnknown()) {
			updateFull();
		}
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
	auto &&filtered = ranges::views::all(
		list
	) | ranges::views::filter([&](const Data::UnavailableReason &reason) {
		return !ranges::contains(skip, reason.reason);
	});
	const auto first = filtered.begin();
	return (first != filtered.end()) ? first->text : QString();
}

// This is duplicated in CanPinMessagesValue().
bool PeerData::canPinMessages() const {
	if (const auto user = asUser()) {
		return user->flags() & UserDataFlag::CanPinMessages;
	} else if (const auto chat = asChat()) {
		return chat->amIn()
			&& !chat->amRestricted(ChatRestriction::PinMessages);
	} else if (const auto channel = asChannel()) {
		return channel->isMegagroup()
			? !channel->amRestricted(ChatRestriction::PinMessages)
			: ((channel->adminRights() & ChatAdminRight::EditMessages)
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

bool PeerData::canExportChatHistory() const {
	if (isRepliesChat() || !allowsForwarding()) {
		return false;
	} else if (const auto channel = asChannel()) {
		if (!channel->amIn() && channel->invitePeekExpires()) {
			return false;
		}
	}
	for (const auto &block : _owner->history(id)->blocks) {
		for (const auto &message : block->messages) {
			if (!message->data()->isService()) {
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

void PeerData::setSettings(const MTPPeerSettings &data) {
	data.match([&](const MTPDpeerSettings &data) {
		_requestChatTitle = data.vrequest_chat_title().value_or_empty();
		_requestChatDate = data.vrequest_chat_date().value_or_empty();

		using Flag = PeerSetting;
		setSettings((data.is_add_contact() ? Flag::AddContact : Flag())
			| (data.is_autoarchived() ? Flag::AutoArchived : Flag())
			| (data.is_block_contact() ? Flag::BlockContact : Flag())
			//| (data.is_invite_members() ? Flag::InviteMembers : Flag())
			| (data.is_need_contacts_exception()
				? Flag::NeedContactsException
				: Flag())
			//| (data.is_report_geo() ? Flag::ReportGeo : Flag())
			| (data.is_report_spam() ? Flag::ReportSpam : Flag())
			| (data.is_share_contact() ? Flag::ShareContact : Flag())
			| (data.vrequest_chat_title() ? Flag::RequestChat : Flag())
			| (data.is_request_chat_broadcast()
				? Flag::RequestChatIsBroadcast
				: Flag()));
	});
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
			session().api().chatParticipants().requestSelf(channel);
		}
	}
}

void PeerData::fullUpdated() {
	_lastFullUpdate = crl::now();
	setLoadedStatus(LoadedStatus::Full);
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

bool PeerData::isSelf() const {
	if (const auto user = asUser()) {
		return (user->flags() & UserDataFlag::Self);
	}
	return false;
}

bool PeerData::isVerified() const {
	if (const auto user = asUser()) {
		return user->isVerified();
	} else if (const auto channel = asChannel()) {
		return channel->isVerified();
	}
	return false;
}

bool PeerData::isPremium() const {
	if (const auto user = asUser()) {
		return user->isPremium();
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

bool PeerData::isFake() const {
	if (const auto user = asUser()) {
		return user->isFake();
	} else if (const auto channel = asChannel()) {
		return channel->isFake();
	}
	return false;
}

bool PeerData::isMegagroup() const {
	return isChannel() && asChannel()->isMegagroup();
}

bool PeerData::isBroadcast() const {
	return isChannel() && asChannel()->isBroadcast();
}

bool PeerData::isGigagroup() const {
	return isChannel() && asChannel()->isGigagroup();
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

bool PeerData::allowsForwarding() const {
	if (const auto user = asUser()) {
		return true;
	} else if (const auto channel = asChannel()) {
		return channel->allowsForwarding();
	} else if (const auto chat = asChat()) {
		return chat->allowsForwarding();
	}
	return false;
}

Data::RestrictionCheckResult PeerData::amRestricted(
		ChatRestriction right) const {
	using Result = Data::RestrictionCheckResult;
	const auto allowByAdminRights = [](auto right, auto chat) -> bool {
		if (right == ChatRestriction::InviteUsers) {
			return chat->adminRights() & ChatAdminRight::InviteUsers;
		} else if (right == ChatRestriction::ChangeInfo) {
			return chat->adminRights() & ChatAdminRight::ChangeInfo;
		} else if (right == ChatRestriction::PinMessages) {
			return chat->adminRights() & ChatAdminRight::PinMessages;
		} else {
			return chat->hasAdminRights();
		}
	};
	if (const auto channel = asChannel()) {
		const auto defaultRestrictions = channel->defaultRestrictions()
			| (channel->isPublic()
				? (ChatRestriction::PinMessages | ChatRestriction::ChangeInfo)
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
			&& (asChannel()->adminRights() & ChatAdminRight::Anonymous));
}

bool PeerData::canRevokeFullHistory() const {
	if (const auto user = asUser()) {
		return !isSelf()
			&& (!user->isBot() || user->isSupport())
			&& session().serverConfig().revokePrivateInbox
			&& (session().serverConfig().revokePrivateTimeLimit == 0x7FFFFFFF);
	} else if (const auto chat = asChat()) {
		return chat->amCreator();
	} else if (const auto megagroup = asMegagroup()) {
		return megagroup->amCreator()
			&& megagroup->membersCountKnown()
			&& megagroup->canDelete();
	}
	return false;
}

bool PeerData::slowmodeApplied() const {
	if (const auto channel = asChannel()) {
		return !channel->amCreator()
			&& !channel->hasAdminRights()
			&& (channel->flags() & ChannelDataFlag::SlowmodeEnabled);
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
		return (change.diff & ChannelDataFlag::SlowmodeEnabled) != 0;
	}) | rpl::map([=](const ChannelData::Flags::Change &change) {
		return (change.value & ChannelDataFlag::SlowmodeEnabled) != 0;
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

bool PeerData::canManageGroupCall() const {
	if (const auto chat = asChat()) {
		return chat->amCreator()
			|| (chat->adminRights() & ChatAdminRight::ManageCall);
	} else if (const auto group = asChannel()) {
		return group->amCreator()
			|| (group->adminRights() & ChatAdminRight::ManageCall);
	}
	return false;
}

Data::GroupCall *PeerData::groupCall() const {
	if (const auto chat = asChat()) {
		return chat->groupCall();
	} else if (const auto group = asChannel()) {
		return group->groupCall();
	}
	return nullptr;
}

PeerId PeerData::groupCallDefaultJoinAs() const {
	if (const auto chat = asChat()) {
		return chat->groupCallDefaultJoinAs();
	} else if (const auto group = asChannel()) {
		return group->groupCallDefaultJoinAs();
	}
	return 0;
}

void PeerData::setThemeEmoji(const QString &emoticon) {
	if (_themeEmoticon == emoticon) {
		return;
	}
	if (Ui::Emoji::Find(_themeEmoticon) == Ui::Emoji::Find(emoticon)) {
		_themeEmoticon = emoticon;
		return;
	}
	_themeEmoticon = emoticon;
	if (!emoticon.isEmpty()
		&& !owner().cloudThemes().themeForEmoji(emoticon)) {
		owner().cloudThemes().refreshChatThemes();
	}
	session().changes().peerUpdated(this, UpdateFlag::ChatThemeEmoji);
}

const QString &PeerData::themeEmoji() const {
	return _themeEmoticon;
}

void PeerData::setIsBlocked(bool is) {
	const auto status = is
		? BlockStatus::Blocked
		: BlockStatus::NotBlocked;
	if (_blockStatus != status) {
		_blockStatus = status;
		if (const auto user = asUser()) {
			const auto flags = user->flags();
			if (is) {
				user->setFlags(flags | UserDataFlag::Blocked);
			} else {
				user->setFlags(flags & ~UserDataFlag::Blocked);
			}
		}
		session().changes().peerUpdated(this, UpdateFlag::IsBlocked);
	}
}

void PeerData::setLoadedStatus(LoadedStatus status) {
	_loadedStatus = status;
}

TimeId PeerData::messagesTTL() const {
	return _ttlPeriod;
}

void PeerData::setMessagesTTL(TimeId period) {
	if (_ttlPeriod != period) {
		_ttlPeriod = period;
		session().changes().peerUpdated(
			this,
			Data::PeerUpdate::Flag::MessagesTTL);
	}
}

namespace Data {

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
				auto date = restrictedUntilDateTime.toString(cDateFormat());
				auto time = restrictedUntilDateTime.toString(cTimeFormat());

				switch (restriction) {
				case Flag::SendPolls:
					return tr::lng_restricted_send_polls_until(
						tr::now, lt_date, date, lt_time, time);
				case Flag::SendMessages:
					return tr::lng_restricted_send_message_until(
						tr::now, lt_date, date, lt_time, time);
				case Flag::SendMedia:
					return tr::lng_restricted_send_media_until(
						tr::now, lt_date, date, lt_time, time);
				case Flag::SendStickers:
					return tr::lng_restricted_send_stickers_until(
						tr::now, lt_date, date, lt_time, time);
				case Flag::SendGifs:
					return tr::lng_restricted_send_gifs_until(
						tr::now, lt_date, date, lt_time, time);
				case Flag::SendInline:
				case Flag::SendGames:
					return tr::lng_restricted_send_inline_until(
						tr::now, lt_date, date, lt_time, time);
				}
				Unexpected("Restriction in Data::RestrictionErrorKey.");
			}
		}
		switch (restriction) {
		case Flag::SendPolls:
			return all
				? tr::lng_restricted_send_polls_all(tr::now)
				: tr::lng_restricted_send_polls(tr::now);
		case Flag::SendMessages:
			return all
				? tr::lng_restricted_send_message_all(tr::now)
				: tr::lng_restricted_send_message(tr::now);
		case Flag::SendMedia:
			return all
				? tr::lng_restricted_send_media_all(tr::now)
				: tr::lng_restricted_send_media(tr::now);
		case Flag::SendStickers:
			return all
				? tr::lng_restricted_send_stickers_all(tr::now)
				: tr::lng_restricted_send_stickers(tr::now);
		case Flag::SendGifs:
			return all
				? tr::lng_restricted_send_gifs_all(tr::now)
				: tr::lng_restricted_send_gifs(tr::now);
		case Flag::SendInline:
		case Flag::SendGames:
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
	peer->owner().history(peer)->setHasPinnedMessages(true);
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
		return FullMsgId(peer->id, slice.messageIds.back());
	} else if (!migrated || slice.count != 0 || old.messageIds.empty()) {
		return FullMsgId();
	} else {
		return FullMsgId(migrated->id, old.messageIds.back());
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
		return FullMsgId(migrated->id, old.messageIds.front());
	} else if (old.count == 0 && !slice.messageIds.empty()) {
		return FullMsgId(peer->id, slice.messageIds.front());
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
