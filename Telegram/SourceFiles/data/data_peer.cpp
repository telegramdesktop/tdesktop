/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_peer.h"

#include "api/api_sensitive_content.h"
#include "data/data_user.h"
#include "data/data_chat.h"
#include "data/data_chat_participant_status.h"
#include "data/data_channel.h"
#include "data/data_changes.h"
#include "data/data_emoji_statuses.h"
#include "data/data_message_reaction_id.h"
#include "data/data_photo.h"
#include "data/data_folder.h"
#include "data/data_forum.h"
#include "data/data_forum_topic.h"
#include "data/data_saved_messages.h"
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
#include "ui/unread_badge.h"
#include "ui/ui_utility.h"
#include "history/history.h"
#include "history/view/history_view_element.h"
#include "history/history_item.h"
#include "storage/file_download.h"
#include "storage/storage_account.h"
#include "storage/storage_facade.h"
#include "storage/storage_shared_media.h"

namespace {

constexpr auto kUpdateFullPeerTimeout = crl::time(5000); // Not more than once in 5 seconds.
constexpr auto kUserpicSize = 160;

using UpdateFlag = Data::PeerUpdate::Flag;

[[nodiscard]] const std::vector<QString> &IgnoredReasons(
		not_null<Main::Session*> session) {
	return session->appConfig().ignoredRestrictionReasons();
}

[[nodiscard]] int ParseRegistrationDate(const QString &text) {
	// MM.YYYY
	if (text.size() != 7 || text[2] != '.') {
		return 0;
	}
	const auto month = text.mid(0, 2).toInt();
	const auto year = text.mid(3, 4).toInt();
	return (year > 2012 && year < 2100 && month > 0 && month <= 12)
		? (year * 100) + month
		: 0;
}

[[nodiscard]] int RegistrationYear(int date) {
	const auto year = date / 100;
	return (year > 2012 && year < 2100) ? year : 0;
}

[[nodiscard]] int RegistrationMonth(int date) {
	const auto month = date % 100;
	return (month > 0 && month <= 12) ? month : 0;
}

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

bool UnavailableReason::sensitive() const {
	return reason == u"sensitive"_q;
}

UnavailableReason UnavailableReason::Sensitive() {
	return { u"sensitive"_q };
}

QString UnavailableReason::Compute(
		not_null<Main::Session*> session,
		const std::vector<UnavailableReason> &list) {
	const auto &skip = IgnoredReasons(session);
	auto &&filtered = ranges::views::all(
		list
	) | ranges::views::filter([&](const Data::UnavailableReason &reason) {
		return !reason.sensitive()
			&& !ranges::contains(skip, reason.reason);
	});
	const auto first = filtered.begin();
	return (first != filtered.end()) ? first->text : QString();
}

bool UnavailableReason::IgnoreSensitiveMark(
		not_null<Main::Session*> session) {
	return ranges::contains(
			IgnoredReasons(session),
			UnavailableReason::Sensitive().reason);
}

// We should get a full restriction in "{full}: {reason}" format and we
// need to find an "-all" tag in {full}, otherwise ignore this restriction.
std::vector<UnavailableReason> UnavailableReason::Extract(
		const MTPvector<MTPRestrictionReason> *list) {
	if (!list) {
		return {};
	}
	return ranges::views::all(
		list->v
	) | ranges::views::filter([](const MTPRestrictionReason &restriction) {
		return restriction.match([&](const MTPDrestrictionReason &data) {
			const auto platform = data.vplatform().v;
			return false
#ifdef OS_MAC_STORE
				|| (platform == "ios"_q)
#elif defined OS_WIN_STORE // OS_MAC_STORE
				|| (platform == "ms"_q)
#endif // OS_MAC_STORE || OS_WIN_STORE
				|| (platform == "all"_q);
		});
	}) | ranges::views::transform([](const MTPRestrictionReason &restriction) {
		return restriction.match([&](const MTPDrestrictionReason &data) {
			return UnavailableReason{ qs(data.vreason()), qs(data.vtext()) };
		});
	}) | ranges::to_vector;
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

AllowedReactions Parse(
		const MTPChatReactions &value,
		int maxCount,
		bool paidEnabled) {
	return value.match([&](const MTPDchatReactionsNone &) {
		return AllowedReactions{
			.maxCount = maxCount,
			.paidEnabled = paidEnabled,
		};
	}, [&](const MTPDchatReactionsAll &data) {
		return AllowedReactions{
			.maxCount = maxCount,
			.type = (data.is_allow_custom()
				? AllowedReactionsType::All
				: AllowedReactionsType::Default),
			.paidEnabled = paidEnabled,
		};
	}, [&](const MTPDchatReactionsSome &data) {
		return AllowedReactions{
			.some = ranges::views::all(
				data.vreactions().v
			) | ranges::views::transform(
				ReactionFromMTP
			) | ranges::to_vector,
			.maxCount = maxCount,
			.type = AllowedReactionsType::Some,
			.paidEnabled = paidEnabled,
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

Ui::ColorCollectible ParseColorCollectible(
		const MTPDpeerColorCollectible &data) {
	return {
		.collectibleId = data.vcollectible_id().v,
		.giftEmojiId = data.vgift_emoji_id().v,
		.backgroundEmojiId = data.vbackground_emoji_id().v,
		.accentColor = Ui::ColorFromSerialized(data.vaccent_color()),
		.strip = ranges::views::all(
			data.vcolors().v
		) | ranges::views::transform(
			&Ui::ColorFromSerialized
		) | ranges::to_vector,
		.darkAccentColor = Ui::MaybeColorFromSerialized(
			data.vdark_accent_color()).value_or(QColor(0, 0, 0, 0)),
		.darkStrip = (data.vdark_colors()
			? ranges::views::all(
				data.vdark_colors()->v
			) | ranges::views::transform(
				&Ui::ColorFromSerialized
			) | ranges::to_vector
			: std::vector<QColor>()),
	};
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

void PeerData::checkTrustedPayForMessage() {
	if (!_checkedTrustedPayForMessage
		&& !starsPerMessage()
		&& session().local().peerTrustedPayForMessageRead()) {
		_checkedTrustedPayForMessage = 1;
		if (session().local().hasPeerTrustedPayForMessageEntry(id)) {
			session().local().clearPeerTrustedPayForMessage(id);
		}
	}
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
		PaintUserpicContext context) const {
	if (const auto broadcast = monoforumBroadcast()) {
		if (context.shape == Ui::PeerUserpicShape::Auto) {
			context.shape = Ui::PeerUserpicShape::Monoforum;
		}
		broadcast->paintUserpic(p, view, context);
		return;
	}
	const auto size = context.size;
	const auto cloud = userpicCloudImage(view);
	const auto ratio = style::DevicePixelRatio();
	if (context.shape == Ui::PeerUserpicShape::Auto) {
		context.shape = isForum()
			? Ui::PeerUserpicShape::Forum
			: isMonoforum()
			? Ui::PeerUserpicShape::Monoforum
			: Ui::PeerUserpicShape::Circle;
	}
	Ui::ValidateUserpicCache(
		view,
		cloud,
		cloud ? nullptr : ensureEmptyUserpic().get(),
		size * ratio,
		context.shape);
	p.drawImage(QRect(context.position, QSize(size, size)), view.cached);
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

QImage PeerData::GenerateUserpicImage(
		not_null<PeerData*> peer,
		Ui::PeerUserpicView &view,
		int size,
		std::optional<int> radius) {
	if (const auto userpic = peer->userpicCloudImage(view)) {
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
		} else if (peer->isForum()) {
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
		peer->ensureEmptyUserpic()->paintSquare(p, 0, 0, size, size);
	} else if (radius) {
		const auto r = *radius;
		peer->ensureEmptyUserpic()->paintRounded(p, 0, 0, size, size, r);
	} else if (peer->isForum()) {
		peer->ensureEmptyUserpic()->paintRounded(
			p,
			0,
			0,
			size,
			size,
			size * Ui::ForumUserpicRadiusMultiplier());
	} else {
		peer->ensureEmptyUserpic()->paintCircle(p, 0, 0, size, size);
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
	return Data::UnavailableReason::Compute(
		&session(),
		unavailableReasons());
}

bool PeerData::hasSensitiveContent() const {
	return _sensitiveContent == 1;
}

void PeerData::setUnavailableReasonsList(
		std::vector<Data::UnavailableReason> &&reasons) {
	Unexpected("PeerData::setUnavailableReasonsList.");
}

void PeerData::setUnavailableReasons(
		std::vector<Data::UnavailableReason> &&reasons) {
	const auto i = ranges::find(
		reasons,
		true,
		&Data::UnavailableReason::sensitive);
	const auto sensitive = (i != end(reasons));
	if (sensitive) {
		reasons.erase(i);
	}
	auto changed = (sensitive != hasSensitiveContent());
	if (changed) {
		setHasSensitiveContent(sensitive);
	}
	if (reasons != unavailableReasons()) {
		setUnavailableReasonsList(std::move(reasons));
		changed = true;
	}
	if (changed) {
		session().changes().peerUpdated(
			this,
			UpdateFlag::UnavailableReason);
	}
}

void PeerData::setHasSensitiveContent(bool has) {
	_sensitiveContent = has ? 1 : 0;
	if (has) {
		session().api().sensitiveContent().preload();
	}
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
		return user->isSelf()
			|| (user->isBot()
				&& !user->isSupport()
				&& !user->isRepliesChat()
				&& !user->isVerifyCodes());
	} else if (isMonoforum()) {
		return false;
	}
	return Data::CanSend(this, ChatRestriction::SendPolls);
}

bool PeerData::canCreateTodoLists() const {
	if (isMonoforum() || isBroadcast()) {
		return false;
	}
	return session().premium()
		&& (Data::CanSend(this, ChatRestriction::SendPolls) || isUser());
}

bool PeerData::canCreateTopics() const {
	if (const auto bot = asBot()) {
		return bot->isForum();
	} else if (const auto channel = asChannel()) {
		return channel->isForum()
			&& !channel->amRestricted(ChatRestriction::CreateTopics);
	}
	return false;
}

bool PeerData::canManageTopics() const {
	if (const auto bot = asBot()) {
		return bot->isForum();
	} else if (const auto channel = asChannel()) {
		return channel->isForum()
			&& (channel->amCreator()
				|| (channel->adminRights() & ChatAdminRight::ManageTopics));
	}
	return false;
}

bool PeerData::canPostStories() const {
	if (const auto channel = asChannel()) {
		return channel->canPostStories();
	}
	return isSelf();
}

bool PeerData::canEditStories() const {
	if (const auto channel = asChannel()) {
		return channel->canEditStories();
	}
	return isSelf();
}

bool PeerData::canDeleteStories() const {
	if (const auto channel = asChannel()) {
		return channel->canDeleteStories();
	}
	return isSelf();
}

bool PeerData::canManageGifts() const {
	if (const auto channel = asChannel()) {
		return channel->canPostMessages();
	}
	return isSelf();
}

bool PeerData::canTransferGifts() const {
	if (const auto channel = asChannel()) {
		return channel->amCreator();
	}
	return isSelf();
}

bool PeerData::canEditMessagesIndefinitely() const {
	if (const auto user = asUser()) {
		return user->isSelf();
	} else if (isChat()) {
		return false;
	} else if (const auto channel = asChannel()) {
		return channel->isMegagroup()
			? channel->canPinMessages()
			: channel->canEditMessages();
	}
	Unexpected("Peer type in PeerData::canEditMessagesIndefinitely.");
}

bool PeerData::canExportChatHistory() const {
	if (isRepliesChat() || isVerifyCodes() || !allowsForwarding()) {
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

bool PeerData::autoTranslation() const {
	if (const auto channel = asChannel()) {
		return channel->autoTranslation();
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

void PeerData::clearBusinessBot() {
	if (const auto details = _barDetails.get()) {
		if (details->requestChatDate
			|| details->paysPerMessage
			|| !details->phoneCountryCode.isEmpty()) {
			details->businessBot = nullptr;
			details->businessBotManageUrl = QString();
		} else {
			_barDetails = nullptr;
		}
	}
	if (const auto settings = barSettings()) {
		setBarSettings(*settings
			& ~PeerBarSetting::BusinessBotPaused
			& ~PeerBarSetting::BusinessBotCanReply
			& ~PeerBarSetting::HasBusinessBot);
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

void PeerData::setBarSettings(const MTPPeerSettings &data) {
	data.match([&](const MTPDpeerSettings &data) {
		const auto wasPaysPerMessage = paysPerMessage();
		if (!data.vbusiness_bot_id()
			&& !data.vrequest_chat_title()
			&& !data.vcharge_paid_message_stars()
			&& !data.vphone_country()
			&& !data.vregistration_month()
			&& !data.vname_change_date()
			&& !data.vphoto_change_date()) {
			_barDetails = nullptr;
		} else if (!_barDetails) {
			_barDetails = std::make_unique<PeerBarDetails>();
		}
		if (_barDetails) {
			_barDetails->phoneCountryCode
				= qs(data.vphone_country().value_or_empty());
			_barDetails->registrationDate = ParseRegistrationDate(
				data.vregistration_month().value_or_empty());
			_barDetails->nameChangeDate
				= data.vname_change_date().value_or_empty();
			_barDetails->photoChangeDate
				= data.vphoto_change_date().value_or_empty();
			_barDetails->requestChatTitle
				= qs(data.vrequest_chat_title().value_or_empty());
			_barDetails->requestChatDate
				= data.vrequest_chat_date().value_or_empty();
			_barDetails->businessBot = data.vbusiness_bot_id()
				? _owner->user(data.vbusiness_bot_id()->v).get()
				: nullptr;
			_barDetails->businessBotManageUrl
				= qs(data.vbusiness_bot_manage_url().value_or_empty());
			_barDetails->paysPerMessage
				= data.vcharge_paid_message_stars().value_or_empty();
		}
		using Flag = PeerBarSetting;
		setBarSettings((data.is_add_contact() ? Flag::AddContact : Flag())
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
			| (data.vbusiness_bot_id() ? Flag::HasBusinessBot : Flag())
			| (data.is_request_chat_broadcast()
				? Flag::RequestChatIsBroadcast
				: Flag())
			| (data.is_business_bot_paused()
				? Flag::BusinessBotPaused
				: Flag())
			| (data.is_business_bot_can_reply()
				? Flag::BusinessBotCanReply
				: Flag()));
		if (wasPaysPerMessage != paysPerMessage()) {
			session().changes().peerUpdated(
				this,
				UpdateFlag::PaysPerMessage);
		}
	});
}

void PeerData::setBarSettings(PeerBarSettings which) {
	const auto was = hideLinks();
	_barSettings.set(which);
	if (was && !hideLinks()) {
		if (const auto history = owner().historyLoaded(this)) {
			crl::on_main(&history->session(), [=] {
				history->refreshHiddenLinksItems();
			});
		}
	}
}

int PeerData::paysPerMessage() const {
	return _barDetails ? _barDetails->paysPerMessage : 0;
}

void PeerData::clearPaysPerMessage() {
	if (const auto details = _barDetails.get()) {
		if (details->paysPerMessage) {
			if (details->businessBot
				|| details->requestChatDate
				|| !details->phoneCountryCode.isEmpty()) {
				details->paysPerMessage = 0;
			} else {
				_barDetails = nullptr;
			}
			session().changes().peerUpdated(
				this,
				UpdateFlag::PaysPerMessage);
		}
	}
}

bool PeerData::hideLinks() const {
	//if (!isUser()) {
	//	return false;
	//}
	const auto settings = barSettings();
	return !settings || (*settings & PeerBarSetting::ReportSpam);
}

QString PeerData::requestChatTitle() const {
	return _barDetails ? _barDetails->requestChatTitle : QString();
}

TimeId PeerData::requestChatDate() const {
	return _barDetails ? _barDetails->requestChatDate : 0;
}

UserData *PeerData::businessBot() const {
	return _barDetails ? _barDetails->businessBot : nullptr;
}

QString PeerData::businessBotManageUrl() const {
	return _barDetails ? _barDetails->businessBotManageUrl : QString();
}

QString PeerData::phoneCountryCode() const {
	return _barDetails ? _barDetails->phoneCountryCode : QString();
}

int PeerData::registrationMonth() const {
	return _barDetails
		? RegistrationMonth(_barDetails->registrationDate)
		: 0;
}

int PeerData::registrationYear() const {
	return _barDetails ? RegistrationYear(_barDetails->registrationDate) : 0;
}

TimeId PeerData::nameChangeDate() const {
	return _barDetails ? _barDetails->nameChangeDate : 0;
}

TimeId PeerData::photoChangeDate() const {
	return _barDetails ? _barDetails->photoChangeDate : 0;
}

bool PeerData::changeBackgroundEmojiId(
		const tl::conditional<MTPlong> &cloudBackgroundEmoji) {
	return changeBackgroundEmojiId(cloudBackgroundEmoji
		? cloudBackgroundEmoji->v
		: DocumentId());
}

bool PeerData::changeColorCollectible(
		const tl::conditional<MTPPeerColor> &cloudColor) {
	if (!cloudColor) {
		return clearColorCollectible();
	}
	return cloudColor->match([&](const MTPDpeerColorCollectible &data) {
		return changeColorCollectible(Data::ParseColorCollectible(data));
	}, [&](const MTPDpeerColor &) {
		return clearColorCollectible();
	}, [&](const MTPDinputPeerColorCollectible &) {
		return clearColorCollectible();
	});
}

bool PeerData::changeColor(
		const tl::conditional<MTPPeerColor> &cloudColor) {
	const auto changed1 = cloudColor
		? changeColorIndex(Data::ColorIndexFromColor(cloudColor))
		: clearColorIndex();
	const auto changed2 = changeBackgroundEmojiId(
		Data::BackgroundEmojiIdFromColor(cloudColor));
	const auto changed3 = changeColorCollectible(cloudColor);
	return changed1 || changed2 || changed3;
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
		} else if (isVerifyCodes()) {
			const auto english = u"Verification Codes"_q;
			const auto localized = tr::lng_verification_codes(tr::now);
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

UserData *PeerData::asBot() {
	return isBot() ? static_cast<UserData*>(this) : nullptr;
}

const UserData *PeerData::asBot() const {
	return isBot()
		? static_cast<const UserData*>(this)
		: nullptr;
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

ChannelData *PeerData::asMonoforum() {
	const auto channel = asMegagroup();
	return (channel && channel->isMonoforum()) ? channel : nullptr;
}

const ChannelData *PeerData::asMonoforum() const {
	const auto channel = asMegagroup();
	return (channel && channel->isMonoforum()) ? channel : nullptr;
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

not_null<PeerData*> PeerData::userpicPaintingPeer() {
	if (const auto broadcast = monoforumBroadcast()) {
		return broadcast;
	}
	return this;
}

not_null<const PeerData*> PeerData::userpicPaintingPeer() const {
	return const_cast<PeerData*>(this)->userpicPaintingPeer();
}

Ui::PeerUserpicShape PeerData::userpicShape() const {
	return isForum()
		? Ui::PeerUserpicShape::Forum
		: isMonoforum()
		? Ui::PeerUserpicShape::Monoforum
		: Ui::PeerUserpicShape::Circle;
}

ChannelData *PeerData::monoforumBroadcast() const {
	const auto monoforum = asMonoforum();
	return monoforum ? monoforum->monoforumLink() : nullptr;
}

ChannelData *PeerData::broadcastMonoforum() const {
	const auto broadcast = asBroadcast();
	return broadcast ? broadcast->monoforumLink() : nullptr;
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
	} else if (const auto broadcast = monoforumBroadcast()) {
		return broadcast->name();
	}
	return _name;
}

const QString &PeerData::shortName() const {
	if (const auto user = asUser()) {
		return user->firstName.isEmpty() ? user->lastName : user->firstName;
	} else if (const auto to = migrateTo()) {
		return to->shortName();
	} else if (const auto broadcast = monoforumBroadcast()) {
		return broadcast->shortName();
	}
	return _name;
}

QString PeerData::username() const {
	if (const auto user = asUser()) {
		return user->username();
	} else if (const auto channel = asChannel()) {
		return channel->username();
	}
	return QString();
}

QString PeerData::editableUsername() const {
	if (const auto user = asUser()) {
		return user->editableUsername();
	} else if (const auto channel = asChannel()) {
		return channel->editableUsername();
	}
	return QString();
}

const std::vector<QString> &PeerData::usernames() const {
	if (const auto user = asUser()) {
		return user->usernames();
	} else if (const auto channel = asChannel()) {
		return channel->usernames();
	}
	static const auto kEmpty = std::vector<QString>();
	return kEmpty;
}

bool PeerData::isUsernameEditable(QString username) const {
	if (const auto user = asUser()) {
		return user->isUsernameEditable(username);
	} else if (const auto channel = asChannel()) {
		return channel->isUsernameEditable(username);
	}
	return false;
}

bool PeerData::changeColorCollectible(Ui::ColorCollectible data) {
	if (!_colorCollectible || (*_colorCollectible != data)) {
		// We don't reuse allocated object because in ChatStyle we
		// cache colors using std::weak_ptr as a key.
		_colorCollectible = std::make_shared<Ui::ColorCollectible>(
			std::move(data));
		return true;
	}
	return false;
}

bool PeerData::clearColorCollectible() {
	if (!_colorCollectible) {
		return false;
	}
	_colorCollectible = nullptr;
	return true;
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

void PeerData::setEmojiStatus(const MTPEmojiStatus &status) {
	const auto parsed = owner().emojiStatuses().parse(status);
	setEmojiStatus(parsed.id, parsed.until);
}

void PeerData::setEmojiStatus(EmojiStatusId emojiStatusId, TimeId until) {
	if (_emojiStatusId != emojiStatusId) {
		_emojiStatusId = emojiStatusId;
		session().changes().peerUpdated(this, UpdateFlag::EmojiStatus);
	}
	owner().emojiStatuses().registerAutomaticClear(this, until);
}

EmojiStatusId PeerData::emojiStatusId() const {
	return _emojiStatusId;
}

bool PeerData::isBot() const {
	if (const auto user = asUser()) {
		return user->isBot();
	}
	return false;
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
	if (const auto bot = asBot()) {
		return bot->isForum();
	} else if (const auto channel = asChannel()) {
		return channel->isForum();
	}
	return false;
}

bool PeerData::isMonoforum() const {
	if (const auto channel = asChannel()) {
		return channel->isMonoforum();
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

bool PeerData::isVerifyCodes() const {
	constexpr auto kVerifyCodesId = peerFromUser(489000);
	return (id == kVerifyCodesId);
}

bool PeerData::isFreezeAppealChat() const {
	return username().compare(u"spambot"_q, Qt::CaseInsensitive) == 0;
}

bool PeerData::sharedMediaInfo() const {
	return isSelf() || isRepliesChat();
}

bool PeerData::savedSublistsInfo() const {
	return isSelf() && owner().savedMessages().supported();
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

Ui::BotVerifyDetails *PeerData::botVerifyDetails() const {
	if (const auto user = asUser()) {
		return user->botVerifyDetails();
	} else if (const auto channel = asChannel()) {
		return channel->botVerifyDetails();
	}
	return nullptr;
}

Data::Forum *PeerData::forum() const {
	if (const auto bot = asBot()) {
		return bot->forum();
	} else if (const auto channel = asChannel()) {
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

Data::SavedMessages *PeerData::monoforum() const {
	if (const auto channel = asChannel()) {
		return channel->monoforum();
	}
	return nullptr;
}

Data::SavedSublist *PeerData::monoforumSublistFor(
		PeerId sublistPeerId) const {
	if (!sublistPeerId) {
		return nullptr;
	} else if (const auto monoforum = this->monoforum()) {
		return monoforum->sublistLoaded(owner().peer(sublistPeerId));
	}
	return nullptr;
}

bool PeerData::useSubsectionTabs() const {
	if (const auto bot = asBot()) {
		return bot->isForum();
	} else if (const auto channel = asChannel()) {
		return channel->useSubsectionTabs();
	}
	return false;
}

bool PeerData::viewForumAsMessages() const {
	if (const auto channel = asChannel()) {
		return channel->viewForumAsMessages();
	}
	return false;
}

void PeerData::processTopics(const MTPVector<MTPForumTopic> &topics) {
	if (const auto forum = this->forum()) {
		forum->applyReceivedTopics(topics);
	}
}

bool PeerData::allowsForwarding() const {
	if (isUser()) {
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
		if (user->requiresPremiumToWrite() && !user->session().premium()) {
			return Result::Explicit();
		}
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
		if (channel->monoforumDisabled()) {
			return Result::WithEveryone();
		}
		const auto defaultRestrictions = channel->defaultRestrictions()
			| (channel->isPublic()
				? (ChatRestriction::PinMessages
					| ChatRestriction::ChangeInfo)
				: ChatRestrictions(0));
		return (channel->amCreator() || allowByAdminRights(right, channel))
			? Result::Allowed()
			: ((defaultRestrictions & right)
				&& !channel->unrestrictedByBoosts())
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
	if (const auto channel = asChannel()) {
		return channel->isBroadcast()
			? !channel->signatureProfiles()
			: (channel->adminRights() & ChatAdminRight::Anonymous);
	}
	return false;
}

bool PeerData::canRevokeFullHistory() const {
	if (const auto user = asUser()) {
		return !isSelf()
			&& (!user->isBot() || user->isSupport())
			&& !user->isInaccessible()
			&& session().serverConfig().revokePrivateInbox
			&& (session().serverConfig().revokePrivateTimeLimit == 0x7FFFFFFF);
	} else if (const auto chat = asChat()) {
		return chat->amCreator();
	} else if (const auto megagroup = asMegagroup()) {
		return megagroup->amCreator()
			&& megagroup->membersCountKnown()
			&& megagroup->canDelete()
			&& !megagroup->isMonoforum();
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
		if (group->isMonoforum()) {
			return false;
		}
		return group->amCreator()
			|| (group->adminRights() & ChatAdminRight::ManageCall);
	}
	return false;
}

bool PeerData::amMonoforumAdmin() const {
	if (const auto channel = asChannel()) {
		return channel->flags() & ChannelDataFlag::MonoforumAdmin;
	}
	return false;
}

int PeerData::starsPerMessage() const {
	if (const auto user = asUser()) {
		return user->starsPerMessage();
	} else if (const auto channel = asChannel()) {
		return channel->starsPerMessage();
	}
	return 0;
}

int PeerData::starsPerMessageChecked() const {
	if (const auto channel = asChannel()) {
		if (channel->adminRights()
			|| channel->amCreator()
			|| amMonoforumAdmin()) {
			return 0;
		}
	}
	return starsPerMessage();
}

Data::StarsRating PeerData::starsRating() const {
	if (const auto user = asUser()) {
		return user->starsRating();
	}
	return {};
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

void PeerData::setThemeToken(const QString &token) {
	if (_themeToken == token) {
		return;
	} else if (!token.startsWith(u"gift:"_q)
		&& Ui::Emoji::Find(_themeToken) == Ui::Emoji::Find(token)) {
		_themeToken = token;
		return;
	}
	_themeToken = token;
	if (!token.isEmpty() && !owner().cloudThemes().themeForToken(token)) {
		owner().cloudThemes().refreshChatThemesFor(token);
	}
	session().changes().peerUpdated(this, UpdateFlag::ChatThemeToken);
}

const QString &PeerData::themeToken() const {
	return _themeToken;
}

void PeerData::setWallPaper(
		std::optional<Data::WallPaper> paper,
		bool overriden) {
	const auto paperChanged = (paper || _wallPaper)
		&& (!paper || !_wallPaper || !_wallPaper->equals(*paper));
	if (paperChanged) {
		_wallPaper = paper
			? std::make_unique<Data::WallPaper>(std::move(*paper))
			: nullptr;
	}

	const auto overridenValue = overriden ? 1 : 0;
	const auto overridenChanged = (_wallPaperOverriden != overridenValue);
	if (overridenChanged) {
		_wallPaperOverriden = overridenValue;
	}

	if (paperChanged || overridenChanged) {
		session().changes().peerUpdated(this, UpdateFlag::ChatWallPaper);
	}
}

bool PeerData::wallPaperOverriden() const {
	return _wallPaperOverriden != 0;
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

int PeerData::peerGiftsCount() const {
	if (const auto user = asUser()) {
		return user->peerGiftsCount();
	} else if (const auto channel = asChannel()) {
		return channel->peerGiftsCount();
	}
	return 0;
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
			PeerId(0), // monoforumPeerId
			0);
		session.saveSettingsDelayed();
	}
	session.storage().add(Storage::SharedMediaAddExisting(
		peer->id,
		MsgId(0), // topicRootId
		PeerId(0), // monoforumPeerId
		Storage::SharedMediaType::Pinned,
		messageId,
		{ messageId, ServerMaxMsgId }));
	peer->owner().history(peer)->setHasPinnedMessages(true);
}

FullMsgId ResolveTopPinnedId(
		not_null<PeerData*> peer,
		MsgId topicRootId,
		PeerId monoforumPeerId,
		PeerData *migrated) {
	const auto slice = peer->session().storage().snapshot(
		Storage::SharedMediaQuery(
			Storage::SharedMediaKey(
				peer->id,
				topicRootId,
				monoforumPeerId,
				Storage::SharedMediaType::Pinned,
				ServerMaxMsgId - 1),
			1,
			1));
	const auto old = (!topicRootId && !monoforumPeerId && migrated)
		? migrated->session().storage().snapshot(
			Storage::SharedMediaQuery(
				Storage::SharedMediaKey(
					migrated->id,
					MsgId(0), // topicRootId
					PeerId(0), // monoforumPeerId
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
		PeerId monoforumPeerId,
		PeerData *migrated) {
	const auto slice = peer->session().storage().snapshot(
		Storage::SharedMediaQuery(
			Storage::SharedMediaKey(
				peer->id,
				topicRootId,
				monoforumPeerId,
				Storage::SharedMediaType::Pinned,
				1),
			1,
			1));
	const auto old = (!topicRootId && !monoforumPeerId && migrated)
		? migrated->session().storage().snapshot(
			Storage::SharedMediaQuery(
				Storage::SharedMediaKey(
					migrated->id,
					MsgId(0), // topicRootId
					PeerId(0), // monoforumPeerId
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

uint64 BackgroundEmojiIdFromColor(const MTPPeerColor *color) {
	if (!color) {
		return 0;
	}
	return color->match([](const MTPDpeerColor &data) -> uint64 {
		return data.vbackground_emoji_id().value_or_empty();
	}, [](const MTPDpeerColorCollectible &data) -> uint64 {
		return data.vbackground_emoji_id().v;
	}, [](const MTPDinputPeerColorCollectible &data) -> uint64 {
		return 0;
	});
}

uint8 ColorIndexFromColor(const MTPPeerColor *color) {
	if (!color) {
		return 0;
	}
	return color->match([](const MTPDpeerColor &data) -> uint8 {
		return data.vcolor().value_or_empty();
	}, [](const MTPDpeerColorCollectible &data) -> uint8 {
		return 0;
	}, [](const MTPDinputPeerColorCollectible &) -> uint8 {
		return 0;
	});
}

} // namespace Data
