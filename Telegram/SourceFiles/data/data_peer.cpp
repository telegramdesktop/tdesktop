/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_peer.h"

#include "data/data_user.h"
#include "data/data_chat.h"
#include "data/data_chat_participant_status.h"
#include "data/data_channel.h"
#include "data/data_changes.h"
#include "data/data_message_reaction_id.h"
#include "data/data_photo.h"
#include "data/data_folder.h"
#include "data/data_forum.h"
#include "data/data_forum_topic.h"
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
#include "ui/chat/chat_style.h"
#include "ui/empty_userpic.h"
#include "ui/text/text_options.h"
#include "ui/painter.h"
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

uint8 DecideColorIndex(PeerId peerId) {
	return Ui::DecideColorIndex(peerId.value & PeerId::kChatTypeMask);
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

bool operator<(
		const AllowedReactions &a,
		const AllowedReactions &b) {
	return (a.type < b.type) || ((a.type == b.type) && (a.some < b.some));
}

bool operator==(
		const AllowedReactions &a,
		const AllowedReactions &b) {
	return (a.type == b.type) && (a.some == b.some);
}

AllowedReactions Parse(const MTPChatReactions &value) {
	return value.match([&](const MTPDchatReactionsNone &) {
		return AllowedReactions();
	}, [&](const MTPDchatReactionsAll &data) {
		return AllowedReactions{
			.type = (data.is_allow_custom()
				? AllowedReactionsType::All
				: AllowedReactionsType::Default),
		};
	}, [&](const MTPDchatReactionsSome &data) {
		return AllowedReactions{
			.some = ranges::views::all(
				data.vreactions().v
			) | ranges::views::transform(
				ReactionFromMTP
			) | ranges::to_vector,
			.type = AllowedReactionsType::Some,
		};
	});
}

PeerData *PeerFromInputMTP(
		not_null<Session*> owner,
		const MTPInputPeer &input) {
	return input.match([&](const MTPDinputPeerUser &data) {
		const auto user = owner->user(data.vuser_id().v);
		user->setAccessHash(data.vaccess_hash().v);
		return (PeerData*)user;
	}, [&](const MTPDinputPeerChat &data) {
		return (PeerData*)owner->chat(data.vchat_id().v);
	}, [&](const MTPDinputPeerChannel &data) {
		const auto channel = owner->channel(data.vchannel_id().v);
		channel->setAccessHash(data.vaccess_hash().v);
		return (PeerData*)channel;
	}, [&](const MTPDinputPeerSelf &data) {
		return (PeerData*)owner->session().user();
	}, [&](const auto &data) {
		return (PeerData*)nullptr;
	});
}

UserData *UserFromInputMTP(
		not_null<Session*> owner,
		const MTPInputUser &input) {
	return input.match([&](const MTPDinputUser &data) {
		const auto user = owner->user(data.vuser_id().v);
		user->setAccessHash(data.vaccess_hash().v);
		return user.get();
	}, [&](const MTPDinputUserSelf &data) {
		return owner->session().user().get();
	}, [](const auto &data) {
		return (UserData*)nullptr;
	});
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
, _owner(owner)
, _colorIndex(Data::DecideColorIndex(id)) {
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
	if (_name == newName && _nameVersion > 1) {
		if (isUser()) {
			if (asUser()->nameOrPhone == newNameOrPhone
				&& asUser()->editableUsername() == newUsername) {
				return;
			}
		} else if (isChannel()) {
			if (asChannel()->editableUsername() == newUsername) {
				return;
			}
		} else if (isChat()) {
			return;
		}
	}
	_name = newName;
	invalidateEmptyUserpic();

	auto flags = UpdateFlag::None | UpdateFlag::None;
	auto oldFirstLetters = base::flat_set<QChar>();
	const auto nameUpdated = (_nameVersion++ > 1);
	if (nameUpdated) {
		oldFirstLetters = nameFirstLetters();
		flags |= UpdateFlag::Name;
	}
	if (isUser()) {
		if (asUser()->editableUsername() != newUsername) {
			asUser()->setUsername(newUsername);
			flags |= UpdateFlag::Username;
		}
		asUser()->setNameOrPhone(newNameOrPhone);
	} else if (isChannel()) {
		if (asChannel()->editableUsername() != newUsername) {
			asChannel()->setUsername(newUsername);
			if (asChannel()->username().isEmpty()) {
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
		const auto user = asUser();
		_userpicEmpty = std::make_unique<Ui::EmptyUserpic>(
			Ui::EmptyUserpic::UserpicColor(colorIndex()),
			((user && user->isInaccessible())
				? Ui::EmptyUserpic::InaccessibleName()
				: name()));
	}
	return _userpicEmpty.get();
}

void PeerData::invalidateEmptyUserpic() {
	_userpicEmpty = nullptr;
}

ClickHandlerPtr PeerData::createOpenLink() {
	return std::make_shared<PeerClickHandler>(this);
}

void PeerData::setUserpic(
		PhotoId photoId,
		const ImageLocation &location,
		bool hasVideo) {
	_userpicPhotoId = photoId;
	_userpicHasVideo = hasVideo ? 1 : 0;
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

QImage *PeerData::userpicCloudImage(Ui::PeerUserpicView &view) const {
	if (!_userpic.isCurrentView(view.cloud)) {
		if (!_userpic.empty()) {
			view.cloud = _userpic.createView();
			_userpic.load(&session(), userpicOrigin());
		} else {
			view.cloud = nullptr;
		}
		view.cached = QImage();
	}
	if (const auto image = view.cloud.get(); image && !image->isNull()) {
		_userpicEmpty = nullptr;
		return image;
	} else if (isNotificationsUser()) {
		static auto result = Window::LogoNoMargin().scaledToWidth(
			kUserpicSize,
			Qt::SmoothTransformation);
		return &result;
	}
	return nullptr;
}

void PeerData::paintUserpic(
		Painter &p,
		Ui::PeerUserpicView &view,
		int x,
		int y,
		int size) const {
	const auto cloud = userpicCloudImage(view);
	const auto ratio = style::DevicePixelRatio();
	Ui::ValidateUserpicCache(
		view,
		cloud,
		cloud ? nullptr : ensureEmptyUserpic().get(),
		size * ratio,
		isForum());
	p.drawImage(QRect(x, y, size, size), view.cached);
}

void PeerData::loadUserpic() {
	_userpic.load(&session(), userpicOrigin());
}

bool PeerData::hasUserpic() const {
	return !_userpic.empty();
}

Ui::PeerUserpicView PeerData::activeUserpicView() {
	return { .cloud = _userpic.empty() ? nullptr : _userpic.activeView() };
}

Ui::PeerUserpicView PeerData::createUserpicView() {
	if (_userpic.empty()) {
		return {};
	}
	auto result = _userpic.createView();
	_userpic.load(&session(), userpicPhotoOrigin());
	return { .cloud = result };
}

bool PeerData::useEmptyUserpic(Ui::PeerUserpicView &view) const {
	return !userpicCloudImage(view);
}

InMemoryKey PeerData::userpicUniqueKey(Ui::PeerUserpicView &view) const {
	return useEmptyUserpic(view)
		? ensureEmptyUserpic()->uniqueKey()
		: inMemoryKey(_userpic.location());
}

QImage PeerData::generateUserpicImage(
		Ui::PeerUserpicView &view,
		int size,
		std::optional<int> radius) const {
	if (const auto userpic = userpicCloudImage(view)) {
		auto image = userpic->scaled(
			{ size, size },
			Qt::IgnoreAspectRatio,
			Qt::SmoothTransformation);
		const auto round = [&](int radius) {
			return Images::Round(
				std::move(image),
				Images::CornersMask(radius / style::DevicePixelRatio()));
		};
		if (radius == 0) {
			return image;
		} else if (radius) {
			return round(*radius);
		} else if (isForum()) {
			return round(size * Ui::ForumUserpicRadiusMultiplier());
		} else {
			return Images::Circle(std::move(image));
		}
	}
	auto result = QImage(
		QSize(size, size),
		QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::transparent);

	Painter p(&result);
	if (radius == 0) {
		ensureEmptyUserpic()->paintSquare(p, 0, 0, size, size);
	} else if (radius) {
		ensureEmptyUserpic()->paintRounded(p, 0, 0, size, size, *radius);
	} else if (isForum()) {
		ensureEmptyUserpic()->paintRounded(
			p,
			0,
			0,
			size,
			size,
			size * Ui::ForumUserpicRadiusMultiplier());
	} else {
		ensureEmptyUserpic()->paintCircle(p, 0, 0, size, size);
	}
	p.end();

	return result;
}

ImageLocation PeerData::userpicLocation() const {
	return _userpic.location();
}

bool PeerData::userpicPhotoUnknown() const {
	return (_userpicPhotoId == kUnknownPhotoId);
}

PhotoId PeerData::userpicPhotoId() const {
	return userpicPhotoUnknown() ? 0 : _userpicPhotoId;
}

bool PeerData::userpicHasVideo() const {
	return _userpicHasVideo != 0;
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
		|| _userpicHasVideo != (hasVideo ? 1 : 0)) {
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
		return !user->amRestricted(ChatRestriction::PinMessages);
	} else if (const auto chat = asChat()) {
		return chat->amIn()
			&& !chat->amRestricted(ChatRestriction::PinMessages);
	} else if (const auto channel = asChannel()) {
		return channel->isMegagroup()
			? !channel->amRestricted(ChatRestriction::PinMessages)
			: ((channel->amCreator()
				|| channel->adminRights() & ChatAdminRight::EditMessages));
	}
	Unexpected("Peer type in PeerData::canPinMessages.");
}

bool PeerData::canCreatePolls() const {
	if (const auto user = asUser()) {
		return user->isBot() && !user->isSupport();
	}
	return Data::CanSend(this, ChatRestriction::SendPolls);
}

bool PeerData::canCreateTopics() const {
	if (const auto channel = asChannel()) {
		return channel->isForum()
			&& !channel->amRestricted(ChatRestriction::CreateTopics);
	}
	return false;
}

bool PeerData::canManageTopics() const {
	if (const auto channel = asChannel()) {
		return channel->isForum()
			&& (channel->amCreator()
				|| (channel->adminRights() & ChatAdminRight::ManageTopics));
	}
	return false;
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

void PeerData::setTranslationDisabled(bool disabled) {
	const auto flag = disabled
		? TranslationFlag::Disabled
		: TranslationFlag::Enabled;
	if (_translationFlag != flag) {
		_translationFlag = flag;
		session().changes().peerUpdated(
			this,
			UpdateFlag::TranslationDisabled);
	}
}

PeerData::TranslationFlag PeerData::translationFlag() const {
	return _translationFlag;
}

void PeerData::saveTranslationDisabled(bool disabled) {
	setTranslationDisabled(disabled);

	using Flag = MTPmessages_TogglePeerTranslations::Flag;
	session().api().request(MTPmessages_TogglePeerTranslations(
		MTP_flags(disabled ? Flag::f_disabled : Flag()),
		input
	)).send();
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

bool PeerData::changeColorIndex(
		const tl::conditional<MTPint> &cloudColorIndex) {
	return cloudColorIndex
		? changeColorIndex(cloudColorIndex->v)
		: clearColorIndex();
}

bool PeerData::changeBackgroundEmojiId(
		const tl::conditional<MTPlong> &cloudBackgroundEmoji) {
	return changeBackgroundEmojiId(cloudBackgroundEmoji
		? cloudBackgroundEmoji->v
		: DocumentId());
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

	appendToIndex(name());
	const auto appendTranslit = !toIndexList.isEmpty()
		&& cRussianLetters().match(toIndexList.front()).hasMatch();
	if (appendTranslit) {
		appendToIndex(translitRusEng(toIndexList.front()));
	}
	if (const auto user = asUser()) {
		if (user->nameOrPhone != name()) {
			appendToIndex(user->nameOrPhone);
		}
		appendToIndex(user->username());
		if (isSelf()) {
			const auto english = u"Saved messages"_q;
			const auto localized = tr::lng_saved_messages(tr::now);
			appendToIndex(english);
			if (localized != english) {
				appendToIndex(localized);
			}
		} else if (isRepliesChat()) {
			const auto english = u"Replies"_q;
			const auto localized = tr::lng_replies_messages(tr::now);
			appendToIndex(english);
			if (localized != english) {
				appendToIndex(localized);
			}
		}
	} else if (const auto channel = asChannel()) {
		appendToIndex(channel->username());
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

const QString &PeerData::topBarNameText() const {
	if (const auto to = migrateTo()) {
		return to->topBarNameText();
	} else if (const auto user = asUser()) {
		if (!user->nameOrPhone.isEmpty()) {
			return user->nameOrPhone;
		}
	}
	return _name;
}

int PeerData::nameVersion() const {
	return _nameVersion;
}

const QString &PeerData::name() const {
	if (const auto to = migrateTo()) {
		return to->name();
	}
	return _name;
}

const QString &PeerData::shortName() const {
	if (const auto user = asUser()) {
		return user->firstName.isEmpty() ? user->lastName : user->firstName;
	}
	return _name;
}

QString PeerData::userName() const {
	if (const auto user = asUser()) {
		return user->username();
	} else if (const auto channel = asChannel()) {
		return channel->username();
	}
	return QString();
}

bool PeerData::changeColorIndex(uint8 index) {
	index %= Ui::kColorIndexCount;
	if (_colorIndexCloud && _colorIndex == index) {
		return false;
	}
	_colorIndexCloud = 1;
	_colorIndex = index;
	return true;
}

bool PeerData::clearColorIndex() {
	if (!_colorIndexCloud) {
		return false;
	}
	_colorIndexCloud = 0;
	_colorIndex = Data::DecideColorIndex(id);
	return true;
}

DocumentId PeerData::backgroundEmojiId() const {
	return _backgroundEmojiId;
}

bool PeerData::changeBackgroundEmojiId(DocumentId id) {
	if (_backgroundEmojiId == id) {
		return false;
	}
	_backgroundEmojiId = id;
	return true;
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
	if (const auto channel = asChannel()) {
		return channel->isMegagroup();
	}
	return false;
}

bool PeerData::isBroadcast() const {
	if (const auto channel = asChannel()) {
		return channel->isBroadcast();
	}
	return false;
}

bool PeerData::isForum() const {
	if (const auto channel = asChannel()) {
		return channel->isForum();
	}
	return false;
}

bool PeerData::isGigagroup() const {
	if (const auto channel = asChannel()) {
		return channel->isGigagroup();
	}
	return false;
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

bool PeerData::sharedMediaInfo() const {
	return isSelf() || isRepliesChat();
}

bool PeerData::hasStoriesHidden() const {
	if (const auto user = asUser()) {
		return user->hasStoriesHidden();
	} else if (const auto channel = asChannel()) {
		return channel->hasStoriesHidden();
	}
	return false;
}

void PeerData::setStoriesHidden(bool hidden) {
	if (const auto user = asUser()) {
		user->setFlags(hidden
			? (user->flags() | UserDataFlag::StoriesHidden)
			: (user->flags() & ~UserDataFlag::StoriesHidden));
	} else if (const auto channel = asChannel()) {
		channel->setFlags(hidden
			? (channel->flags() | ChannelDataFlag::StoriesHidden)
			: (channel->flags() & ~ChannelDataFlag::StoriesHidden));
	} else {
		Unexpected("PeerData::setStoriesHidden for non-user/non-channel.");
	}
}

Data::Forum *PeerData::forum() const {
	if (const auto channel = asChannel()) {
		return channel->forum();
	}
	return nullptr;
}

Data::ForumTopic *PeerData::forumTopicFor(MsgId rootId) const {
	if (!rootId) {
		return nullptr;
	} else if (const auto forum = this->forum()) {
		return forum->topicFor(rootId);
	}
	return nullptr;
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
		if (right == ChatRestriction::AddParticipants) {
			return chat->adminRights() & ChatAdminRight::InviteByLinkOrAdd;
		} else if (right == ChatRestriction::ChangeInfo) {
			return chat->adminRights() & ChatAdminRight::ChangeInfo;
		} else if (right == ChatRestriction::CreateTopics) {
			return chat->adminRights() & ChatAdminRight::ManageTopics;
		} else if (right == ChatRestriction::PinMessages) {
			return chat->adminRights() & ChatAdminRight::PinMessages;
		} else {
			return chat->hasAdminRights();
		}
	};
	if (const auto user = asUser()) {
		return (right == ChatRestriction::SendVoiceMessages
			|| right == ChatRestriction::SendVideoMessages)
			? ((user->flags() & UserDataFlag::VoiceMessagesForbidden)
				? Result::Explicit()
				: Result::Allowed())
			: (right == ChatRestriction::PinMessages)
			? ((user->flags() & UserDataFlag::CanPinMessages)
				? Result::Allowed()
				: Result::Explicit())
			: Result::Allowed();
	} else if (const auto channel = asChannel()) {
		const auto defaultRestrictions = channel->defaultRestrictions()
			| (channel->isPublic()
				? (ChatRestriction::PinMessages
					| ChatRestriction::ChangeInfo)
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

void PeerData::setWallPaper(std::optional<Data::WallPaper> paper) {
	if (!paper && !_wallPaper) {
		return;
	} else if (paper && _wallPaper && _wallPaper->equals(*paper)) {
		return;
	}
	_wallPaper = paper
		? std::make_unique<Data::WallPaper>(std::move(*paper))
		: nullptr;
	session().changes().peerUpdated(this, UpdateFlag::ChatWallPaper);
}

const Data::WallPaper *PeerData::wallPaper() const {
	return _wallPaper.get();
}

bool PeerData::hasActiveStories() const {
	if (const auto user = asUser()) {
		return user->hasActiveStories();
	} else if (const auto channel = asChannel()) {
		return channel->hasActiveStories();
	}
	return false;
}

bool PeerData::hasUnreadStories() const {
	if (const auto user = asUser()) {
		return user->hasUnreadStories();
	} else if (const auto channel = asChannel()) {
		return channel->hasUnreadStories();
	}
	return false;
}

void PeerData::setStoriesState(StoriesState state) {
	if (const auto user = asUser()) {
		return user->setStoriesState(state);
	} else if (const auto channel = asChannel()) {
		return channel->setStoriesState(state);
	} else {
		Unexpected("PeerData::setStoriesState for non-user/non-channel.");
	}
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

void SetTopPinnedMessageId(
		not_null<PeerData*> peer,
		MsgId messageId) {
	if (const auto channel = peer->asChannel()) {
		if (messageId <= channel->availableMinId()) {
			return;
		}
	}
	auto &session = peer->session();
	const auto hiddenId = session.settings().hiddenPinnedMessageId(peer->id);
	if (hiddenId != 0 && hiddenId != messageId) {
		session.settings().setHiddenPinnedMessageId(
			peer->id,
			MsgId(0), // topicRootId
			0);
		session.saveSettingsDelayed();
	}
	session.storage().add(Storage::SharedMediaAddExisting(
		peer->id,
		MsgId(0), // topicRootId
		Storage::SharedMediaType::Pinned,
		messageId,
		{ messageId, ServerMaxMsgId }));
	peer->owner().history(peer)->setHasPinnedMessages(true);
}

FullMsgId ResolveTopPinnedId(
		not_null<PeerData*> peer,
		MsgId topicRootId,
		PeerData *migrated) {
	const auto slice = peer->session().storage().snapshot(
		Storage::SharedMediaQuery(
			Storage::SharedMediaKey(
				peer->id,
				topicRootId,
				Storage::SharedMediaType::Pinned,
				ServerMaxMsgId - 1),
			1,
			1));
	const auto old = (!topicRootId && migrated)
		? migrated->session().storage().snapshot(
			Storage::SharedMediaQuery(
				Storage::SharedMediaKey(
					migrated->id,
					MsgId(0), // topicRootId
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
		MsgId topicRootId,
		PeerData *migrated) {
	const auto slice = peer->session().storage().snapshot(
		Storage::SharedMediaQuery(
			Storage::SharedMediaKey(
				peer->id,
				topicRootId,
				Storage::SharedMediaType::Pinned,
				1),
			1,
			1));
	const auto old = (!topicRootId && migrated)
		? migrated->session().storage().snapshot(
			Storage::SharedMediaQuery(
				Storage::SharedMediaKey(
					migrated->id,
					MsgId(0), // topicRootId
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

} // namespace Data
