/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "structs.h"

#include "lang/lang_keys.h"
#include "inline_bots/inline_bot_layout_item.h"
#include "observer_peer.h"
#include "mainwidget.h"
#include "application.h"
#include "storage/file_upload.h"
#include "mainwindow.h"
#include "core/file_utilities.h"
#include "apiwrap.h"
#include "boxes/confirm_box.h"
#include "media/media_audio.h"
#include "storage/localstorage.h"
#include "history/history_media_types.h"
#include "styles/style_history.h"
#include "window/themes/window_theme.h"
#include "auth_session.h"
#include "messenger.h"
#include "storage/file_download.h"

namespace {

constexpr auto kUpdateFullPeerTimeout = TimeMs(5000); // Not more than once in 5 seconds.

int peerColorIndex(const PeerId &peer) {
	auto myId = AuthSession::CurrentUserId();
	auto peerId = peerToBareInt(peer);
	auto both = (QByteArray::number(peerId) + QByteArray::number(myId)).mid(0, 15);
	uchar md5[16];
	hashMd5(both.constData(), both.size(), md5);
	return (md5[peerId & 0x0F] & (peerIsUser(peer) ? 0x07 : 0x03));
}

ImagePtr generateUserpicImage(const style::icon &icon) {
	auto data = QImage(icon.size() * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	data.setDevicePixelRatio(cRetinaFactor());
	{
		Painter p(&data);
		icon.paint(p, 0, 0, icon.width());
	}
	return ImagePtr(App::pixmapFromImageInPlace(std::move(data)), "PNG");
}

} // namespace

style::color peerUserpicColor(int index) {
	static style::color peerColors[kUserColorsCount] = {
		st::historyPeer1UserpicBg,
		st::historyPeer2UserpicBg,
		st::historyPeer3UserpicBg,
		st::historyPeer4UserpicBg,
		st::historyPeer5UserpicBg,
		st::historyPeer6UserpicBg,
		st::historyPeer7UserpicBg,
		st::historyPeer8UserpicBg,
	};
	return peerColors[index];
}

class EmptyUserpic::Impl {
public:
	Impl(int index, const QString &name) : _color(peerUserpicColor(index)) {
		fillString(name);
	}

	void paint(Painter &p, int x, int y, int size);
	void paintRounded(Painter &p, int x, int y, int size);
	void paintSquare(Painter &p, int x, int y, int size);
	StorageKey uniqueKey() const;

private:
	template <typename PaintBackground>
	void paint(Painter &p, int x, int y, int size, PaintBackground paintBackground);

	void fillString(const QString &name);

	style::color _color;
	QString _string;

};

template <typename PaintBackground>
void EmptyUserpic::Impl::paint(Painter &p, int x, int y, int size, PaintBackground paintBackground) {
	auto fontsize = (size * 13) / 33;
	auto font = st::historyPeerUserpicFont->f;
	font.setPixelSize(fontsize);

	PainterHighQualityEnabler hq(p);
	p.setBrush(_color);
	p.setPen(Qt::NoPen);
	paintBackground();

	p.setFont(font);
	p.setBrush(Qt::NoBrush);
	p.setPen(st::historyPeerUserpicFg);
	p.drawText(QRect(x, y, size, size), _string, QTextOption(style::al_center));
}

void EmptyUserpic::Impl::paint(Painter &p, int x, int y, int size) {
	paint(p, x, y, size, [&p, x, y, size] {
		p.drawEllipse(x, y, size, size);
	});
}

void EmptyUserpic::Impl::paintRounded(Painter &p, int x, int y, int size) {
	paint(p, x, y, size, [&p, x, y, size] {
		p.drawRoundedRect(x, y, size, size, st::buttonRadius, st::buttonRadius);
	});
}

void EmptyUserpic::Impl::paintSquare(Painter &p, int x, int y, int size) {
	paint(p, x, y, size, [&p, x, y, size] {
		p.fillRect(x, y, size, size, p.brush());
	});
}

StorageKey EmptyUserpic::Impl::uniqueKey() const {
	auto first = 0xFFFFFFFF00000000ULL | anim::getPremultiplied(_color->c);
	auto second = uint64(0);
	memcpy(&second, _string.constData(), qMin(sizeof(second), _string.size() * sizeof(QChar)));
	return StorageKey(first, second);
}

void EmptyUserpic::Impl::fillString(const QString &name) {
	QList<QString> letters;
	QList<int> levels;
	auto level = 0;
	auto letterFound = false;
	auto ch = name.constData(), end = ch + name.size();
	while (ch != end) {
		auto emojiLength = 0;
		if (auto emoji = Ui::Emoji::Find(ch, end, &emojiLength)) {
			ch += emojiLength;
		} else if (ch->isHighSurrogate()) {
			++ch;
			if (ch != end && ch->isLowSurrogate()) {
				++ch;
			}
		} else if (!letterFound && ch->isLetterOrNumber()) {
			letterFound = true;
			if (ch + 1 != end && chIsDiac(*(ch + 1))) {
				letters.push_back(QString(ch, 2));
				levels.push_back(level);
				++ch;
			} else {
				letters.push_back(QString(ch, 1));
				levels.push_back(level);
			}
			++ch;
		} else {
			if (*ch == ' ') {
				level = 0;
				letterFound = false;
			} else if (letterFound && *ch == '-') {
				level = 1;
				letterFound = true;
			}
			++ch;
		}
	}

	// We prefer the second letter to be after ' ', but it can also be after '-'.
	_string = QString();
	if (!letters.isEmpty()) {
		_string += letters.front();
		auto bestIndex = 0;
		auto bestLevel = 2;
		for (auto i = letters.size(); i != 1;) {
			if (levels[--i] < bestLevel) {
				bestIndex = i;
				bestLevel = levels[i];
			}
		}
		if (bestIndex > 0) {
			_string += letters[bestIndex];
		}
	}
	_string = _string.toUpper();
}

EmptyUserpic::EmptyUserpic() = default;

EmptyUserpic::EmptyUserpic(int index, const QString &name) : _impl(std::make_unique<Impl>(index, name)) {
}

void EmptyUserpic::set(int index, const QString &name) {
	_impl = std::make_unique<Impl>(index, name);
}

void EmptyUserpic::clear() {
	_impl.reset();
}

void EmptyUserpic::paint(Painter &p, int x, int y, int outerWidth, int size) const {
	Expects(_impl != nullptr);
	_impl->paint(p, rtl() ? (outerWidth - x - size) : x, y, size);
}

void EmptyUserpic::paintRounded(Painter &p, int x, int y, int outerWidth, int size) const {
	Expects(_impl != nullptr);
	_impl->paintRounded(p, rtl() ? (outerWidth - x - size) : x, y, size);
}

void EmptyUserpic::paintSquare(Painter &p, int x, int y, int outerWidth, int size) const {
	Expects(_impl != nullptr);
	_impl->paintSquare(p, rtl() ? (outerWidth - x - size) : x, y, size);
}

StorageKey EmptyUserpic::uniqueKey() const {
	Expects(_impl != nullptr);
	return _impl->uniqueKey();
}

QPixmap EmptyUserpic::generate(int size) {
	auto result = QImage(QSize(size, size) * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(cRetinaFactor());
	result.fill(Qt::transparent);
	{
		Painter p(&result);
		paint(p, 0, 0, size, size);
	}
	return App::pixmapFromImageInPlace(std::move(result));
}

EmptyUserpic::~EmptyUserpic() = default;

using UpdateFlag = Notify::PeerUpdate::Flag;

NotifySettings globalNotifyAll, globalNotifyUsers, globalNotifyChats;
NotifySettingsPtr globalNotifyAllPtr = UnknownNotifySettings, globalNotifyUsersPtr = UnknownNotifySettings, globalNotifyChatsPtr = UnknownNotifySettings;

PeerClickHandler::PeerClickHandler(gsl::not_null<PeerData*> peer) : _peer(peer) {
}

void PeerClickHandler::onClick(Qt::MouseButton button) const {
	if (button == Qt::LeftButton && App::main()) {
		if (_peer && _peer->isChannel() && App::main()->historyPeer() != _peer) {
			if (!_peer->asChannel()->isPublic() && !_peer->asChannel()->amIn()) {
				Ui::show(Box<InformBox>(lang((_peer->isMegagroup()) ? lng_group_not_accessible : lng_channel_not_accessible)));
			} else {
				Ui::showPeerHistory(_peer, ShowAtUnreadMsgId, Ui::ShowWay::Forward);
			}
		} else {
			Ui::showPeerProfile(_peer);
		}
	}
}

PeerData::PeerData(const PeerId &id) : id(id), _colorIndex(peerColorIndex(id)) {
	nameText.setText(st::msgNameStyle, QString(), _textNameOptions);
	_userpicEmpty.set(_colorIndex, QString());
}

void PeerData::updateNameDelayed(const QString &newName, const QString &newNameOrPhone, const QString &newUsername) {
	if (name == newName) {
		if (isUser()) {
			if (asUser()->nameOrPhone == newNameOrPhone && asUser()->username == newUsername) {
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

	++nameVersion;
	name = newName;
	nameText.setText(st::msgNameStyle, name, _textNameOptions);
	if (!_userpic) {
		_userpicEmpty.set(_colorIndex, name);
	}

	Notify::PeerUpdate update(this);
	update.flags |= UpdateFlag::NameChanged;
	update.oldNames = names;
	update.oldNameFirstChars = chars;

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
				asChannel()->flags &= ~MTPDchannel::Flag::f_username;
			} else {
				asChannel()->flags |= MTPDchannel::Flag::f_username;
			}
			update.flags |= UpdateFlag::UsernameChanged;
		}
	}
	fillNames();
	if (App::main()) {
		emit App::main()->peerNameChanged(this, update.oldNames, update.oldNameFirstChars);
	}
	Notify::peerUpdatedDelayed(update);
}

ClickHandlerPtr PeerData::createOpenLink() {
	return MakeShared<PeerClickHandler>(this);
}

void PeerData::setUserpic(ImagePtr userpic) {
	_userpic = userpic;
	if (!_userpic || !_userpic->loaded()) {
		_userpicEmpty.set(_colorIndex, name);
	} else {
		_userpicEmpty.clear();
	}
}

ImagePtr PeerData::currentUserpic() const {
	if (_userpic) {
		_userpic->load();
		if (_userpic->loaded()) {
			_userpicEmpty.clear();
			return _userpic;
		}
	}
	return ImagePtr();
}

void PeerData::paintUserpic(Painter &p, int x, int y, int size) const {
	if (auto userpic = currentUserpic()) {
		p.drawPixmap(x, y, userpic->pixCircled(size, size));
	} else {
		_userpicEmpty.paint(p, x, y, x + size + x, size);
	}
}

void PeerData::paintUserpicRounded(Painter &p, int x, int y, int size) const {
	if (auto userpic = currentUserpic()) {
		p.drawPixmap(x, y, userpic->pixRounded(size, size, ImageRoundRadius::Small));
	} else {
		_userpicEmpty.paintRounded(p, x, y, x + size + x, size);
	}
}

void PeerData::paintUserpicSquare(Painter &p, int x, int y, int size) const {
	if (auto userpic = currentUserpic()) {
		p.drawPixmap(x, y, userpic->pix(size, size));
	} else {
		_userpicEmpty.paintSquare(p, x, y, x + size + x, size);
	}
}

StorageKey PeerData::userpicUniqueKey() const {
	if (photoLoc.isNull() || !_userpic || !_userpic->loaded()) {
		return _userpicEmpty.uniqueKey();
	}
	return storageKey(photoLoc);
}

void PeerData::saveUserpic(const QString &path, int size) const {
	genUserpic(size).save(path, "PNG");
}

void PeerData::saveUserpicRounded(const QString &path, int size) const {
	genUserpicRounded(size).save(path, "PNG");
}

QPixmap PeerData::genUserpic(int size) const {
	if (auto userpic = currentUserpic()) {
		return userpic->pixCircled(size, size);
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
		return userpic->pixRounded(size, size, ImageRoundRadius::Small);
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

const Text &BotCommand::descriptionText() const {
	if (_descriptionText.isEmpty() && !_description.isEmpty()) {
		_descriptionText.setText(st::defaultTextStyle, _description, _textNameOptions);
	}
	return _descriptionText;
}

bool UserData::canShareThisContact() const {
	return canShareThisContactFast() || !App::phoneFromSharedContact(peerToUser(id)).isEmpty();
}

void UserData::setPhoto(const MTPUserProfilePhoto &p) { // see Local::readPeer as well
	PhotoId newPhotoId = photoId;
	ImagePtr newPhoto = _userpic;
	StorageImageLocation newPhotoLoc = photoLoc;
	switch (p.type()) {
	case mtpc_userProfilePhoto: {
		const auto &d(p.c_userProfilePhoto());
		newPhotoId = d.vphoto_id.v;
		newPhotoLoc = App::imageLocation(160, 160, d.vphoto_small);
		newPhoto = newPhotoLoc.isNull() ? ImagePtr() : ImagePtr(newPhotoLoc);
		//App::feedPhoto(App::photoFromUserPhoto(peerToUser(id), MTP_int(unixtime()), p));
	} break;
	default: {
		newPhotoId = 0;
		if (id == ServiceUserId) {
			if (!_userpic) {
				newPhoto = ImagePtr(App::pixmapFromImageInPlace(Messenger::Instance().logoNoMargin().scaledToWidth(160, Qt::SmoothTransformation)), "PNG");
			}
		} else {
			newPhoto = ImagePtr();
		}
		newPhotoLoc = StorageImageLocation();
	} break;
	}
	if (newPhotoId != photoId || newPhoto.v() != _userpic.v() || newPhotoLoc != photoLoc) {
		photoId = newPhotoId;
		setUserpic(newPhoto);
		photoLoc = newPhotoLoc;
		if (App::main()) {
			emit App::main()->peerPhotoChanged(this);
		}
		Notify::peerUpdatedDelayed(this, UpdateFlag::PhotoChanged);
	}
}

void PeerData::fillNames() {
	names.clear();
	chars.clear();
	auto toIndex = TextUtilities::RemoveAccents(name);
	if (cRussianLetters().match(toIndex).hasMatch()) {
		toIndex += ' ' + translitRusEng(toIndex);
	}
	if (isUser()) {
		if (!asUser()->nameOrPhone.isEmpty() && asUser()->nameOrPhone != name) toIndex += ' ' + TextUtilities::RemoveAccents(asUser()->nameOrPhone);
		if (!asUser()->username.isEmpty()) toIndex += ' ' + TextUtilities::RemoveAccents(asUser()->username);
	} else if (isChannel()) {
		if (!asChannel()->username.isEmpty()) toIndex += ' ' + TextUtilities::RemoveAccents(asChannel()->username);
	}
	toIndex += ' ' + rusKeyboardLayoutSwitch(toIndex);

	auto namesList = TextUtilities::PrepareSearchWords(toIndex);
	for (auto &name : namesList) {
		names.insert(name);
		chars.insert(name[0]);
	}
}

bool UserData::setAbout(const QString &newAbout) {
	if (_about == newAbout) {
		return false;
	}
	_about = newAbout;
	Notify::peerUpdatedDelayed(this, UpdateFlag::AboutChanged);
	return true;
}

void UserData::setCommonChatsCount(int count) {
	if (_commonChatsCount != count) {
		_commonChatsCount = count;
		Notify::peerUpdatedDelayed(this, UpdateFlag::UserCommonChatsChanged);
	}
}

void UserData::setName(const QString &newFirstName, const QString &newLastName, const QString &newPhoneName, const QString &newUsername) {
	bool changeName = !newFirstName.isEmpty() || !newLastName.isEmpty();

	QString newFullName;
	if (changeName && newFirstName.trimmed().isEmpty()) {
		firstName = newLastName;
		lastName = QString();
		newFullName = firstName;
	} else {
		if (changeName) {
			firstName = newFirstName;
			lastName = newLastName;
		}
		newFullName = lastName.isEmpty() ? firstName : lng_full_name(lt_first_name, firstName, lt_last_name, lastName);
	}
	updateNameDelayed(newFullName, newPhoneName, newUsername);
}

void UserData::setPhone(const QString &newPhone) {
	_phone = newPhone;
}

void UserData::setBotInfoVersion(int version) {
	if (version < 0) {
		if (botInfo) {
			if (!botInfo->commands.isEmpty()) {
				botInfo->commands.clear();
				Notify::botCommandsChanged(this);
			}
			botInfo = nullptr;
			Notify::userIsBotChanged(this);
		}
	} else if (!botInfo) {
		botInfo = std::make_unique<BotInfo>();
		botInfo->version = version;
		Notify::userIsBotChanged(this);
	} else if (botInfo->version < version) {
		if (!botInfo->commands.isEmpty()) {
			botInfo->commands.clear();
			Notify::botCommandsChanged(this);
		}
		botInfo->description.clear();
		botInfo->version = version;
		botInfo->inited = false;
	}
}

void UserData::setBotInfo(const MTPBotInfo &info) {
	switch (info.type()) {
	case mtpc_botInfo: {
		const auto &d(info.c_botInfo());
		if (peerFromUser(d.vuser_id.v) != id || !botInfo) return;

		QString desc = qs(d.vdescription);
		if (botInfo->description != desc) {
			botInfo->description = desc;
			botInfo->text = Text(st::msgMinWidth);
		}

		auto &v = d.vcommands.v;
		botInfo->commands.reserve(v.size());
		auto changedCommands = false;
		int32 j = 0;
		for (int32 i = 0, l = v.size(); i < l; ++i) {
			if (v.at(i).type() != mtpc_botCommand) continue;

			QString cmd = qs(v.at(i).c_botCommand().vcommand), desc = qs(v.at(i).c_botCommand().vdescription);
			if (botInfo->commands.size() <= j) {
				botInfo->commands.push_back(BotCommand(cmd, desc));
				changedCommands = true;
			} else {
				if (botInfo->commands[j].command != cmd) {
					botInfo->commands[j].command = cmd;
					changedCommands = true;
				}
				if (botInfo->commands[j].setDescription(desc)) {
					changedCommands = true;
				}
			}
			++j;
		}
		while (j < botInfo->commands.size()) {
			botInfo->commands.pop_back();
			changedCommands = true;
		}

		botInfo->inited = true;

		if (changedCommands) {
			Notify::botCommandsChanged(this);
		}
	} break;
	}
}

void UserData::setNameOrPhone(const QString &newNameOrPhone) {
	if (nameOrPhone != newNameOrPhone) {
		nameOrPhone = newNameOrPhone;
		phoneText.setText(st::msgNameStyle, nameOrPhone, _textNameOptions);
	}
}

void UserData::madeAction(TimeId when) {
	if (botInfo || isServiceUser(id) || when <= 0) return;

	if (onlineTill <= 0 && -onlineTill < when) {
		onlineTill = -when - SetOnlineAfterActivity;
		App::markPeerUpdated(this);
		Notify::peerUpdatedDelayed(this, Notify::PeerUpdate::Flag::UserOnlineChanged);
	} else if (onlineTill > 0 && onlineTill < when + 1) {
		onlineTill = when + SetOnlineAfterActivity;
		App::markPeerUpdated(this);
		Notify::peerUpdatedDelayed(this, Notify::PeerUpdate::Flag::UserOnlineChanged);
	}
}

void UserData::setBlockStatus(BlockStatus blockStatus) {
	if (blockStatus != _blockStatus) {
		_blockStatus = blockStatus;
		Notify::peerUpdatedDelayed(this, UpdateFlag::UserIsBlocked);
	}
}

void UserData::setCallsStatus(CallsStatus callsStatus) {
	if (callsStatus != _callsStatus) {
		_callsStatus = callsStatus;
		Notify::peerUpdatedDelayed(this, UpdateFlag::UserHasCalls);
	}
}

bool UserData::hasCalls() const {
	return (callsStatus() != CallsStatus::Disabled) && (callsStatus() != CallsStatus::Unknown);
}

void ChatData::setPhoto(const MTPChatPhoto &p, const PhotoId &phId) { // see Local::readPeer as well
	PhotoId newPhotoId = photoId;
	ImagePtr newPhoto = _userpic;
	StorageImageLocation newPhotoLoc = photoLoc;
	switch (p.type()) {
	case mtpc_chatPhoto: {
		const auto &d(p.c_chatPhoto());
		if (phId != UnknownPeerPhotoId) {
			newPhotoId = phId;
		}
		newPhotoLoc = App::imageLocation(160, 160, d.vphoto_small);
		newPhoto = newPhotoLoc.isNull() ? ImagePtr() : ImagePtr(newPhotoLoc);
//		photoFull = newPhoto ? ImagePtr(640, 640, d.vphoto_big, ImagePtr()) : ImagePtr();
	} break;
	default: {
		newPhotoId = 0;
		newPhotoLoc = StorageImageLocation();
		newPhoto = ImagePtr();
//		photoFull = ImagePtr();
	} break;
	}
	if (newPhotoId != photoId || newPhoto.v() != _userpic.v() || newPhotoLoc != photoLoc) {
		photoId = newPhotoId;
		setUserpic(newPhoto);
		photoLoc = newPhotoLoc;
		if (App::main()) {
			emit App::main()->peerPhotoChanged(this);
		}
		Notify::peerUpdatedDelayed(this, UpdateFlag::PhotoChanged);
	}
}

void ChatData::setName(const QString &newName) {
	updateNameDelayed(newName.isEmpty() ? name : newName, QString(), QString());
}

void ChatData::invalidateParticipants() {
	auto wasCanEdit = canEdit();
	participants.clear();
	admins.clear();
	flags &= ~MTPDchat::Flag::f_admin;
	invitedByMe.clear();
	botStatus = 0;
	if (wasCanEdit != canEdit()) {
		Notify::peerUpdatedDelayed(this, Notify::PeerUpdate::Flag::ChatCanEdit);
	}
	Notify::peerUpdatedDelayed(this, Notify::PeerUpdate::Flag::MembersChanged | Notify::PeerUpdate::Flag::AdminsChanged);
}

void ChatData::setInviteLink(const QString &newInviteLink) {
	if (newInviteLink != _inviteLink) {
		_inviteLink = newInviteLink;
		Notify::peerUpdatedDelayed(this, UpdateFlag::InviteLinkChanged);
	}
}

void ChannelData::setPhoto(const MTPChatPhoto &p, const PhotoId &phId) { // see Local::readPeer as well
	PhotoId newPhotoId = photoId;
	ImagePtr newPhoto = _userpic;
	StorageImageLocation newPhotoLoc = photoLoc;
	switch (p.type()) {
	case mtpc_chatPhoto: {
		const auto &d(p.c_chatPhoto());
		if (phId != UnknownPeerPhotoId) {
			newPhotoId = phId;
		}
		newPhotoLoc = App::imageLocation(160, 160, d.vphoto_small);
		newPhoto = newPhotoLoc.isNull() ? ImagePtr() : ImagePtr(newPhotoLoc);
//		photoFull = newPhoto ? ImagePtr(640, 640, d.vphoto_big, newPhoto) : ImagePtr();
	} break;
	default: {
		newPhotoId = 0;
		newPhotoLoc = StorageImageLocation();
		newPhoto = ImagePtr();
//		photoFull = ImagePtr();
	} break;
	}
	if (newPhotoId != photoId || newPhoto.v() != _userpic.v() || newPhotoLoc != photoLoc) {
		photoId = newPhotoId;
		setUserpic(newPhoto);
		photoLoc = newPhotoLoc;
		if (App::main()) {
			emit App::main()->peerPhotoChanged(this);
		}
		Notify::peerUpdatedDelayed(this, UpdateFlag::PhotoChanged);
	}
}

void ChannelData::setName(const QString &newName, const QString &newUsername) {
	updateNameDelayed(newName.isEmpty() ? name : newName, QString(), newUsername);
}

void PeerData::updateFull() {
	if (!_lastFullUpdate || getms(true) > _lastFullUpdate + kUpdateFullPeerTimeout) {
		updateFullForced();
	}
}

void PeerData::updateFullForced() {
	if (App::api()) {
		App::api()->requestFullPeer(this);
		if (auto channel = asChannel()) {
			if (!channel->amCreator() && !channel->inviter) {
				App::api()->requestSelfParticipant(channel);
			}
		}
	}
}

void PeerData::fullUpdated() {
	_lastFullUpdate = getms(true);
}

bool ChannelData::setAbout(const QString &newAbout) {
	if (_about == newAbout) {
		return false;
	}
	_about = newAbout;
	Notify::peerUpdatedDelayed(this, UpdateFlag::AboutChanged);
	return true;
}

void ChannelData::setInviteLink(const QString &newInviteLink) {
	if (newInviteLink != _inviteLink) {
		_inviteLink = newInviteLink;
		Notify::peerUpdatedDelayed(this, UpdateFlag::InviteLinkChanged);
	}
}

void ChannelData::setMembersCount(int newMembersCount) {
	if (_membersCount != newMembersCount) {
		if (isMegagroup() && !mgInfo->lastParticipants.isEmpty()) {
			mgInfo->lastParticipantsStatus |= MegagroupInfo::LastParticipantsCountOutdated;
			mgInfo->lastParticipantsCount = membersCount();
		}
		_membersCount = newMembersCount;
		Notify::peerUpdatedDelayed(this, Notify::PeerUpdate::Flag::MembersChanged);
	}
}

void ChannelData::setAdminsCount(int newAdminsCount) {
	if (_adminsCount != newAdminsCount) {
		_adminsCount = newAdminsCount;
		Notify::peerUpdatedDelayed(this, Notify::PeerUpdate::Flag::AdminsChanged);
	}
}

void ChannelData::setRestrictedCount(int newRestrictedCount) {
	if (_restrictedCount != newRestrictedCount) {
		_restrictedCount = newRestrictedCount;
		Notify::peerUpdatedDelayed(this, Notify::PeerUpdate::Flag::BannedUsersChanged);
	}
}

void ChannelData::setKickedCount(int newKickedCount) {
	if (_kickedCount != newKickedCount) {
		_kickedCount = newKickedCount;
		Notify::peerUpdatedDelayed(this, Notify::PeerUpdate::Flag::BannedUsersChanged);
	}
}

MTPChannelBannedRights ChannelData::KickedRestrictedRights() {
	using Flag = MTPDchannelBannedRights::Flag;
	auto flags = Flag::f_view_messages | Flag::f_send_messages | Flag::f_send_media | Flag::f_embed_links | Flag::f_send_stickers | Flag::f_send_gifs | Flag::f_send_games | Flag::f_send_inline;
	return MTP_channelBannedRights(MTP_flags(flags), MTP_int(std::numeric_limits<int32>::max()));
}

void ChannelData::applyEditAdmin(gsl::not_null<UserData*> user, const MTPChannelAdminRights &oldRights, const MTPChannelAdminRights &newRights) {
	auto flags = Notify::PeerUpdate::Flag::AdminsChanged | Notify::PeerUpdate::Flag::None;
	if (mgInfo) {
		if (!mgInfo->lastParticipants.contains(user)) { // If rights are empty - still add participant? TODO check
			mgInfo->lastParticipants.push_front(user);
			setMembersCount(membersCount() + 1);
			if (user->botInfo && !mgInfo->bots.contains(user)) {
				mgInfo->bots.insert(user);
				if (mgInfo->botStatus != 0 && mgInfo->botStatus < 2) {
					mgInfo->botStatus = 2;
				}
			}
		}
		if (mgInfo->lastRestricted.contains(user)) { // If rights are empty - still remove restrictions? TODO check
			mgInfo->lastRestricted.remove(user);
			if (restrictedCount() > 0) {
				setRestrictedCount(restrictedCount() - 1);
			}
		}
		auto it = mgInfo->lastAdmins.find(user);
		if (newRights.c_channelAdminRights().vflags.v != 0) {
			auto lastAdmin = MegagroupInfo::Admin { newRights };
			lastAdmin.canEdit = true;
			if (it == mgInfo->lastAdmins.cend()) {
				mgInfo->lastAdmins.insert(user, lastAdmin);
				setAdminsCount(adminsCount() + 1);
			} else {
				it.value() = lastAdmin;
			}
		} else {
			if (it != mgInfo->lastAdmins.cend()) {
				mgInfo->lastAdmins.erase(it);
				if (adminsCount() > 0) {
					setAdminsCount(adminsCount() - 1);
				}
			}
		}
	}
	if (oldRights.c_channelAdminRights().vflags.v && !newRights.c_channelAdminRights().vflags.v) {
		// We removed an admin.
		if (adminsCount() > 1) {
			setAdminsCount(adminsCount() - 1);
			if (App::main()) emit App::main()->peerUpdated(this);
		}
		if (!isMegagroup() && user->botInfo && membersCount() > 1) {
			// Removing bot admin removes it from channel.
			setMembersCount(membersCount() - 1);
			if (App::main()) emit App::main()->peerUpdated(this);
		}
	} else if (!oldRights.c_channelAdminRights().vflags.v && newRights.c_channelAdminRights().vflags.v) {
		// We added an admin.
		setAdminsCount(adminsCount() + 1);
		if (App::main()) emit App::main()->peerUpdated(this);
		updateFullForced();
	}
	Notify::peerUpdatedDelayed(this, flags);
}

void ChannelData::applyEditBanned(gsl::not_null<UserData*> user, const MTPChannelBannedRights &oldRights, const MTPChannelBannedRights &newRights) {
	auto flags = Notify::PeerUpdate::Flag::BannedUsersChanged | Notify::PeerUpdate::Flag::None;
	if (mgInfo) {
		if (mgInfo->lastAdmins.contains(user)) { // If rights are empty - still remove admin? TODO check
			mgInfo->lastAdmins.remove(user);
			if (adminsCount() > 1) {
				setAdminsCount(adminsCount() - 1);
			} else {
				flags |= Notify::PeerUpdate::Flag::AdminsChanged;
			}
		}
		auto isKicked = (newRights.c_channelBannedRights().vflags.v & MTPDchannelBannedRights::Flag::f_view_messages);
		auto isRestricted = !isKicked && (newRights.c_channelBannedRights().vflags.v != 0);
		auto it = mgInfo->lastRestricted.find(user);
		if (isRestricted) {
			if (it == mgInfo->lastRestricted.cend()) {
				mgInfo->lastRestricted.insert(user, MegagroupInfo::Restricted { newRights });
				setRestrictedCount(restrictedCount() + 1);
			} else {
				it->rights = newRights;
			}
		} else {
			if (it != mgInfo->lastRestricted.cend()) {
				mgInfo->lastRestricted.erase(it);
				if (restrictedCount() > 0) {
					setRestrictedCount(restrictedCount() - 1);
				}
			}
			if (isKicked) {
				auto i = mgInfo->lastParticipants.indexOf(user);
				if (i >= 0) {
					mgInfo->lastParticipants.removeAt(i);
				}
				if (membersCount() > 1) {
					setMembersCount(membersCount() - 1);
				} else {
					mgInfo->lastParticipantsStatus |= MegagroupInfo::LastParticipantsCountOutdated;
					mgInfo->lastParticipantsCount = 0;
				}
				setKickedCount(kickedCount() + 1);
				if (mgInfo->bots.contains(user)) {
					mgInfo->bots.remove(user);
					if (mgInfo->bots.isEmpty() && mgInfo->botStatus > 0) {
						mgInfo->botStatus = -1;
					}
				}
				flags |= Notify::PeerUpdate::Flag::MembersChanged;
			}
		}
	}
	Notify::peerUpdatedDelayed(this, flags);
}

void ChannelData::flagsUpdated() {
	if (isMegagroup()) {
		if (!mgInfo) {
			mgInfo = std::make_unique<MegagroupInfo>();
		}
	} else if (mgInfo) {
		mgInfo = nullptr;
	}
}

bool ChannelData::canNotEditLastAdmin(gsl::not_null<UserData*> user) const {
	if (mgInfo) {
		auto i = mgInfo->lastAdmins.constFind(user);
		if (i != mgInfo->lastAdmins.cend()) {
			return !i->canEdit;
		}
		return (user == mgInfo->creator);
	}
	return false;
}

bool ChannelData::canEditAdmin(gsl::not_null<UserData*> user) const {
	if (user->isSelf()) {
		return false;
	} else if (amCreator()) {
		return true;
	} else if (canNotEditLastAdmin(user)) {
		return false;
	}
	return adminRights().is_add_admins();
}

bool ChannelData::canRestrictUser(gsl::not_null<UserData*> user) const {
	if (user->isSelf()) {
		return false;
	} else if (amCreator()) {
		return true;
	} else if (canNotEditLastAdmin(user)) {
		return false;
	}
	return adminRights().is_ban_users();
}

void ChannelData::setAdminRights(const MTPChannelAdminRights &rights) {
	if (rights.c_channelAdminRights().vflags.v == _adminRights.c_channelAdminRights().vflags.v) {
		return;
	}
	_adminRights = rights;
	if (isMegagroup()) {
		if (hasAdminRights()) {
			if (!amCreator()) {
				auto me = MegagroupInfo::Admin { _adminRights };
				me.canEdit = false;
				mgInfo->lastAdmins.insert(App::self(), me);
			}
			mgInfo->lastRestricted.remove(App::self());
		} else {
			mgInfo->lastAdmins.remove(App::self());
		}
	}
	Notify::peerUpdatedDelayed(this, UpdateFlag::ChannelRightsChanged | UpdateFlag::AdminsChanged | UpdateFlag::BannedUsersChanged);
}

void ChannelData::setRestrictedRights(const MTPChannelBannedRights &rights) {
	if (rights.c_channelBannedRights().vflags.v == _restrictedRights.c_channelBannedRights().vflags.v
		&& rights.c_channelBannedRights().vuntil_date.v == _restrictedRights.c_channelBannedRights().vuntil_date.v) {
		return;
	}
	_restrictedRights = rights;
	if (isMegagroup()) {
		if (hasRestrictedRights()) {
			if (!amCreator()) {
				auto me = MegagroupInfo::Restricted { _restrictedRights };
				mgInfo->lastRestricted.insert(App::self(), me);
			}
			mgInfo->lastAdmins.remove(App::self());
		} else {
			mgInfo->lastRestricted.remove(App::self());
		}
	}
	Notify::peerUpdatedDelayed(this, UpdateFlag::ChannelRightsChanged | UpdateFlag::AdminsChanged | UpdateFlag::BannedUsersChanged);
}

uint64 PtsWaiter::ptsKey(PtsSkippedQueue queue, int32 pts) {
	return _queue.insert(uint64(uint32(pts)) << 32 | (++_skippedKey), queue).key();
}

void PtsWaiter::setWaitingForSkipped(ChannelData *channel, int32 ms) {
	if (ms >= 0) {
		if (App::main()) {
			App::main()->ptsWaiterStartTimerFor(channel, ms);
		}
		_waitingForSkipped = true;
	} else {
		_waitingForSkipped = false;
		checkForWaiting(channel);
	}
}

void PtsWaiter::setWaitingForShortPoll(ChannelData *channel, int32 ms) {
	if (ms >= 0) {
		if (App::main()) {
			App::main()->ptsWaiterStartTimerFor(channel, ms);
		}
		_waitingForShortPoll = true;
	} else {
		_waitingForShortPoll = false;
		checkForWaiting(channel);
	}
}

void PtsWaiter::checkForWaiting(ChannelData *channel) {
	if (!_waitingForSkipped && !_waitingForShortPoll && App::main()) {
		App::main()->ptsWaiterStartTimerFor(channel, -1);
	}
}

void PtsWaiter::applySkippedUpdates(ChannelData *channel) {
	if (!_waitingForSkipped) return;

	setWaitingForSkipped(channel, -1);

	if (!App::api() || _queue.isEmpty()) return;

	++_applySkippedLevel;
	for (QMap<uint64, PtsSkippedQueue>::const_iterator i = _queue.cbegin(), e = _queue.cend(); i != e; ++i) {
		switch (i.value()) {
		case SkippedUpdate: App::api()->applyUpdateNoPtsCheck(_updateQueue.value(i.key())); break;
		case SkippedUpdates: App::api()->applyUpdatesNoPtsCheck(_updatesQueue.value(i.key())); break;
		}
	}
	--_applySkippedLevel;
	clearSkippedUpdates();
}

void PtsWaiter::clearSkippedUpdates() {
	_queue.clear();
	_updateQueue.clear();
	_updatesQueue.clear();
	_applySkippedLevel = 0;
}

bool PtsWaiter::updated(ChannelData *channel, int32 pts, int32 count, const MTPUpdates &updates) {
	if (_requesting || _applySkippedLevel) {
		return true;
	} else if (pts <= _good && count > 0) {
		return false;
	} else if (check(channel, pts, count)) {
		return true;
	}
	_updatesQueue.insert(ptsKey(SkippedUpdates, pts), updates);
	return false;
}

bool PtsWaiter::updated(ChannelData *channel, int32 pts, int32 count, const MTPUpdate &update) {
	if (_requesting || _applySkippedLevel) {
		return true;
	} else if (pts <= _good && count > 0) {
		return false;
	} else if (check(channel, pts, count)) {
		return true;
	}
	_updateQueue.insert(ptsKey(SkippedUpdate, pts), update);
	return false;
}

bool PtsWaiter::updated(ChannelData *channel, int32 pts, int32 count) {
	if (_requesting || _applySkippedLevel) {
		return true;
	} else if (pts <= _good && count > 0) {
		return false;
	}
	return check(channel, pts, count);
}

bool PtsWaiter::updateAndApply(ChannelData *channel, int32 pts, int32 count, const MTPUpdates &updates) {
	if (!updated(channel, pts, count, updates)) {
		return false;
	}
	if (!_waitingForSkipped || _queue.isEmpty()) {
		// Optimization - no need to put in queue and back.
		App::api()->applyUpdatesNoPtsCheck(updates);
	} else {
		_updatesQueue.insert(ptsKey(SkippedUpdates, pts), updates);
		applySkippedUpdates(channel);
	}
	return true;
}

bool PtsWaiter::updateAndApply(ChannelData *channel, int32 pts, int32 count, const MTPUpdate &update) {
	if (!updated(channel, pts, count, update)) {
		return false;
	}
	if (!_waitingForSkipped || _queue.isEmpty()) {
		// Optimization - no need to put in queue and back.
		App::api()->applyUpdateNoPtsCheck(update);
	} else {
		_updateQueue.insert(ptsKey(SkippedUpdate, pts), update);
		applySkippedUpdates(channel);
	}
	return true;
}

bool PtsWaiter::updateAndApply(ChannelData *channel, int32 pts, int32 count) {
	if (!updated(channel, pts, count)) {
		return false;
	}
	applySkippedUpdates(channel);
	return true;
}

bool PtsWaiter::check(ChannelData *channel, int32 pts, int32 count) { // return false if need to save that update and apply later
	if (!inited()) {
		init(pts);
		return true;
	}

	_last = qMax(_last, pts);
	_count += count;
	if (_last == _count) {
		_good = _last;
		return true;
	} else if (_last < _count) {
		setWaitingForSkipped(channel, 1);
	} else {
		setWaitingForSkipped(channel, WaitForSkippedTimeout);
	}
	return !count;
}

PhotoData::PhotoData(const PhotoId &id, const uint64 &access, int32 date, const ImagePtr &thumb, const ImagePtr &medium, const ImagePtr &full)
: id(id)
, access(access)
, date(date)
, thumb(thumb)
, medium(medium)
, full(full) {
}

void PhotoData::automaticLoad(const HistoryItem *item) {
	full->automaticLoad(item);
}

void PhotoData::automaticLoadSettingsChanged() {
	full->automaticLoadSettingsChanged();
}

void PhotoData::download() {
	full->loadEvenCancelled();
	notifyLayoutChanged();
}

bool PhotoData::loaded() const {
	bool wasLoading = loading();
	if (full->loaded()) {
		if (wasLoading) {
			notifyLayoutChanged();
		}
		return true;
	}
	return false;
}

bool PhotoData::loading() const {
	return full->loading();
}

bool PhotoData::displayLoading() const {
	return full->loading() ? full->displayLoading() : uploading();
}

void PhotoData::cancel() {
	full->cancel();
	notifyLayoutChanged();
}

void PhotoData::notifyLayoutChanged() const {
	auto &items = App::photoItems();
	auto i = items.constFind(const_cast<PhotoData*>(this));
	if (i != items.cend()) {
		for_const (auto item, i.value()) {
			Notify::historyItemLayoutChanged(item);
		}
	}
}

float64 PhotoData::progress() const {
	if (uploading()) {
		if (uploadingData->size > 0) {
			return float64(uploadingData->offset) / uploadingData->size;
		}
		return 0;
	}
	return full->progress();
}

int32 PhotoData::loadOffset() const {
	return full->loadOffset();
}

bool PhotoData::uploading() const {
	return !!uploadingData;
}

void PhotoData::forget() {
	thumb->forget();
	replyPreview->forget();
	medium->forget();
	full->forget();
}

ImagePtr PhotoData::makeReplyPreview() {
	if (replyPreview->isNull() && !thumb->isNull()) {
		if (thumb->loaded()) {
			int w = thumb->width(), h = thumb->height();
			if (w <= 0) w = 1;
			if (h <= 0) h = 1;
			replyPreview = ImagePtr(w > h ? thumb->pix(w * st::msgReplyBarSize.height() / h, st::msgReplyBarSize.height()) : thumb->pix(st::msgReplyBarSize.height()), "PNG");
		} else {
			thumb->load();
		}
	}
	return replyPreview;
}

void PhotoOpenClickHandler::onClickImpl() const {
	App::wnd()->showPhoto(this, App::hoveredLinkItem() ? App::hoveredLinkItem() : App::contextItem());
}

void PhotoSaveClickHandler::onClickImpl() const {
	auto data = photo();
	if (!data->date) return;

	data->download();
}

void PhotoCancelClickHandler::onClickImpl() const {
	auto data = photo();
	if (!data->date) return;

	if (data->uploading()) {
		if (auto item = App::hoveredLinkItem() ? App::hoveredLinkItem() : (App::contextItem() ? App::contextItem() : nullptr)) {
			if (auto media = item->getMedia()) {
				if (media->type() == MediaTypePhoto && static_cast<HistoryPhoto*>(media)->photo() == data) {
					App::contextItem(item);
					App::main()->cancelUploadLayer();
				}
			}
		}
	} else {
		data->cancel();
	}
}

QString joinList(const QStringList &list, const QString &sep) {
	QString result;
	if (list.isEmpty()) return result;

	int32 l = list.size(), s = sep.size() * (l - 1);
	for (int32 i = 0; i < l; ++i) {
		s += list.at(i).size();
	}
	result.reserve(s);
	result.append(list.at(0));
	for (int32 i = 1; i < l; ++i) {
		result.append(sep).append(list.at(i));
	}
	return result;
}

QString saveFileName(const QString &title, const QString &filter, const QString &prefix, QString name, bool savingAs, const QDir &dir) {
#ifdef Q_OS_WIN
	name = name.replace(QRegularExpression(qsl("[\\\\\\/\\:\\*\\?\\\"\\<\\>\\|]")), qsl("_"));
#elif defined Q_OS_MAC
	name = name.replace(QRegularExpression(qsl("[\\:]")), qsl("_"));
#elif defined Q_OS_LINUX
	name = name.replace(QRegularExpression(qsl("[\\/]")), qsl("_"));
#endif
	if (Global::AskDownloadPath() || savingAs) {
		if (!name.isEmpty() && name.at(0) == QChar::fromLatin1('.')) {
			name = filedialogDefaultName(prefix, name);
		} else if (dir.path() != qsl(".")) {
			QString path = dir.absolutePath();
			if (path != cDialogLastPath()) {
				cSetDialogLastPath(path);
				Local::writeUserSettings();
			}
		}

		// check if extension of filename is present in filter
		// it should be in first filter section on the first place
		// place it there, if it is not
		QString ext = QFileInfo(name).suffix(), fil = filter, sep = qsl(";;");
		if (!ext.isEmpty()) {
			if (QRegularExpression(qsl("^[a-zA-Z_0-9]+$")).match(ext).hasMatch()) {
				QStringList filters = filter.split(sep);
				if (filters.size() > 1) {
					QString first = filters.at(0);
					int32 start = first.indexOf(qsl("(*."));
					if (start >= 0) {
						if (!QRegularExpression(qsl("\\(\\*\\.") + ext + qsl("[\\)\\s]"), QRegularExpression::CaseInsensitiveOption).match(first).hasMatch()) {
							QRegularExpressionMatch m = QRegularExpression(qsl(" \\*\\.") + ext + qsl("[\\)\\s]"), QRegularExpression::CaseInsensitiveOption).match(first);
							if (m.hasMatch() && m.capturedStart() > start + 3) {
								int32 oldpos = m.capturedStart(), oldend = m.capturedEnd();
								fil = first.mid(0, start + 3) + ext + qsl(" *.") + first.mid(start + 3, oldpos - start - 3) + first.mid(oldend - 1) + sep + joinList(filters.mid(1), sep);
							} else {
								fil = first.mid(0, start + 3) + ext + qsl(" *.") + first.mid(start + 3) + sep + joinList(filters.mid(1), sep);
							}
						}
					} else {
						fil = QString();
					}
				} else {
					fil = QString();
				}
			} else {
				fil = QString();
			}
		}
		return filedialogGetSaveFile(name, title, fil, name) ? name : QString();
	}

	QString path;
	if (Global::DownloadPath().isEmpty()) {
		path = psDownloadPath();
	} else if (Global::DownloadPath() == qsl("tmp")) {
		path = cTempDir();
	} else {
		path = Global::DownloadPath();
	}
	if (name.isEmpty()) name = qsl(".unknown");
	if (name.at(0) == QChar::fromLatin1('.')) {
		if (!QDir().exists(path)) QDir().mkpath(path);
		return filedialogDefaultName(prefix, name, path);
	}
	if (dir.path() != qsl(".")) {
		path = dir.absolutePath() + '/';
	}

	QString nameStart, extension;
	int32 extPos = name.lastIndexOf('.');
	if (extPos >= 0) {
		nameStart = name.mid(0, extPos);
		extension = name.mid(extPos);
	} else {
		nameStart = name;
	}
	QString nameBase = path + nameStart;
	name = nameBase + extension;
	for (int i = 0; QFileInfo(name).exists(); ++i) {
		name = nameBase + QString(" (%1)").arg(i + 2) + extension;
	}

	if (!QDir().exists(path)) QDir().mkpath(path);
	return name;
}

bool StickerData::setInstalled() const {
	switch (set.type()) {
	case mtpc_inputStickerSetID: {
		auto it = Global::StickerSets().constFind(set.c_inputStickerSetID().vid.v);
		return (it != Global::StickerSets().cend()) && !(it->flags & MTPDstickerSet::Flag::f_archived) && (it->flags & MTPDstickerSet::Flag::f_installed);
	} break;
	case mtpc_inputStickerSetShortName: {
		QString name = qs(set.c_inputStickerSetShortName().vshort_name).toLower();
		for (auto it = Global::StickerSets().cbegin(), e = Global::StickerSets().cend(); it != e; ++it) {
			if (it->shortName.toLower() == name) {
				return !(it->flags & MTPDstickerSet::Flag::f_archived) && (it->flags & MTPDstickerSet::Flag::f_installed);
			}
		}
	} break;
	}
	return false;
}

QString documentSaveFilename(const DocumentData *data, bool forceSavingAs = false, const QString already = QString(), const QDir &dir = QDir()) {
	auto alreadySavingFilename = data->loadingFilePath();
	if (!alreadySavingFilename.isEmpty()) {
		return alreadySavingFilename;
	}

	QString name, filter, caption, prefix;
	MimeType mimeType = mimeTypeForName(data->mime);
	QStringList p = mimeType.globPatterns();
	QString pattern = p.isEmpty() ? QString() : p.front();
	if (data->voice()) {
		bool mp3 = (data->mime == qstr("audio/mp3"));
		name = already.isEmpty() ? (mp3 ? qsl(".mp3") : qsl(".ogg")) : already;
		filter = mp3 ? qsl("MP3 Audio (*.mp3);;") : qsl("OGG Opus Audio (*.ogg);;");
		filter += FileDialog::AllFilesFilter();
		caption = lang(lng_save_audio);
		prefix = qsl("audio");
	} else if (data->isVideo()) {
		name = already.isEmpty() ? data->name : already;
		if (name.isEmpty()) {
			name = pattern.isEmpty() ? qsl(".mov") : pattern.replace('*', QString());
		}
		if (pattern.isEmpty()) {
			filter = qsl("MOV Video (*.mov);;") + FileDialog::AllFilesFilter();
		} else {
			filter = mimeType.filterString() + qsl(";;") + FileDialog::AllFilesFilter();
		}
		caption = lang(lng_save_video);
		prefix = qsl("video");
	} else {
		name = already.isEmpty() ? data->name : already;
		if (name.isEmpty()) {
			name = pattern.isEmpty() ? qsl(".unknown") : pattern.replace('*', QString());
		}
		if (pattern.isEmpty()) {
			filter = QString();
		} else {
			filter = mimeType.filterString() + qsl(";;") + FileDialog::AllFilesFilter();
		}
		caption = lang(data->song() ? lng_save_audio_file : lng_save_file);
		prefix = qsl("doc");
	}

	return saveFileName(caption, filter, prefix, name, forceSavingAs, dir);
}

void DocumentOpenClickHandler::doOpen(DocumentData *data, HistoryItem *context, ActionOnLoad action) {
	if (!data->date) return;

	auto msgId = context ? context->fullId() : FullMsgId();
	bool playVoice = data->voice();
	bool playMusic = data->tryPlaySong();
	bool playVideo = data->isVideo();
	bool playAnimation = data->isAnimation();
	auto &location = data->location(true);
	if (auto applyTheme = data->isTheme()) {
		if (!location.isEmpty() && location.accessEnable()) {
			App::wnd()->showDocument(data, context);
			location.accessDisable();
			return;
		}
	}
	if (!location.isEmpty() || (!data->data().isEmpty() && (playVoice || playMusic || playVideo || playAnimation))) {
		using State = Media::Player::State;
		if (playVoice) {
			auto state = Media::Player::mixer()->currentState(AudioMsgId::Type::Voice);
			if (state.id == AudioMsgId(data, msgId) && !Media::Player::IsStoppedOrStopping(state.state)) {
				if (Media::Player::IsPaused(state.state) || state.state == State::Pausing) {
					Media::Player::mixer()->resume(state.id);
				} else {
					Media::Player::mixer()->pause(state.id);
				}
			} else {
				auto audio = AudioMsgId(data, msgId);
				Media::Player::mixer()->play(audio);
				Media::Player::Updated().notify(audio);
				if (App::main()) {
					App::main()->mediaMarkRead(data);
				}
			}
		} else if (playMusic) {
			auto state = Media::Player::mixer()->currentState(AudioMsgId::Type::Song);
			if (state.id == AudioMsgId(data, msgId) && !Media::Player::IsStoppedOrStopping(state.state)) {
				if (Media::Player::IsPaused(state.state) || state.state == State::Pausing) {
					Media::Player::mixer()->resume(state.id);
				} else {
					Media::Player::mixer()->pause(state.id);
				}
			} else {
				auto song = AudioMsgId(data, msgId);
				Media::Player::mixer()->play(song);
				Media::Player::Updated().notify(song);
			}
		} else if (playVideo) {
			if (!data->data().isEmpty()) {
				App::wnd()->showDocument(data, context);
			} else if (location.accessEnable()) {
				App::wnd()->showDocument(data, context);
				location.accessDisable();
			} else {
				auto filepath = location.name();
				if (documentIsValidMediaFile(filepath)) {
					File::Launch(filepath);
				}
			}
			if (App::main()) App::main()->mediaMarkRead(data);
		} else if (data->voice() || data->song() || data->isVideo()) {
			auto filepath = location.name();
			if (documentIsValidMediaFile(filepath)) {
				File::Launch(filepath);
			}
			if (App::main()) App::main()->mediaMarkRead(data);
		} else if (data->size < App::kImageSizeLimit) {
			if (!data->data().isEmpty() && playAnimation) {
				if (action == ActionOnLoadPlayInline && context && context->getMedia()) {
					context->getMedia()->playInline();
				} else {
					App::wnd()->showDocument(data, context);
				}
			} else if (location.accessEnable()) {
				if (data->isAnimation() || QImageReader(location.name()).canRead()) {
					if (action == ActionOnLoadPlayInline && context && context->getMedia()) {
						context->getMedia()->playInline();
					} else {
						App::wnd()->showDocument(data, context);
					}
				} else {
					File::Launch(location.name());
				}
				location.accessDisable();
			} else {
				File::Launch(location.name());
			}
		} else {
			File::Launch(location.name());
		}
		return;
	}

	if (data->status != FileReady) return;

	QString filename;
	if (!data->saveToCache()) {
		filename = documentSaveFilename(data);
		if (filename.isEmpty()) return;
	}

	data->save(filename, action, msgId);
}

void DocumentOpenClickHandler::onClickImpl() const {
	auto item = App::hoveredLinkItem() ? App::hoveredLinkItem() : (App::contextItem() ? App::contextItem() : nullptr);
	doOpen(document(), item, document()->voice() ? ActionOnLoadNone : ActionOnLoadOpen);
}

void GifOpenClickHandler::onClickImpl() const {
	auto item = App::hoveredLinkItem() ? App::hoveredLinkItem() : (App::contextItem() ? App::contextItem() : nullptr);
	doOpen(document(), item, ActionOnLoadPlayInline);
}

void DocumentSaveClickHandler::doSave(DocumentData *data, bool forceSavingAs) {
	if (!data->date) return;

	auto filepath = data->filepath(DocumentData::FilePathResolveSaveFromDataSilent, forceSavingAs);
	if (!filepath.isEmpty() && !forceSavingAs) {
		File::OpenWith(filepath, QCursor::pos());
	} else {
		auto fileinfo = QFileInfo(filepath);
		auto filedir = filepath.isEmpty() ? QDir() : fileinfo.dir();
		auto filename = filepath.isEmpty() ? QString() : fileinfo.fileName();
		auto newfname = documentSaveFilename(data, forceSavingAs, filename, filedir);
		if (!newfname.isEmpty()) {
			auto action = (filename.isEmpty() || forceSavingAs) ? ActionOnLoadNone : ActionOnLoadOpenWith;
			auto actionMsgId = App::hoveredLinkItem() ? App::hoveredLinkItem()->fullId() : (App::contextItem() ? App::contextItem()->fullId() : FullMsgId());
			data->save(newfname, action, actionMsgId);
		}
	}
}

void DocumentSaveClickHandler::onClickImpl() const {
	doSave(document());
}

void DocumentCancelClickHandler::onClickImpl() const {
	auto data = document();
	if (!data->date) return;

	if (data->uploading()) {
		if (auto item = App::hoveredLinkItem() ? App::hoveredLinkItem() : (App::contextItem() ? App::contextItem() : nullptr)) {
			if (auto media = item->getMedia()) {
				if (media->getDocument() == data) {
					App::contextItem(item);
					App::main()->cancelUploadLayer();
				}
			}
		}
	} else {
		data->cancel();
	}
}

VoiceData::~VoiceData() {
	if (!waveform.isEmpty() && waveform.at(0) == -1 && waveform.size() > int32(sizeof(TaskId))) {
		TaskId taskId = 0;
		memcpy(&taskId, waveform.constData() + 1, sizeof(taskId));
		Local::cancelTask(taskId);
	}
}

DocumentData::DocumentData(DocumentId id, int32 dc, uint64 accessHash, int32 version, const QString &url, const QVector<MTPDocumentAttribute> &attributes)
: id(id)
, _dc(dc)
, _access(accessHash)
, _version(version)
, _url(url) {
	setattributes(attributes);
	if (_dc && _access) {
		_location = Local::readFileLocation(mediaKey());
	}
}

DocumentData *DocumentData::create(DocumentId id) {
	return new DocumentData(id, 0, 0, 0, QString(), QVector<MTPDocumentAttribute>());
}

DocumentData *DocumentData::create(DocumentId id, int32 dc, uint64 accessHash, int32 version, const QVector<MTPDocumentAttribute> &attributes) {
	return new DocumentData(id, dc, accessHash, version, QString(), attributes);
}

DocumentData *DocumentData::create(DocumentId id, const QString &url, const QVector<MTPDocumentAttribute> &attributes) {
	return new DocumentData(id, 0, 0, 0, url, attributes);
}

void DocumentData::setattributes(const QVector<MTPDocumentAttribute> &attributes) {
	for (int32 i = 0, l = attributes.size(); i < l; ++i) {
		switch (attributes[i].type()) {
		case mtpc_documentAttributeImageSize: {
			auto &d = attributes[i].c_documentAttributeImageSize();
			dimensions = QSize(d.vw.v, d.vh.v);
		} break;
		case mtpc_documentAttributeAnimated: if (type == FileDocument || type == StickerDocument || type == VideoDocument) {
			type = AnimatedDocument;
			_additional = nullptr;
		} break;
		case mtpc_documentAttributeSticker: {
			auto &d = attributes[i].c_documentAttributeSticker();
			if (type == FileDocument) {
				type = StickerDocument;
				_additional = std::make_unique<StickerData>();
			}
			if (sticker()) {
				sticker()->alt = qs(d.valt);
				if (sticker()->set.type() != mtpc_inputStickerSetID || d.vstickerset.type() == mtpc_inputStickerSetID) {
					sticker()->set = d.vstickerset;
				}
			}
		} break;
		case mtpc_documentAttributeVideo: {
			auto &d = attributes[i].c_documentAttributeVideo();
			if (type == FileDocument) {
				type = d.is_round_message() ? RoundVideoDocument : VideoDocument;
			}
			_duration = d.vduration.v;
			dimensions = QSize(d.vw.v, d.vh.v);
		} break;
		case mtpc_documentAttributeAudio: {
			auto &d = attributes[i].c_documentAttributeAudio();
			if (type == FileDocument) {
				if (d.is_voice()) {
					type = VoiceDocument;
					_additional = std::make_unique<VoiceData>();
				} else {
					type = SongDocument;
					_additional = std::make_unique<SongData>();
				}
			}
			if (voice()) {
				voice()->duration = d.vduration.v;
				VoiceWaveform waveform = documentWaveformDecode(qba(d.vwaveform));
				uchar wavemax = 0;
				for (int32 i = 0, l = waveform.size(); i < l; ++i) {
					uchar waveat = waveform.at(i);
					if (wavemax < waveat) wavemax = waveat;
				}
				voice()->waveform = waveform;
				voice()->wavemax = wavemax;
			} else if (song()) {
				song()->duration = d.vduration.v;
				song()->title = qs(d.vtitle);
				song()->performer = qs(d.vperformer);
			}
		} break;
		case mtpc_documentAttributeFilename: name = qs(attributes[i].c_documentAttributeFilename().vfile_name); break;
		}
	}
	if (type == StickerDocument) {
		if (dimensions.width() <= 0 || dimensions.height() <= 0 || dimensions.width() > StickerMaxSize || dimensions.height() > StickerMaxSize || size > StickerInMemory) {
			type = FileDocument;
			_additional = nullptr;
		}
	}
}

bool DocumentData::saveToCache() const {
	return (type == StickerDocument) || (isAnimation() && size < AnimationInMemory) || (voice() && size < AudioVoiceMsgInMemory);
}

void DocumentData::forget() {
	thumb->forget();
	if (sticker()) sticker()->img->forget();
	replyPreview->forget();
	_data.clear();
}

void DocumentData::automaticLoad(const HistoryItem *item) {
	if (loaded() || status != FileReady) return;

	if (saveToCache() && _loader != CancelledMtpFileLoader) {
		if (type == StickerDocument) {
			save(QString(), _actionOnLoad, _actionOnLoadMsgId);
		} else if (isAnimation()) {
			bool loadFromCloud = false;
			if (item) {
				if (item->history()->peer->isUser()) {
					loadFromCloud = !(cAutoDownloadGif() & dbiadNoPrivate);
				} else {
					loadFromCloud = !(cAutoDownloadGif() & dbiadNoGroups);
				}
			} else { // if load at least anywhere
				loadFromCloud = !(cAutoDownloadGif() & dbiadNoPrivate) || !(cAutoDownloadGif() & dbiadNoGroups);
			}
			save(QString(), _actionOnLoad, _actionOnLoadMsgId, loadFromCloud ? LoadFromCloudOrLocal : LoadFromLocalOnly, true);
		} else if (voice()) {
			if (item) {
				bool loadFromCloud = false;
				if (item->history()->peer->isUser()) {
					loadFromCloud = !(cAutoDownloadAudio() & dbiadNoPrivate);
				} else {
					loadFromCloud = !(cAutoDownloadAudio() & dbiadNoGroups);
				}
				save(QString(), _actionOnLoad, _actionOnLoadMsgId, loadFromCloud ? LoadFromCloudOrLocal : LoadFromLocalOnly, true);
			}
		}
	}
}

void DocumentData::automaticLoadSettingsChanged() {
	if (loaded() || status != FileReady || (!isAnimation() && !voice()) || !saveToCache() || _loader != CancelledMtpFileLoader) {
		return;
	}
	_loader = nullptr;
}

void DocumentData::performActionOnLoad() {
	if (_actionOnLoad == ActionOnLoadNone) return;

	auto loc = location(true);
	auto already = loc.name();
	auto item = _actionOnLoadMsgId.msg ? App::histItemById(_actionOnLoadMsgId) : nullptr;
	auto showImage = !isVideo() && (size < App::kImageSizeLimit);
	auto playVoice = voice() && (_actionOnLoad == ActionOnLoadPlayInline || _actionOnLoad == ActionOnLoadOpen);
	auto playMusic = tryPlaySong() && (_actionOnLoad == ActionOnLoadPlayInline || _actionOnLoad == ActionOnLoadOpen);
	auto playAnimation = isAnimation() && (_actionOnLoad == ActionOnLoadPlayInline || _actionOnLoad == ActionOnLoadOpen) && showImage && item && item->getMedia();
	if (auto applyTheme = isTheme()) {
		if (!loc.isEmpty() && loc.accessEnable()) {
			App::wnd()->showDocument(this, item);
			loc.accessDisable();
			return;
		}
	}
	using State = Media::Player::State;
	if (playVoice) {
		if (loaded()) {
			auto state = Media::Player::mixer()->currentState(AudioMsgId::Type::Voice);
			if (state.id == AudioMsgId(this, _actionOnLoadMsgId) && !Media::Player::IsStoppedOrStopping(state.state)) {
				if (Media::Player::IsPaused(state.state) || state.state == State::Pausing) {
					Media::Player::mixer()->resume(state.id);
				} else {
					Media::Player::mixer()->pause(state.id);
				}
			} else if (Media::Player::IsStopped(state.state)) {
				Media::Player::mixer()->play(AudioMsgId(this, _actionOnLoadMsgId));
				if (App::main()) App::main()->mediaMarkRead(this);
			}
		}
	} else if (playMusic) {
		if (loaded()) {
			auto state = Media::Player::mixer()->currentState(AudioMsgId::Type::Song);
			if (state.id == AudioMsgId(this, _actionOnLoadMsgId) && !Media::Player::IsStoppedOrStopping(state.state)) {
				if (Media::Player::IsPaused(state.state) || state.state == State::Pausing) {
					Media::Player::mixer()->resume(state.id);
				} else {
					Media::Player::mixer()->pause(state.id);
				}
			} else if (Media::Player::IsStopped(state.state)) {
				auto song = AudioMsgId(this, _actionOnLoadMsgId);
				Media::Player::mixer()->play(song);
				Media::Player::Updated().notify(song);
			}
		}
	} else if (playAnimation) {
		if (loaded()) {
			if (_actionOnLoad == ActionOnLoadPlayInline && item->getMedia()) {
				item->getMedia()->playInline();
			} else {
				App::wnd()->showDocument(this, item);
			}
		}
	} else {
		if (already.isEmpty()) return;

		if (_actionOnLoad == ActionOnLoadOpenWith) {
			File::OpenWith(already, QCursor::pos());
		} else if (_actionOnLoad == ActionOnLoadOpen || _actionOnLoad == ActionOnLoadPlayInline) {
			if (voice() || song() || isVideo()) {
				if (documentIsValidMediaFile(already)) {
					File::Launch(already);
				}
				if (App::main()) App::main()->mediaMarkRead(this);
			} else if (loc.accessEnable()) {
				if (showImage && QImageReader(loc.name()).canRead()) {
					if (_actionOnLoad == ActionOnLoadPlayInline && item && item->getMedia()) {
						item->getMedia()->playInline();
					} else {
						App::wnd()->showDocument(this, item);
					}
				} else {
					File::Launch(already);
				}
				loc.accessDisable();
			} else {
				File::Launch(already);
			}
		}
	}
	_actionOnLoad = ActionOnLoadNone;
}

bool DocumentData::loaded(FilePathResolveType type) const {
	if (loading() && _loader->finished()) {
		if (_loader->cancelled()) {
			destroyLoaderDelayed(CancelledMtpFileLoader);
		} else {
			auto that = const_cast<DocumentData*>(this);
			that->_location = FileLocation(_loader->fileName());
			that->_data = _loader->bytes();
			if (that->sticker() && !_loader->imagePixmap().isNull()) {
				that->sticker()->img = ImagePtr(_data, _loader->imageFormat(), _loader->imagePixmap());
			}
			destroyLoaderDelayed();
		}
		notifyLayoutChanged();
	}
	return !data().isEmpty() || !filepath(type).isEmpty();
}

void DocumentData::destroyLoaderDelayed(mtpFileLoader *newValue) const {
	_loader->stop();
	auto loader = std::unique_ptr<FileLoader>(std::exchange(_loader, newValue));
	AuthSession::Current().downloader().delayedDestroyLoader(std::move(loader));
}

bool DocumentData::loading() const {
	return _loader && _loader != CancelledMtpFileLoader;
}

QString DocumentData::loadingFilePath() const {
	return loading() ? _loader->fileName() : QString();
}

bool DocumentData::displayLoading() const {
	return loading() ? (!_loader->loadingLocal() || !_loader->autoLoading()) : uploading();
}

float64 DocumentData::progress() const {
	if (uploading()) {
		return snap((size > 0) ? float64(uploadOffset) / size : 0., 0., 1.);
	}
	return loading() ? _loader->currentProgress() : (loaded() ? 1. : 0.);
}

int32 DocumentData::loadOffset() const {
	return loading() ? _loader->currentOffset() : 0;
}

bool DocumentData::uploading() const {
	return status == FileUploading;
}

void DocumentData::save(const QString &toFile, ActionOnLoad action, const FullMsgId &actionMsgId, LoadFromCloudSetting fromCloud, bool autoLoading) {
	if (loaded(FilePathResolveChecked)) {
		auto &l = location(true);
		if (!toFile.isEmpty()) {
			if (!_data.isEmpty()) {
				QFile f(toFile);
				f.open(QIODevice::WriteOnly);
				f.write(_data);
				f.close();

				setLocation(FileLocation(toFile));
				Local::writeFileLocation(mediaKey(), FileLocation(toFile));
			} else if (l.accessEnable()) {
				auto alreadyName = l.name();
				if (alreadyName != toFile) {
					QFile(toFile).remove();
					QFile(alreadyName).copy(toFile);
				}
				l.accessDisable();
			}
		}
		_actionOnLoad = action;
		_actionOnLoadMsgId = actionMsgId;
		performActionOnLoad();
		return;
	}

	if (_loader == CancelledMtpFileLoader) _loader = nullptr;
	if (_loader) {
		if (!_loader->setFileName(toFile)) {
			cancel(); // changes _actionOnLoad
			_loader = nullptr;
		}
	}

	_actionOnLoad = action;
	_actionOnLoadMsgId = actionMsgId;
	if (_loader) {
		if (fromCloud == LoadFromCloudOrLocal) _loader->permitLoadFromCloud();
	} else {
		status = FileReady;
		if (!_access && !_url.isEmpty()) {
			_loader = new webFileLoader(_url, toFile, fromCloud, autoLoading);
		} else {
			_loader = new mtpFileLoader(_dc, id, _access, _version, locationType(), toFile, size, (saveToCache() ? LoadToCacheAsWell : LoadToFileOnly), fromCloud, autoLoading);
		}
		_loader->connect(_loader, SIGNAL(progress(FileLoader*)), App::main(), SLOT(documentLoadProgress(FileLoader*)));
		_loader->connect(_loader, SIGNAL(failed(FileLoader*,bool)), App::main(), SLOT(documentLoadFailed(FileLoader*,bool)));
		_loader->start();
	}
	notifyLayoutChanged();
}

void DocumentData::cancel() {
	if (!loading()) return;

	auto loader = std::unique_ptr<FileLoader>(std::exchange(_loader, CancelledMtpFileLoader));
	loader->cancel();
	loader->stop();
	AuthSession::Current().downloader().delayedDestroyLoader(std::move(loader));

	notifyLayoutChanged();
	if (auto main = App::main()) {
		main->documentLoadProgress(this);
	}

	_actionOnLoad = ActionOnLoadNone;
}

void DocumentData::notifyLayoutChanged() const {
	auto &items = App::documentItems();
	for (auto item : items.value(const_cast<DocumentData*>(this))) {
		Notify::historyItemLayoutChanged(item);
	}

	if (auto items = InlineBots::Layout::documentItems()) {
		for (auto item : items->value(const_cast<DocumentData*>(this))) {
			item->layoutChanged();
		}
	}
}

VoiceWaveform documentWaveformDecode(const QByteArray &encoded5bit) {
	auto bitsCount = static_cast<int>(encoded5bit.size() * 8);
	auto valuesCount = bitsCount / 5;
	if (!valuesCount) {
		return VoiceWaveform();
	}

	// Read each 5 bit of encoded5bit as 0-31 unsigned char.
	// We count the index of the byte in which the desired 5-bit sequence starts.
	// And then we read a uint16 starting from that byte to guarantee to get all of those 5 bits.
	//
	// BUT! if it is the last byte we have, we're not allowed to read a uint16 starting with it.
	// Because it will be an overflow (we'll access one byte after the available memory).
	// We see, that only the last 5 bits could start in the last available byte and be problematic.
	// So we read in a general way all the entries in a general way except the last one.
	auto result = VoiceWaveform(valuesCount, 0);
	auto bitsData = encoded5bit.constData();
	for (auto i = 0, l = valuesCount - 1; i != l; ++i) {
		auto byteIndex = (i * 5) / 8;
		auto bitShift = (i * 5) % 8;
		auto value = *reinterpret_cast<const uint16*>(bitsData + byteIndex);
		result[i] = static_cast<char>((value >> bitShift) & 0x1F);
	}
	auto lastByteIndex = ((valuesCount - 1) * 5) / 8;
	auto lastBitShift = ((valuesCount - 1) * 5) % 8;
	auto lastValue = (lastByteIndex == encoded5bit.size() - 1)
		? static_cast<uint16>(*reinterpret_cast<const uchar*>(bitsData + lastByteIndex))
		: *reinterpret_cast<const uint16*>(bitsData + lastByteIndex);
	result[valuesCount - 1] = static_cast<char>((lastValue >> lastBitShift) & 0x1F);

	return result;
}

QByteArray documentWaveformEncode5bit(const VoiceWaveform &waveform) {
	auto bitsCount = waveform.size() * 5;
	auto bytesCount = (bitsCount + 7) / 8;
	auto result = QByteArray(bytesCount + 1, 0);
	auto bitsData = result.data();

	// Write each 0-31 unsigned char as 5 bit to result.
	// We reserve one extra byte to be able to dereference any of required bytes
	// as a uint16 without overflowing, even the byte with index "bytesCount - 1".
	for (auto i = 0, l = waveform.size(); i < l; ++i) {
		auto byteIndex = (i * 5) / 8;
		auto bitShift = (i * 5) % 8;
		auto value = (static_cast<uint16>(waveform[i]) & 0x1F) << bitShift;
		*reinterpret_cast<uint16*>(bitsData + byteIndex) |= value;
	}
	result.resize(bytesCount);
	return result;
}

QByteArray DocumentData::data() const {
	return _data;
}

const FileLocation &DocumentData::location(bool check) const {
	if (check && !_location.check()) {
		const_cast<DocumentData*>(this)->_location = Local::readFileLocation(mediaKey());
	}
	return _location;
}

void DocumentData::setLocation(const FileLocation &loc) {
	if (loc.check()) {
		_location = loc;
	}
}

QString DocumentData::filepath(FilePathResolveType type, bool forceSavingAs) const {
	bool check = (type != FilePathResolveCached);
	QString result = (check && _location.name().isEmpty()) ? QString() : location(check).name();
	bool saveFromData = result.isEmpty() && !data().isEmpty();
	if (saveFromData) {
		if (type != FilePathResolveSaveFromData && type != FilePathResolveSaveFromDataSilent) {
			saveFromData = false;
		} else if (type == FilePathResolveSaveFromDataSilent && (Global::AskDownloadPath() || forceSavingAs)) {
			saveFromData = false;
		}
	}
	if (saveFromData) {
		QString filename = documentSaveFilename(this, forceSavingAs);
		if (!filename.isEmpty()) {
			QFile f(filename);
			if (f.open(QIODevice::WriteOnly)) {
				if (f.write(data()) == data().size()) {
					f.close();
					const_cast<DocumentData*>(this)->_location = FileLocation(filename);
					Local::writeFileLocation(mediaKey(), _location);
					result = filename;
				}
			}
		}
	}
	return result;
}

ImagePtr DocumentData::makeReplyPreview() {
	if (replyPreview->isNull() && !thumb->isNull()) {
		if (thumb->loaded()) {
			int w = thumb->width(), h = thumb->height();
			if (w <= 0) w = 1;
			if (h <= 0) h = 1;
			auto thumbSize = (w > h) ? QSize(w * st::msgReplyBarSize.height() / h, st::msgReplyBarSize.height()) : QSize(st::msgReplyBarSize.height(), h * st::msgReplyBarSize.height() / w);
			thumbSize *= cIntRetinaFactor();
			auto options = Images::Option::Smooth | (isRoundVideo() ? Images::Option::Circled : Images::Option::None) | Images::Option::TransparentBackground;
			auto outerSize = st::msgReplyBarSize.height();
			auto image = thumb->pixNoCache(thumbSize.width(), thumbSize.height(), options, outerSize, outerSize);
			replyPreview = ImagePtr(image, "PNG");
		} else {
			thumb->load();
		}
	}
	return replyPreview;
}

bool fileIsImage(const QString &name, const QString &mime) {
	QString lowermime = mime.toLower(), namelower = name.toLower();
	if (lowermime.startsWith(qstr("image/"))) {
		return true;
	} else if (namelower.endsWith(qstr(".bmp"))
		|| namelower.endsWith(qstr(".jpg"))
		|| namelower.endsWith(qstr(".jpeg"))
		|| namelower.endsWith(qstr(".gif"))
		|| namelower.endsWith(qstr(".webp"))
		|| namelower.endsWith(qstr(".tga"))
		|| namelower.endsWith(qstr(".tiff"))
		|| namelower.endsWith(qstr(".tif"))
		|| namelower.endsWith(qstr(".psd"))
		|| namelower.endsWith(qstr(".png"))) {
		return true;
	}
	return false;
}

void DocumentData::recountIsImage() {
	if (isAnimation() || isVideo()) {
		return;
	}
	_duration = fileIsImage(name, mime) ? 1 : -1; // hack
}

bool DocumentData::setRemoteVersion(int32 version) {
	if (_version == version) {
		return false;
	}
	_version = version;
	_location = FileLocation();
	_data = QByteArray();
	status = FileReady;
	if (loading()) {
		destroyLoaderDelayed();
	}
	return true;
}

void DocumentData::setRemoteLocation(int32 dc, uint64 access) {
	_dc = dc;
	_access = access;
	if (isValid()) {
		if (_location.check()) {
			Local::writeFileLocation(mediaKey(), _location);
		} else {
			_location = Local::readFileLocation(mediaKey());
		}
	}
}

void DocumentData::setContentUrl(const QString &url) {
	_url = url;
}

void DocumentData::collectLocalData(DocumentData *local) {
	if (local == this) return;

	if (!local->_data.isEmpty()) {
		_data = local->_data;
		if (voice()) {
			if (!Local::copyAudio(local->mediaKey(), mediaKey())) {
				Local::writeAudio(mediaKey(), _data);
			}
		} else {
			if (!Local::copyStickerImage(local->mediaKey(), mediaKey())) {
				Local::writeStickerImage(mediaKey(), _data);
			}
		}
	}
	if (!local->_location.isEmpty()) {
		_location = local->_location;
		Local::writeFileLocation(mediaKey(), _location);
	}
}

DocumentData::~DocumentData() {
	if (loading()) {
		destroyLoaderDelayed();
	}
}

QString DocumentData::composeNameString(const QString &filename, const QString &songTitle, const QString &songPerformer) {
	if (songTitle.isEmpty() && songPerformer.isEmpty()) {
		return filename.isEmpty() ? qsl("Unknown File") : filename;
	}

	if (songPerformer.isEmpty()) {
		return songTitle;
	}

	auto trackTitle = (songTitle.isEmpty() ? qsl("Unknown Track") : songTitle);
	return songPerformer + QString::fromUtf8(" \xe2\x80\x93 ") + trackTitle;
}

WebPageData::WebPageData(const WebPageId &id, WebPageType type, const QString &url, const QString &displayUrl, const QString &siteName, const QString &title, const TextWithEntities &description, DocumentData *document, PhotoData *photo, int32 duration, const QString &author, int32 pendingTill) : id(id)
, type(type)
, url(url)
, displayUrl(displayUrl)
, siteName(siteName)
, title(title)
, description(description)
, duration(duration)
, author(author)
, photo(photo)
, document(document)
, pendingTill(pendingTill) {
}

GameData::GameData(const GameId &id, const uint64 &accessHash, const QString &shortName, const QString &title, const QString &description, PhotoData *photo, DocumentData *document) : id(id)
, accessHash(accessHash)
, shortName(shortName)
, title(title)
, description(description)
, photo(photo)
, document(document) {
}

MsgId clientMsgId() {
	static MsgId currentClientMsgId = StartClientMsgId;
	t_assert(currentClientMsgId < EndClientMsgId);
	return currentClientMsgId++;
}
