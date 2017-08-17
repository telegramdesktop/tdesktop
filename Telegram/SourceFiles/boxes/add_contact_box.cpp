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
#include "boxes/add_contact_box.h"

#include "styles/style_boxes.h"
#include "styles/style_dialogs.h"
#include "lang/lang_keys.h"
#include "messenger.h"
#include "mtproto/sender.h"
#include "base/flat_set.h"
#include "boxes/confirm_box.h"
#include "boxes/photo_crop_box.h"
#include "boxes/peer_list_controllers.h"
#include "core/file_utilities.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/labels.h"
#include "ui/toast/toast.h"
#include "ui/special_buttons.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "apiwrap.h"
#include "observer_peer.h"
#include "auth_session.h"

namespace {

constexpr auto kMaxGroupChannelTitle = 255;
constexpr auto kMaxChannelDescription = 255;
constexpr auto kMaxBioLength = 70;

style::InputField CreateBioFieldStyle() {
	auto result = st::newGroupDescription;
	result.textMargins.setRight(st::boxTextFont->spacew + st::boxTextFont->width(QString::number(kMaxBioLength)));
	return result;
}

} // namespace

QString PeerFloodErrorText(PeerFloodType type) {
	auto link = textcmdLink(
		Messenger::Instance().createInternalLinkFull(qsl("spambot")),
		lang(lng_cant_more_info));
	if (type == PeerFloodType::InviteGroup) {
		return lng_cant_invite_not_contact(lt_more_info, link);
	}
	return lng_cant_send_to_not_contact(lt_more_info, link);
}

class RevokePublicLinkBox::Inner : public TWidget, private MTP::Sender {
public:
	Inner(QWidget *parent, base::lambda<void()> revokeCallback);

protected:
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

private:
	struct ChatRow {
		ChatRow(not_null<PeerData*> peer) : peer(peer) {
		}

		not_null<PeerData*> peer;
		Text name, status;
	};
	void paintChat(Painter &p, const ChatRow &row, bool selected) const;
	void updateSelected();

	PeerData *_selected = nullptr;
	PeerData *_pressed = nullptr;

	std::vector<ChatRow> _rows;

	int _rowsTop = 0;
	int _rowHeight = 0;
	int _revokeWidth = 0;

	base::lambda<void()> _revokeCallback;
	mtpRequestId _revokeRequestId = 0;
	QPointer<ConfirmBox> _weakRevokeConfirmBox;

};

AddContactBox::AddContactBox(QWidget*, QString fname, QString lname, QString phone)
: _first(this, st::defaultInputField, langFactory(lng_signup_firstname), fname)
, _last(this, st::defaultInputField, langFactory(lng_signup_lastname), lname)
, _phone(this, st::defaultInputField, langFactory(lng_contact_phone), phone)
, _invertOrder(langFirstNameGoesSecond()) {
	if (!phone.isEmpty()) {
		_phone->setDisabled(true);
	}
}

AddContactBox::AddContactBox(QWidget*, UserData *user)
: _user(user)
, _first(this, st::defaultInputField, langFactory(lng_signup_firstname), user->firstName)
, _last(this, st::defaultInputField, langFactory(lng_signup_lastname), user->lastName)
, _phone(this, st::defaultInputField, langFactory(lng_contact_phone), user->phone())
, _invertOrder(langFirstNameGoesSecond()) {
	_phone->setDisabled(true);
}

void AddContactBox::prepare() {
	if (_invertOrder) {
		setTabOrder(_last, _first);
	}
	if (_user) {
		setTitle(langFactory(lng_edit_contact_title));
	} else {
		auto readyToAdd = !_phone->getLastText().isEmpty() && (!_first->getLastText().isEmpty() || !_last->getLastText().isEmpty());
		setTitle(langFactory(readyToAdd ? lng_confirm_contact_data : lng_enter_contact_data));
	}
	updateButtons();

	connect(_first, SIGNAL(submitted(bool)), this, SLOT(onSubmit()));
	connect(_last, SIGNAL(submitted(bool)), this, SLOT(onSubmit()));
	connect(_phone, SIGNAL(submitted(bool)), this, SLOT(onSubmit()));

	setDimensions(st::boxWideWidth, st::contactPadding.top() + _first->height() + st::contactSkip + _last->height() + st::contactPhoneSkip + _phone->height() + st::contactPadding.bottom() + st::boxPadding.bottom());
}

void AddContactBox::setInnerFocus() {
	if ((_first->getLastText().isEmpty() && _last->getLastText().isEmpty()) || !_phone->isEnabled()) {
		(_invertOrder ? _last : _first)->setFocusFast();
		_phone->finishAnimations();
	} else {
		_phone->setFocusFast();
	}
}

void AddContactBox::paintEvent(QPaintEvent *e) {
	BoxContent::paintEvent(e);

	Painter p(this);
	if (_retrying) {
		p.setPen(st::boxTextFg);
		p.setFont(st::boxTextFont);
		auto textHeight = height() - st::contactPadding.top() - st::contactPadding.bottom() - st::boxPadding.bottom();
		p.drawText(QRect(st::boxPadding.left(), st::contactPadding.top(), width() - st::boxPadding.left() - st::boxPadding.right(), textHeight), lng_contact_not_joined(lt_name, _sentName), style::al_topleft);
	} else {
		st::contactUserIcon.paint(p, st::boxPadding.left(), _first->y() + st::contactIconTop, width());
		st::contactPhoneIcon.paint(p, st::boxPadding.left(), _phone->y() + st::contactIconTop, width());
	}
}

void AddContactBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	_first->resize(width() - st::boxPadding.left() - st::contactPadding.left() - st::boxPadding.right(), _first->height());
	_last->resize(_first->width(), _last->height());
	_phone->resize(_first->width(), _last->height());
	if (_invertOrder) {
		_last->moveToLeft(st::boxPadding.left() + st::contactPadding.left(), st::contactPadding.top());
		_first->moveToLeft(st::boxPadding.left() + st::contactPadding.left(), _last->y() + _last->height() + st::contactSkip);
		_phone->moveToLeft(st::boxPadding.left() + st::contactPadding.left(), _first->y() + _first->height() + st::contactPhoneSkip);
	} else {
		_first->moveToLeft(st::boxPadding.left() + st::contactPadding.left(), st::contactPadding.top());
		_last->moveToLeft(st::boxPadding.left() + st::contactPadding.left(), _first->y() + _first->height() + st::contactSkip);
		_phone->moveToLeft(st::boxPadding.left() + st::contactPadding.left(), _last->y() + _last->height() + st::contactPhoneSkip);
	}
}

void AddContactBox::onSubmit() {
	if (_first->hasFocus()) {
		_last->setFocus();
	} else if (_last->hasFocus()) {
		if (_phone->isEnabled()) {
			_phone->setFocus();
		} else {
			onSave();
		}
	} else if (_phone->hasFocus()) {
		onSave();
	}
}

void AddContactBox::onSave() {
	if (_addRequest) return;

	auto firstName = TextUtilities::PrepareForSending(_first->getLastText());
	auto lastName = TextUtilities::PrepareForSending(_last->getLastText());
	auto phone = _phone->getLastText().trimmed();
	if (firstName.isEmpty() && lastName.isEmpty()) {
		if (_invertOrder) {
			_last->setFocus();
			_last->showError();
		} else {
			_first->setFocus();
			_first->showError();
		}
		return;
	} else if (!_user && !App::isValidPhone(phone)) {
		_phone->setFocus();
		_phone->showError();
		return;
	}
	if (firstName.isEmpty()) {
		firstName = lastName;
		lastName = QString();
	}
	_sentName = firstName;
	if (_user) {
		_contactId = rand_value<uint64>();
		QVector<MTPInputContact> v(1, MTP_inputPhoneContact(MTP_long(_contactId), MTP_string(_user->phone()), MTP_string(firstName), MTP_string(lastName)));
		_addRequest = MTP::send(MTPcontacts_ImportContacts(MTP_vector<MTPInputContact>(v)), rpcDone(&AddContactBox::onSaveUserDone), rpcFail(&AddContactBox::onSaveUserFail));
	} else {
		_contactId = rand_value<uint64>();
		QVector<MTPInputContact> v(1, MTP_inputPhoneContact(MTP_long(_contactId), MTP_string(phone), MTP_string(firstName), MTP_string(lastName)));
		_addRequest = MTP::send(MTPcontacts_ImportContacts(MTP_vector<MTPInputContact>(v)), rpcDone(&AddContactBox::onImportDone));
	}
}

bool AddContactBox::onSaveUserFail(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	_addRequest = 0;
	QString err(error.type());
	QString firstName = _first->getLastText().trimmed(), lastName = _last->getLastText().trimmed();
	if (err == "CHAT_TITLE_NOT_MODIFIED") {
		_user->setName(firstName, lastName, _user->nameOrPhone, _user->username);
		closeBox();
		return true;
	} else if (err == "NO_CHAT_TITLE") {
		_first->setFocus();
		_first->showError();
		return true;
	}
	_first->setFocus();
	return true;
}

void AddContactBox::onImportDone(const MTPcontacts_ImportedContacts &res) {
	if (!isBoxShown() || !App::main()) return;

	auto &d = res.c_contacts_importedContacts();
	App::feedUsers(d.vusers);

	auto &v = d.vimported.v;
	UserData *user = nullptr;
	if (!v.isEmpty()) {
		auto &c = v.front().c_importedContact();
		if (c.vclient_id.v != _contactId) return;

		user = App::userLoaded(c.vuser_id.v);
	}
	if (user) {
		Notify::userIsContactChanged(user, true);
		Ui::hideLayer();
	} else {
		hideChildren();
		_retrying = true;
		updateButtons();
		update();
	}
}

void AddContactBox::onSaveUserDone(const MTPcontacts_ImportedContacts &res) {
	auto &d = res.c_contacts_importedContacts();
	App::feedUsers(d.vusers);
	closeBox();
}

void AddContactBox::onRetry() {
	_addRequest = 0;
	_contactId = 0;
	showChildren();
	_retrying = false;
	updateButtons();
	_first->setText(QString());
	_last->setText(QString());
	_phone->clearText();
	_phone->setDisabled(false);
	_first->setFocus();
	update();
}

void AddContactBox::updateButtons() {
	clearButtons();
	if (_retrying) {
		addButton(langFactory(lng_try_other_contact), [this] { onRetry(); });
	} else {
		addButton(langFactory(_user ? lng_settings_save : lng_add_contact), [this] { onSave(); });
		addButton(langFactory(lng_cancel), [this] { closeBox(); });
	}
}

GroupInfoBox::GroupInfoBox(QWidget*, CreatingGroupType creating, bool fromTypeChoose)
: _creating(creating)
, _fromTypeChoose(fromTypeChoose)
, _photo(this, st::newGroupPhotoSize, st::newGroupPhotoIconPosition)
, _title(this, st::defaultInputField, langFactory(_creating == CreatingGroupChannel ? lng_dlg_new_channel_name : lng_dlg_new_group_name)) {
}

void GroupInfoBox::prepare() {
	setMouseTracking(true);

	_title->setMaxLength(kMaxGroupChannelTitle);

	if (_creating == CreatingGroupChannel) {
		_description.create(this, st::newGroupDescription, langFactory(lng_create_group_description));
		_description->show();
		_description->setMaxLength(kMaxChannelDescription);

		connect(_description, SIGNAL(resized()), this, SLOT(onDescriptionResized()));
		connect(_description, SIGNAL(submitted(bool)), this, SLOT(onNext()));
		connect(_description, SIGNAL(cancelled()), this, SLOT(onClose()));
	}

	connect(_title, SIGNAL(submitted(bool)), this, SLOT(onNameSubmit()));

	addButton(langFactory(_creating == CreatingGroupChannel ? lng_create_group_create : lng_create_group_next), [this] { onNext(); });
	addButton(langFactory(_fromTypeChoose ? lng_create_group_back : lng_cancel), [this] { closeBox(); });

	setupPhotoButton();

	updateMaxHeight();
}

void GroupInfoBox::setupPhotoButton() {
	_photo->setClickedCallback(App::LambdaDelayed(st::defaultActiveButton.ripple.hideDuration, this, [this] {
		auto imgExtensions = cImgExtensions();
		auto filter = qsl("Image files (*") + imgExtensions.join(qsl(" *")) + qsl(");;") + FileDialog::AllFilesFilter();
		FileDialog::GetOpenPath(lang(lng_choose_image), filter, base::lambda_guarded(this, [this](const FileDialog::OpenResult &result) {
			if (result.remoteContent.isEmpty() && result.paths.isEmpty()) {
				return;
			}

			QImage img;
			if (!result.remoteContent.isEmpty()) {
				img = App::readImage(result.remoteContent);
			} else {
				img = App::readImage(result.paths.front());
			}
			if (img.isNull() || img.width() > 10 * img.height() || img.height() > 10 * img.width()) {
				return;
			}
			auto box = Ui::show(Box<PhotoCropBox>(img, (_creating == CreatingGroupChannel) ? peerFromChannel(0) : peerFromChat(0)), KeepOtherLayers);
			connect(box, SIGNAL(ready(const QImage&)), this, SLOT(onPhotoReady(const QImage&)));
		}));
	}));
}

void GroupInfoBox::setInnerFocus() {
	_title->setFocusFast();
}

void GroupInfoBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	_photo->moveToLeft(st::boxPadding.left() + st::newGroupInfoPadding.left(), st::boxPadding.top() + st::newGroupInfoPadding.top());

	auto nameLeft = st::newGroupPhotoSize + st::newGroupNamePosition.x();
	_title->resize(width() - st::boxPadding.left() - st::newGroupInfoPadding.left() - st::boxPadding.right() - nameLeft, _title->height());
	_title->moveToLeft(st::boxPadding.left() + st::newGroupInfoPadding.left() + nameLeft, st::boxPadding.top() + st::newGroupInfoPadding.top() + st::newGroupNamePosition.y());
	if (_description) {
		_description->resize(width() - st::boxPadding.left() - st::newGroupInfoPadding.left() - st::boxPadding.right(), _description->height());
		_description->moveToLeft(st::boxPadding.left() + st::newGroupInfoPadding.left(), st::boxPadding.top() + st::newGroupInfoPadding.top() + st::newGroupPhotoSize + st::newGroupDescriptionPadding.top());
	}
}

void GroupInfoBox::onNameSubmit() {
	if (_title->getLastText().trimmed().isEmpty()) {
		_title->setFocus();
		_title->showError();
	} else if (_description) {
		_description->setFocus();
	} else {
		onNext();
	}
}

void GroupInfoBox::createGroup(not_null<PeerListBox*> selectUsersBox, const QString &title, const std::vector<not_null<PeerData*>> &users) {
	if (_creationRequestId) return;

	auto inputs = QVector<MTPInputUser>();
	inputs.reserve(users.size());
	for (auto peer : users) {
		auto user = peer->asUser();
		Assert(user != nullptr);
		if (!user->isSelf()) {
			inputs.push_back(user->inputUser);
		}
	}
	_creationRequestId = request(MTPmessages_CreateChat(MTP_vector<MTPInputUser>(inputs), MTP_string(title))).done([this](const MTPUpdates &result) {
		Ui::hideLayer();

		App::main()->sentUpdatesReceived(result);

		auto success = base::make_optional(&result)
			| [](auto updates) -> base::optional<const QVector<MTPChat>*> {
				switch (updates->type()) {
				case mtpc_updates:
					return &updates->c_updates().vchats.v;
				case mtpc_updatesCombined:
					return &updates->c_updatesCombined().vchats.v;
				}
				LOG(("API Error: unexpected update cons %1 (GroupInfoBox::creationDone)").arg(updates->type()));
				return base::none;
			}
			| [](auto chats) {
				return (!chats->empty() && chats->front().type() == mtpc_chat)
					? base::make_optional(chats)
					: base::none;
			}
			| [](auto chats) {
				return App::chat(chats->front().c_chat().vid.v);
			}
			| [this](not_null<ChatData*> chat) {
				if (!_photoImage.isNull()) {
					Messenger::Instance().uploadProfilePhoto(_photoImage, chat->id);
				}
				Ui::showPeerHistory(chat, ShowAtUnreadMsgId);
			};
		if (!success) {
			LOG(("API Error: chat not found in updates (ContactsBox::creationDone)"));
		}
	}).fail([this, selectUsersBox](const RPCError &error) {
		_creationRequestId = 0;
		if (error.type() == qstr("NO_CHAT_TITLE")) {
			auto guard = weak(this);
			selectUsersBox->closeBox();
			if (guard) {
				_title->showError();
			}
		} else if (error.type() == qstr("USERS_TOO_FEW")) {
		} else if (error.type() == qstr("PEER_FLOOD")) {
			Ui::show(Box<InformBox>(PeerFloodErrorText(PeerFloodType::InviteGroup)), KeepOtherLayers);
		} else if (error.type() == qstr("USER_RESTRICTED")) {
			Ui::show(Box<InformBox>(lang(lng_cant_do_this)), KeepOtherLayers);
		}
	}).send();
}

void GroupInfoBox::onNext() {
	if (_creationRequestId) return;

	auto title = TextUtilities::PrepareForSending(_title->getLastText());
	auto description = _description ? TextUtilities::PrepareForSending(_description->getLastText(), TextUtilities::PrepareTextOption::CheckLinks) : QString();
	if (title.isEmpty()) {
		_title->setFocus();
		_title->showError();
		return;
	}
	if (_creating != CreatingGroupGroup) {
		createChannel(title, description);
	} else {
		auto initBox = [title, weak = weak(this)](not_null<PeerListBox*> box) {
			box->addButton(langFactory(lng_create_group_create), [box, title, weak] {
				if (weak) {
					auto rows = box->peerListCollectSelectedRows();
					if (!rows.empty()) {
						weak->createGroup(box, title, rows);
					}
				}
			});
			box->addButton(langFactory(lng_cancel), [box] { box->closeBox(); });
		};
		Ui::show(Box<PeerListBox>(std::make_unique<AddParticipantsBoxController>(nullptr), std::move(initBox)), KeepOtherLayers);
	}
}

void GroupInfoBox::createChannel(const QString &title, const QString &description) {
	bool mega = false;
	auto flags = mega ? MTPchannels_CreateChannel::Flag::f_megagroup : MTPchannels_CreateChannel::Flag::f_broadcast;
	_creationRequestId = request(MTPchannels_CreateChannel(MTP_flags(flags), MTP_string(title), MTP_string(description))).done([this](const MTPUpdates &result) {
		App::main()->sentUpdatesReceived(result);

		auto success = base::make_optional(&result)
			| [](auto updates) -> base::optional<const QVector<MTPChat>*> {
				switch (updates->type()) {
				case mtpc_updates:
					return &updates->c_updates().vchats.v;
				case mtpc_updatesCombined:
					return &updates->c_updatesCombined().vchats.v;
				}
				LOG(("API Error: unexpected update cons %1 (GroupInfoBox::createChannel)").arg(updates->type()));
				return base::none;
			}
			| [](auto chats) {
				return (!chats->empty() && chats->front().type() == mtpc_channel)
					? base::make_optional(chats)
					: base::none;
			}
			| [](auto chats) {
				return App::channel(chats->front().c_channel().vid.v);
			}
			| [this](not_null<ChannelData*> channel) {
				if (!_photoImage.isNull()) {
					Messenger::Instance().uploadProfilePhoto(
						_photoImage,
						channel->id);
				}
				_createdChannel = channel;
				_creationRequestId = request(
					MTPchannels_ExportInvite(_createdChannel->inputChannel)
				).done([this](const MTPExportedChatInvite &result) {
					_creationRequestId = 0;
					if (result.type() == mtpc_chatInviteExported) {
						auto link = qs(result.c_chatInviteExported().vlink);
						_createdChannel->setInviteLink(link);
					}
					Ui::show(Box<SetupChannelBox>(_createdChannel));
				}).send();
			};
		if (!success) {
			LOG(("API Error: channel not found in updates (GroupInfoBox::creationDone)"));
			closeBox();
		}
	}).fail([this](const RPCError &error) {
		_creationRequestId = 0;
		if (error.type() == "NO_CHAT_TITLE") {
			_title->setFocus();
			_title->showError();
		} else if (error.type() == qstr("USER_RESTRICTED")) {
			Ui::show(Box<InformBox>(lang(lng_cant_do_this)));
		} else if (error.type() == qstr("CHANNELS_TOO_MUCH")) {
			Ui::show(Box<InformBox>(lang(lng_cant_do_this))); // TODO
		}
	}).send();
}

void GroupInfoBox::onDescriptionResized() {
	updateMaxHeight();
	update();
}

void GroupInfoBox::updateMaxHeight() {
	auto newHeight = st::boxPadding.top() + st::newGroupInfoPadding.top() + st::newGroupPhotoSize + st::boxPadding.bottom() + st::newGroupInfoPadding.bottom();
	if (_description) {
		newHeight += st::newGroupDescriptionPadding.top() + _description->height() + st::newGroupDescriptionPadding.bottom();
	}
	setDimensions(st::boxWideWidth, newHeight);
}

void GroupInfoBox::onPhotoReady(const QImage &img) {
	_photoImage = img;
	_photo->setImage(_photoImage);
}

SetupChannelBox::SetupChannelBox(QWidget*, ChannelData *channel, bool existing)
: _channel(channel)
, _existing(existing)
, _privacyGroup(std::make_shared<Ui::RadioenumGroup<Privacy>>(Privacy::Public))
, _public(this, _privacyGroup, Privacy::Public, lang(channel->isMegagroup() ? lng_create_public_group_title : lng_create_public_channel_title), st::defaultBoxCheckbox)
, _private(this, _privacyGroup, Privacy::Private, lang(channel->isMegagroup() ? lng_create_private_group_title : lng_create_private_channel_title), st::defaultBoxCheckbox)
, _aboutPublicWidth(st::boxWideWidth - st::boxPadding.left() - st::boxButtonPadding.right() - st::newGroupPadding.left() - st::defaultRadio.diameter - st::defaultBoxCheckbox.textPosition.x())
, _aboutPublic(st::defaultTextStyle, lang(channel->isMegagroup() ? lng_create_public_group_about : lng_create_public_channel_about), _defaultOptions, _aboutPublicWidth)
, _aboutPrivate(st::defaultTextStyle, lang(channel->isMegagroup() ? lng_create_private_group_about : lng_create_private_channel_about), _defaultOptions, _aboutPublicWidth)
, _link(this, st::setupChannelLink, base::lambda<QString()>(), channel->username, true) {
}

void SetupChannelBox::prepare() {
	_aboutPublicHeight = _aboutPublic.countHeight(_aboutPublicWidth);

	setMouseTracking(true);

	_checkRequestId = MTP::send(MTPchannels_CheckUsername(_channel->inputChannel, MTP_string("preston")), RPCDoneHandlerPtr(), rpcFail(&SetupChannelBox::onFirstCheckFail));

	addButton(langFactory(lng_settings_save), [this] { onSave(); });
	addButton(langFactory(_existing ? lng_cancel : lng_create_group_skip), [this] { closeBox(); });

	connect(_link, SIGNAL(changed()), this, SLOT(onChange()));
	_link->setVisible(_privacyGroup->value() == Privacy::Public);

	_checkTimer.setSingleShot(true);
	connect(&_checkTimer, SIGNAL(timeout()), this, SLOT(onCheck()));

	_privacyGroup->setChangedCallback([this](Privacy value) { privacyChanged(value); });
	subscribe(Notify::PeerUpdated(), Notify::PeerUpdatedHandler(Notify::PeerUpdate::Flag::InviteLinkChanged, [this](const Notify::PeerUpdate &update) {
		if (update.peer == _channel) {
			rtlupdate(_invitationLink);
		}
	}));
	subscribe(boxClosing, [this] {
		if (!_existing) {
			AddParticipantsBoxController::Start(_channel);
		}
	});

	updateMaxHeight();
}

void SetupChannelBox::setInnerFocus() {
	if (_link->isHidden()) {
		setFocus();
	} else {
		_link->setFocusFast();
	}
}

void SetupChannelBox::updateMaxHeight() {
	auto newHeight = st::boxPadding.top() + st::newGroupPadding.top() + _public->heightNoMargins() + _aboutPublicHeight + st::newGroupSkip + _private->heightNoMargins() + _aboutPrivate.countHeight(_aboutPublicWidth) + st::newGroupSkip + st::newGroupPadding.bottom();
	if (!_channel->isMegagroup() || _privacyGroup->value() == Privacy::Public) {
		newHeight += st::newGroupLinkPadding.top() + _link->height() + st::newGroupLinkPadding.bottom();
	}
	setDimensions(st::boxWideWidth, newHeight);
}

void SetupChannelBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		if (_link->hasFocus()) {
			if (_link->text().trimmed().isEmpty()) {
				_link->setFocus();
				_link->showError();
			} else {
				onSave();
			}
		}
	} else {
		BoxContent::keyPressEvent(e);
	}
}

void SetupChannelBox::paintEvent(QPaintEvent *e) {
	Painter p(this);

	p.fillRect(e->rect(), st::boxBg);
	p.setPen(st::newGroupAboutFg);

	QRect aboutPublic(st::boxPadding.left() + st::newGroupPadding.left() + st::defaultRadio.diameter + st::defaultBoxCheckbox.textPosition.x(), _public->bottomNoMargins(), _aboutPublicWidth, _aboutPublicHeight);
	_aboutPublic.drawLeft(p, aboutPublic.x(), aboutPublic.y(), aboutPublic.width(), width());

	QRect aboutPrivate(st::boxPadding.left() + st::newGroupPadding.left() + st::defaultRadio.diameter + st::defaultBoxCheckbox.textPosition.x(), _private->bottomNoMargins(), _aboutPublicWidth, _aboutPublicHeight);
	_aboutPrivate.drawLeft(p, aboutPrivate.x(), aboutPrivate.y(), aboutPrivate.width(), width());

	if (!_channel->isMegagroup() || !_link->isHidden()) {
		p.setPen(st::boxTextFg);
		p.setFont(st::newGroupLinkFont);
		p.drawTextLeft(st::boxPadding.left() + st::newGroupPadding.left() + st::defaultInputField.textMargins.left(), _link->y() - st::newGroupLinkPadding.top() + st::newGroupLinkTop, width(), lang(_link->isHidden() ? lng_create_group_invite_link : lng_create_group_link));
	}

	if (_link->isHidden()) {
		if (!_channel->isMegagroup()) {
			QTextOption option(style::al_left);
			option.setWrapMode(QTextOption::WrapAnywhere);
			p.setFont(_linkOver ? st::boxTextFont->underline() : st::boxTextFont);
			p.setPen(st::defaultLinkButton.color);
			auto inviteLinkText = _channel->inviteLink().isEmpty() ? lang(lng_group_invite_create) : _channel->inviteLink();
			p.drawText(_invitationLink, inviteLinkText, option);
		}
	} else {
		if (!_errorText.isEmpty()) {
			p.setPen(st::boxTextFgError);
			p.setFont(st::boxTextFont);
			p.drawTextRight(st::boxPadding.right(), _link->y() - st::newGroupLinkPadding.top() + st::newGroupLinkTop + st::newGroupLinkFont->ascent - st::boxTextFont->ascent, width(), _errorText);
		} else if (!_goodText.isEmpty()) {
			p.setPen(st::boxTextFgGood);
			p.setFont(st::boxTextFont);
			p.drawTextRight(st::boxPadding.right(), _link->y() - st::newGroupLinkPadding.top() + st::newGroupLinkTop + st::newGroupLinkFont->ascent - st::boxTextFont->ascent, width(), _goodText);
		}
	}
}

void SetupChannelBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	_public->moveToLeft(st::boxPadding.left() + st::newGroupPadding.left(), st::boxPadding.top() + st::newGroupPadding.top());
	_private->moveToLeft(st::boxPadding.left() + st::newGroupPadding.left(), _public->bottomNoMargins() + _aboutPublicHeight + st::newGroupSkip);

	_link->resize(width() - st::boxPadding.left() - st::newGroupLinkPadding.left() - st::boxPadding.right(), _link->height());
	_link->moveToLeft(st::boxPadding.left() + st::newGroupLinkPadding.left(), _private->bottomNoMargins() + _aboutPrivate.countHeight(_aboutPublicWidth) + st::newGroupSkip + st::newGroupPadding.bottom() + st::newGroupLinkPadding.top());
	_invitationLink = QRect(_link->x(), _link->y() + (_link->height() / 2) - st::boxTextFont->height, _link->width(), 2 * st::boxTextFont->height);
}

void SetupChannelBox::mouseMoveEvent(QMouseEvent *e) {
	updateSelected(e->globalPos());
}

void SetupChannelBox::mousePressEvent(QMouseEvent *e) {
	if (_linkOver) {
		if (_channel->inviteLink().isEmpty()) {
			Auth().api().exportInviteLink(_channel);
		} else {
			QGuiApplication::clipboard()->setText(_channel->inviteLink());
			Ui::Toast::Show(lang(lng_create_channel_link_copied));
		}
	}
}

void SetupChannelBox::leaveEventHook(QEvent *e) {
	updateSelected(QCursor::pos());
}

void SetupChannelBox::updateSelected(const QPoint &cursorGlobalPosition) {
	QPoint p(mapFromGlobal(cursorGlobalPosition));

	bool linkOver = _invitationLink.contains(p);
	if (linkOver != _linkOver) {
		_linkOver = linkOver;
		update();
		setCursor(_linkOver ? style::cur_pointer : style::cur_default);
	}
}

void SetupChannelBox::onSave() {
	if (_privacyGroup->value() == Privacy::Private) {
		if (_existing) {
			_sentUsername = QString();
			_saveRequestId = MTP::send(MTPchannels_UpdateUsername(_channel->inputChannel, MTP_string(_sentUsername)), rpcDone(&SetupChannelBox::onUpdateDone), rpcFail(&SetupChannelBox::onUpdateFail));
		} else {
			closeBox();
		}
	}

	if (_saveRequestId) return;

	QString link = _link->text().trimmed();
	if (link.isEmpty()) {
		_link->setFocus();
		_link->showError();
		return;
	}

	_sentUsername = link;
	_saveRequestId = MTP::send(MTPchannels_UpdateUsername(_channel->inputChannel, MTP_string(_sentUsername)), rpcDone(&SetupChannelBox::onUpdateDone), rpcFail(&SetupChannelBox::onUpdateFail));
}

void SetupChannelBox::onChange() {
	QString name = _link->text().trimmed();
	if (name.isEmpty()) {
		if (!_errorText.isEmpty() || !_goodText.isEmpty()) {
			_errorText = _goodText = QString();
			update();
		}
		_checkTimer.stop();
	} else {
		int32 len = name.size();
		for (int32 i = 0; i < len; ++i) {
			QChar ch = name.at(i);
			if ((ch < 'A' || ch > 'Z') && (ch < 'a' || ch > 'z') && (ch < '0' || ch > '9') && ch != '_') {
				if (_errorText != lang(lng_create_channel_link_bad_symbols)) {
					_errorText = lang(lng_create_channel_link_bad_symbols);
					update();
				}
				_checkTimer.stop();
				return;
			}
		}
		if (name.size() < MinUsernameLength) {
			if (_errorText != lang(lng_create_channel_link_too_short)) {
				_errorText = lang(lng_create_channel_link_too_short);
				update();
			}
			_checkTimer.stop();
		} else {
			if (!_errorText.isEmpty() || !_goodText.isEmpty()) {
				_errorText = _goodText = QString();
				update();
			}
			_checkTimer.start(UsernameCheckTimeout);
		}
	}
}

void SetupChannelBox::onCheck() {
	if (_checkRequestId) {
		MTP::cancel(_checkRequestId);
	}
	QString link = _link->text().trimmed();
	if (link.size() >= MinUsernameLength) {
		_checkUsername = link;
		_checkRequestId = MTP::send(MTPchannels_CheckUsername(_channel->inputChannel, MTP_string(link)), rpcDone(&SetupChannelBox::onCheckDone), rpcFail(&SetupChannelBox::onCheckFail));
	}
}

void SetupChannelBox::privacyChanged(Privacy value) {
	if (value == Privacy::Public) {
		if (_tooMuchUsernames) {
			_privacyGroup->setValue(Privacy::Private);
			Ui::show(Box<RevokePublicLinkBox>(base::lambda_guarded(this, [this] {
				_tooMuchUsernames = false;
				_privacyGroup->setValue(Privacy::Public);
				onCheck();
			})), KeepOtherLayers);
			return;
		}
		_link->show();
		_link->setDisplayFocused(true);
		_link->setFocus();
	} else {
		_link->hide();
		setFocus();
	}
	if (_channel->isMegagroup()) {
		updateMaxHeight();
	}
	update();
}

void SetupChannelBox::onUpdateDone(const MTPBool &result) {
	_channel->setName(TextUtilities::SingleLine(_channel->name), _sentUsername);
	closeBox();
}

bool SetupChannelBox::onUpdateFail(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	_saveRequestId = 0;
	QString err(error.type());
	if (err == "USERNAME_NOT_MODIFIED" || _sentUsername == _channel->username) {
		_channel->setName(TextUtilities::SingleLine(_channel->name), TextUtilities::SingleLine(_sentUsername));
		closeBox();
		return true;
	} else if (err == "USERNAME_INVALID") {
		_link->setFocus();
		_link->showError();
		_errorText = lang(lng_create_channel_link_invalid);
		update();
		return true;
	} else if (err == "USERNAME_OCCUPIED" || err == "USERNAMES_UNAVAILABLE") {
		_link->setFocus();
		_link->showError();
		_errorText = lang(lng_create_channel_link_occupied);
		update();
		return true;
	}
	_link->setFocus();
	return true;
}

void SetupChannelBox::onCheckDone(const MTPBool &result) {
	_checkRequestId = 0;
	QString newError = (mtpIsTrue(result) || _checkUsername == _channel->username) ? QString() : lang(lng_create_channel_link_occupied);
	QString newGood = newError.isEmpty() ? lang(lng_create_channel_link_available) : QString();
	if (_errorText != newError || _goodText != newGood) {
		_errorText = newError;
		_goodText = newGood;
		update();
	}
}

bool SetupChannelBox::onCheckFail(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	_checkRequestId = 0;
	QString err(error.type());
	if (err == qstr("CHANNEL_PUBLIC_GROUP_NA")) {
		Ui::hideLayer();
		return true;
	} else if (err == qstr("CHANNELS_ADMIN_PUBLIC_TOO_MUCH")) {
		if (_existing) {
			showRevokePublicLinkBoxForEdit();
		} else {
			_tooMuchUsernames = true;
			_privacyGroup->setValue(Privacy::Private);
		}
		return true;
	} else if (err == qstr("USERNAME_INVALID")) {
		_errorText = lang(lng_create_channel_link_invalid);
		update();
		return true;
	} else if (err == qstr("USERNAME_OCCUPIED") && _checkUsername != _channel->username) {
		_errorText = lang(lng_create_channel_link_occupied);
		update();
		return true;
	}
	_goodText = QString();
	_link->setFocus();
	return true;
}

void SetupChannelBox::showRevokePublicLinkBoxForEdit() {
	closeBox();
	Ui::show(Box<RevokePublicLinkBox>([channel = _channel, existing = _existing]() {
		Ui::show(Box<SetupChannelBox>(channel, existing), KeepOtherLayers);
	}), KeepOtherLayers);
}

bool SetupChannelBox::onFirstCheckFail(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	_checkRequestId = 0;
	QString err(error.type());
	if (err == qstr("CHANNEL_PUBLIC_GROUP_NA")) {
		Ui::hideLayer();
		return true;
	} else if (err == qstr("CHANNELS_ADMIN_PUBLIC_TOO_MUCH")) {
		if (_existing) {
			showRevokePublicLinkBoxForEdit();
		} else {
			_tooMuchUsernames = true;
			_privacyGroup->setValue(Privacy::Private);
		}
		return true;
	}
	_goodText = QString();
	_link->setFocus();
	return true;
}

EditNameTitleBox::EditNameTitleBox(QWidget*, not_null<PeerData*> peer)
: _peer(peer)
, _first(this, st::defaultInputField, langFactory(_peer->isUser() ? lng_signup_firstname : lng_dlg_new_group_name), _peer->isUser() ? _peer->asUser()->firstName : _peer->name)
, _last(this, st::defaultInputField, langFactory(lng_signup_lastname), peer->isUser() ? peer->asUser()->lastName : QString())
, _invertOrder(!peer->isChat() && langFirstNameGoesSecond()) {
}

void EditNameTitleBox::prepare() {
	auto newHeight = st::contactPadding.top() + _first->height();
	if (_peer->isUser()) {
		setTitle(langFactory(_peer->isSelf() ? lng_edit_self_title : lng_edit_contact_title));
		newHeight += st::contactSkip + _last->height();
	} else if (_peer->isChat()) {
		setTitle(langFactory(lng_edit_group_title));
	}
	newHeight += st::boxPadding.bottom() + st::contactPadding.bottom();
	setDimensions(st::boxWideWidth, newHeight);

	addButton(langFactory(lng_settings_save), [this] { onSave(); });
	addButton(langFactory(lng_cancel), [this] { closeBox(); });
	if (_invertOrder) {
		setTabOrder(_last, _first);
	}
	_first->setMaxLength(kMaxGroupChannelTitle);
	_last->setMaxLength(kMaxGroupChannelTitle);

	connect(_first, SIGNAL(submitted(bool)), this, SLOT(onSubmit()));
	connect(_last, SIGNAL(submitted(bool)), this, SLOT(onSubmit()));
	_last->setVisible(!_peer->isChat());
}

void EditNameTitleBox::setInnerFocus() {
	(_invertOrder ? _last : _first)->setFocusFast();
}

void EditNameTitleBox::onSubmit() {
	if (_first->hasFocus()) {
		if (_peer->isChat()) {
			if (_first->getLastText().trimmed().isEmpty()) {
				_first->setFocus();
				_first->showError();
			} else {
				onSave();
			}
		} else {
			_last->setFocus();
		}
	} else if (_last->hasFocus()) {
		if (_first->getLastText().trimmed().isEmpty()) {
			_first->setFocus();
			_first->showError();
		} else if (_last->getLastText().trimmed().isEmpty()) {
			_last->setFocus();
			_last->showError();
		} else {
			onSave();
		}
	}
}

void EditNameTitleBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	_first->resize(width() - st::boxPadding.left() - st::newGroupInfoPadding.left() - st::boxPadding.right(), _first->height());
	_last->resize(_first->size());
	if (_invertOrder) {
		_last->moveToLeft(st::boxPadding.left() + st::newGroupInfoPadding.left(), st::contactPadding.top());
		_first->moveToLeft(st::boxPadding.left() + st::newGroupInfoPadding.left(), _last->y() + _last->height() + st::contactSkip);
	} else {
		_first->moveToLeft(st::boxPadding.left() + st::newGroupInfoPadding.left(), st::contactPadding.top());
		_last->moveToLeft(st::boxPadding.left() + st::newGroupInfoPadding.left(), _first->y() + _first->height() + st::contactSkip);
	}
}

void EditNameTitleBox::onSave() {
	if (_requestId) return;

	auto first = TextUtilities::PrepareForSending(_first->getLastText());
	auto last = TextUtilities::PrepareForSending(_last->getLastText());
	if (first.isEmpty() && last.isEmpty()) {
		if (_invertOrder) {
			_last->setFocus();
			_last->showError();
		} else {
			_first->setFocus();
			_first->showError();
		}
		return;
	}
	if (first.isEmpty()) {
		first = last;
		last = QString();
	}
	_sentName = first;
	if (_peer == App::self()) {
		auto flags = MTPaccount_UpdateProfile::Flag::f_first_name | MTPaccount_UpdateProfile::Flag::f_last_name;
		_requestId = MTP::send(MTPaccount_UpdateProfile(MTP_flags(flags), MTP_string(first), MTP_string(last), MTPstring()), rpcDone(&EditNameTitleBox::onSaveSelfDone), rpcFail(&EditNameTitleBox::onSaveSelfFail));
	} else if (_peer->isChat()) {
		_requestId = MTP::send(MTPmessages_EditChatTitle(_peer->asChat()->inputChat, MTP_string(first)), rpcDone(&EditNameTitleBox::onSaveChatDone), rpcFail(&EditNameTitleBox::onSaveChatFail));
	}
}

void EditNameTitleBox::onSaveSelfDone(const MTPUser &user) {
	App::feedUsers(MTP_vector<MTPUser>(1, user));
	closeBox();
}

bool EditNameTitleBox::onSaveSelfFail(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	auto err = error.type();
	auto first = TextUtilities::SingleLine(_first->getLastText().trimmed());
	auto last = TextUtilities::SingleLine(_last->getLastText().trimmed());
	if (err == "NAME_NOT_MODIFIED") {
		App::self()->setName(first, last, QString(), TextUtilities::SingleLine(App::self()->username));
		closeBox();
		return true;
	} else if (err == "FIRSTNAME_INVALID") {
		_first->setFocus();
		_first->showError();
		return true;
	} else if (err == "LASTNAME_INVALID") {
		_last->setFocus();
		_last->showError();
		return true;
	}
	_first->setFocus();
	return true;
}

bool EditNameTitleBox::onSaveChatFail(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	_requestId = 0;
	QString err(error.type());
	if (err == qstr("CHAT_TITLE_NOT_MODIFIED") || err == qstr("CHAT_NOT_MODIFIED")) {
		if (auto chatData = _peer->asChat()) {
			chatData->setName(_sentName);
		}
		closeBox();
		return true;
	} else if (err == qstr("NO_CHAT_TITLE")) {
		_first->setFocus();
		_first->showError();
		return true;
	}
	_first->setFocus();
	return true;
}

void EditNameTitleBox::onSaveChatDone(const MTPUpdates &updates) {
	App::main()->sentUpdatesReceived(updates);
	closeBox();
}

EditBioBox::EditBioBox(QWidget*, not_null<UserData*> self) : BoxContent()
, _dynamicFieldStyle(CreateBioFieldStyle())
, _self(self)
, _bio(this, _dynamicFieldStyle, langFactory(lng_bio_placeholder), _self->about())
, _countdown(this, QString(), Ui::FlatLabel::InitType::Simple, st::editBioCountdownLabel)
, _about(this, lang(lng_bio_about), Ui::FlatLabel::InitType::Simple, st::aboutRevokePublicLabel) {
}

void EditBioBox::prepare() {
	setTitle(langFactory(lng_bio_title));

	addButton(langFactory(lng_settings_save), [this] { save(); });
	addButton(langFactory(lng_cancel), [this] { closeBox(); });
	_bio->setMaxLength(kMaxBioLength);
	_bio->setCtrlEnterSubmit(Ui::CtrlEnterSubmit::Both);
	auto cursor = _bio->textCursor();
	cursor.setPosition(_bio->getLastText().size());
	_bio->setTextCursor(cursor);
	connect(_bio, &Ui::InputArea::submitted, this, [this](bool ctrlShiftEnter) { save(); });
	connect(_bio, &Ui::InputArea::resized, this, [this] { updateMaxHeight(); });
	connect(_bio, &Ui::InputArea::changed, this, [this] { handleBioUpdated(); });
	handleBioUpdated();
	updateMaxHeight();
}

void EditBioBox::updateMaxHeight() {
	auto newHeight = st::contactPadding.top() + _bio->height() + st::boxLittleSkip + _about->height() + st::boxPadding.bottom() + st::contactPadding.bottom();
	setDimensions(st::boxWideWidth, newHeight);
}

void EditBioBox::handleBioUpdated() {
	auto text = _bio->getLastText();
	if (text.indexOf('\n') >= 0) {
		auto position = _bio->textCursor().position();
		_bio->setText(text.replace('\n', ' '));
		auto cursor = _bio->textCursor();
		cursor.setPosition(position);
		_bio->setTextCursor(cursor);
	}
	auto countLeft = qMax(kMaxBioLength - text.size(), 0);
	_countdown->setText(QString::number(countLeft));
}

void EditBioBox::setInnerFocus() {
	_bio->setFocusFast();
}

void EditBioBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	_bio->resize(width() - st::boxPadding.left() - st::newGroupInfoPadding.left() - st::boxPadding.right(), _bio->height());
	_bio->moveToLeft(st::boxPadding.left() + st::newGroupInfoPadding.left(), st::contactPadding.top());
	_countdown->moveToRight(st::boxPadding.right(), _bio->y() + _dynamicFieldStyle.textMargins.top());
	_about->moveToLeft(st::boxPadding.left(), _bio->y() + _bio->height() + st::boxLittleSkip);
}

void EditBioBox::save() {
	if (_requestId) return;

	auto text = TextUtilities::PrepareForSending(_bio->getLastText());
	_sentBio = text;

	auto flags = MTPaccount_UpdateProfile::Flag::f_about;
	_requestId = request(MTPaccount_UpdateProfile(MTP_flags(flags), MTPstring(), MTPstring(), MTP_string(text))).done([this](const MTPUser &result) {
		App::feedUsers(MTP_vector<MTPUser>(1, result));
		_self->setAbout(_sentBio);
		closeBox();
	}).send();
}

EditChannelBox::EditChannelBox(QWidget*, not_null<ChannelData*> channel)
: _channel(channel)
, _title(this, st::defaultInputField, langFactory(_channel->isMegagroup() ? lng_dlg_new_group_name : lng_dlg_new_channel_name), _channel->name)
, _description(this, st::newGroupDescription, langFactory(lng_create_group_description), _channel->about())
, _sign(this, lang(lng_edit_sign_messages), channel->addsSignature(), st::defaultBoxCheckbox)
, _inviteGroup(std::make_shared<Ui::RadioenumGroup<Invites>>(channel->anyoneCanAddMembers() ? Invites::Everybody : Invites::OnlyAdmins))
, _inviteEverybody(this, _inviteGroup, Invites::Everybody, lang(lng_edit_group_invites_everybody))
, _inviteOnlyAdmins(this, _inviteGroup, Invites::OnlyAdmins, lang(lng_edit_group_invites_only_admins))
, _publicLink(this, lang(channel->isPublic() ? lng_profile_edit_public_link : lng_profile_create_public_link), st::boxLinkButton) {
}

void EditChannelBox::prepare() {
	setTitle(langFactory(_channel->isMegagroup() ? lng_edit_group : lng_edit_channel_title));

	addButton(langFactory(lng_settings_save), [this] { onSave(); });
	addButton(langFactory(lng_cancel), [this] { closeBox(); });

	subscribe(Notify::PeerUpdated(), Notify::PeerUpdatedHandler(Notify::PeerUpdate::Flag::NameChanged, [this](const Notify::PeerUpdate &update) {
		if (update.peer == _channel) {
			handleChannelNameChange();
		}
	}));

	setMouseTracking(true);

	_title->setMaxLength(kMaxGroupChannelTitle);
	_description->setMaxLength(kMaxChannelDescription);

	connect(_description, SIGNAL(resized()), this, SLOT(onDescriptionResized()));
	connect(_description, SIGNAL(submitted(bool)), this, SLOT(onSave()));
	connect(_description, SIGNAL(cancelled()), this, SLOT(onClose()));

	connect(_publicLink, SIGNAL(clicked()), this, SLOT(onPublicLink()));
	_publicLink->setVisible(_channel->canEditUsername());
	_sign->setVisible(canEditSignatures());
	_inviteEverybody->setVisible(canEditInvites());
	_inviteOnlyAdmins->setVisible(canEditInvites());

	updateMaxHeight();
}

void EditChannelBox::setInnerFocus() {
	_title->setFocusFast();
}

void EditChannelBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		if (_title->hasFocus()) {
			onSave();
		}
	} else {
		BoxContent::keyPressEvent(e);
	}
}

void EditChannelBox::handleChannelNameChange() {
	_publicLink->setText(lang(_channel->isPublic() ? lng_profile_edit_public_link : lng_profile_create_public_link));
	_sign->setChecked(_channel->addsSignature());
}

void EditChannelBox::onDescriptionResized() {
	updateMaxHeight();
	update();
}

bool EditChannelBox::canEditSignatures() const {
	return _channel->canEditInformation() && !_channel->isMegagroup();
}

bool EditChannelBox::canEditInvites() const {
	return _channel->canEditInformation() && _channel->isMegagroup();
}

void EditChannelBox::updateMaxHeight() {
	auto newHeight = st::newGroupInfoPadding.top() + _title->height();
	newHeight += st::newGroupDescriptionPadding.top() + _description->height() + st::newGroupDescriptionPadding.bottom();
	if (canEditSignatures()) {
		newHeight += st::newGroupPublicLinkPadding.top() + _sign->heightNoMargins() + st::newGroupPublicLinkPadding.bottom();
	}
	if (canEditInvites()) {
		newHeight += st::boxTitleHeight + _inviteEverybody->heightNoMargins();
		newHeight += st::boxLittleSkip + _inviteOnlyAdmins->heightNoMargins();
	}
	if (_channel->canEditUsername()) {
		newHeight += st::newGroupPublicLinkPadding.top() + _publicLink->height() + st::newGroupPublicLinkPadding.bottom();
	}
	newHeight += st::boxPadding.bottom() + st::newGroupInfoPadding.bottom();
	setDimensions(st::boxWideWidth, newHeight);
}

void EditChannelBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	_title->resize(width() - st::boxPadding.left() - st::newGroupInfoPadding.left() - st::boxPadding.right(), _title->height());
	_title->moveToLeft(st::boxPadding.left() + st::newGroupInfoPadding.left(), st::newGroupInfoPadding.top() + st::newGroupNamePosition.y());

	_description->resize(width() - st::boxPadding.left() - st::newGroupInfoPadding.left() - st::boxPadding.right(), _description->height());
	_description->moveToLeft(st::boxPadding.left() + st::newGroupInfoPadding.left(), _title->y() + _title->height() + st::newGroupDescriptionPadding.top());

	_sign->moveToLeft(st::boxPadding.left() + st::newGroupInfoPadding.left(), _description->y() + _description->height() + st::newGroupDescriptionPadding.bottom() + st::newGroupPublicLinkPadding.top());

	_inviteEverybody->moveToLeft(st::boxPadding.left() + st::newGroupInfoPadding.left(), _description->y() + _description->height() + st::boxTitleHeight);
	_inviteOnlyAdmins->moveToLeft(st::boxPadding.left() + st::newGroupInfoPadding.left(), _inviteEverybody->bottomNoMargins() + st::boxLittleSkip);

	if (canEditSignatures()) {
		_publicLink->moveToLeft(st::boxPadding.left() + st::newGroupInfoPadding.left(), _sign->bottomNoMargins() + st::newGroupDescriptionPadding.bottom() + st::newGroupPublicLinkPadding.top());
	} else if (canEditInvites()) {
		_publicLink->moveToLeft(st::boxPadding.left() + st::newGroupInfoPadding.left(), _inviteOnlyAdmins->bottomNoMargins() + st::newGroupDescriptionPadding.bottom() + st::newGroupPublicLinkPadding.top());
	} else {
		_publicLink->moveToLeft(st::boxPadding.left() + st::newGroupInfoPadding.left(), _description->y() + _description->height() + st::newGroupDescriptionPadding.bottom() + st::newGroupPublicLinkPadding.top());
	}
}

void EditChannelBox::paintEvent(QPaintEvent *e) {
	BoxContent::paintEvent(e);

	if (canEditInvites()) {
		Painter p(this);
		p.setPen(st::boxTitleFg);
		p.setFont(st::autoDownloadTitleFont);
		p.drawTextLeft(st::boxTitlePosition.x(), _description->y() + _description->height() + st::boxTitlePosition.y(), width(), lang(lng_edit_group_who_invites));
	}
}

void EditChannelBox::onSave() {
	if (_saveTitleRequestId || _saveDescriptionRequestId || _saveSignRequestId || _saveInvitesRequestId) return;

	auto title = TextUtilities::PrepareForSending(_title->getLastText());
	auto description = TextUtilities::PrepareForSending(_description->getLastText(), TextUtilities::PrepareTextOption::CheckLinks);
	if (title.isEmpty()) {
		_title->setFocus();
		_title->showError();
		return;
	}
	_sentTitle = title;
	_sentDescription = description;
	if (_sentTitle == _channel->name) {
		saveDescription();
	} else {
		_saveTitleRequestId = MTP::send(MTPchannels_EditTitle(_channel->inputChannel, MTP_string(_sentTitle)), rpcDone(&EditChannelBox::onSaveTitleDone), rpcFail(&EditChannelBox::onSaveFail));
	}
}

void EditChannelBox::onPublicLink() {
	Ui::show(Box<SetupChannelBox>(_channel, true), KeepOtherLayers);
}

void EditChannelBox::saveDescription() {
	if (_sentDescription == _channel->about()) {
		saveSign();
	} else {
		_saveDescriptionRequestId = MTP::send(MTPchannels_EditAbout(_channel->inputChannel, MTP_string(_sentDescription)), rpcDone(&EditChannelBox::onSaveDescriptionDone), rpcFail(&EditChannelBox::onSaveFail));
	}
}

void EditChannelBox::saveSign() {
	if (!canEditSignatures() || _channel->addsSignature() == _sign->checked()) {
		saveInvites();
	} else {
		_saveSignRequestId = MTP::send(MTPchannels_ToggleSignatures(_channel->inputChannel, MTP_bool(_sign->checked())), rpcDone(&EditChannelBox::onSaveSignDone), rpcFail(&EditChannelBox::onSaveFail));
	}
}

void EditChannelBox::saveInvites() {
	if (!canEditInvites() || _channel->anyoneCanAddMembers() == (_inviteGroup->value() == Invites::Everybody)) {
		closeBox();
	} else {
		_saveInvitesRequestId = MTP::send(MTPchannels_ToggleInvites(_channel->inputChannel, MTP_bool(_inviteGroup->value() == Invites::Everybody)), rpcDone(&EditChannelBox::onSaveInvitesDone), rpcFail(&EditChannelBox::onSaveFail));
	}
}

bool EditChannelBox::onSaveFail(const RPCError &error, mtpRequestId req) {
	if (MTP::isDefaultHandledError(error)) return false;

	QString err(error.type());
	if (req == _saveTitleRequestId) {
		_saveTitleRequestId = 0;
		if (err == qstr("CHAT_NOT_MODIFIED") || err == qstr("CHAT_TITLE_NOT_MODIFIED")) {
			_channel->setName(_sentTitle, _channel->username);
			saveDescription();
			return true;
		} else if (err == qstr("NO_CHAT_TITLE")) {
			_title->setFocus();
			_title->showError();
			return true;
		} else {
			_title->setFocus();
		}
	} else if (req == _saveDescriptionRequestId) {
		_saveDescriptionRequestId = 0;
		if (err == qstr("CHAT_ABOUT_NOT_MODIFIED")) {
			if (_channel->setAbout(_sentDescription)) {
				Auth().api().fullPeerUpdated().notify(_channel);
			}
			saveSign();
			return true;
		} else {
			_description->setFocus();
		}
	} else if (req == _saveSignRequestId) {
		_saveSignRequestId = 0;
		if (err == qstr("CHAT_NOT_MODIFIED")) {
			saveInvites();
			return true;
		}
	} else if (req == _saveInvitesRequestId) {
		_saveInvitesRequestId = 0;
		if (err == qstr("CHAT_NOT_MODIFIED")) {
			closeBox();
			return true;
		}
	}
	return true;
}

void EditChannelBox::onSaveTitleDone(const MTPUpdates &result) {
	_saveTitleRequestId = 0;
	Auth().api().applyUpdates(result);
	saveDescription();
}

void EditChannelBox::onSaveDescriptionDone(const MTPBool &result) {
	_saveDescriptionRequestId = 0;
	if (_channel->setAbout(_sentDescription)) {
		Auth().api().fullPeerUpdated().notify(_channel);
	}
	saveSign();
}

void EditChannelBox::onSaveSignDone(const MTPUpdates &result) {
	_saveSignRequestId = 0;
	Auth().api().applyUpdates(result);
	saveInvites();
}

void EditChannelBox::onSaveInvitesDone(const MTPUpdates &result) {
	_saveSignRequestId = 0;
	Auth().api().applyUpdates(result);
	closeBox();
}

RevokePublicLinkBox::Inner::Inner(QWidget *parent, base::lambda<void()> revokeCallback) : TWidget(parent)
, _rowHeight(st::contactsPadding.top() + st::contactsPhotoSize + st::contactsPadding.bottom())
, _revokeWidth(st::normalFont->width(lang(lng_channels_too_much_public_revoke)))
, _revokeCallback(std::move(revokeCallback)) {
	setMouseTracking(true);

	resize(width(), 5 * _rowHeight);

	request(MTPchannels_GetAdminedPublicChannels()).done([this](const MTPmessages_Chats &result) {
		if (auto chats = Api::getChatsFromMessagesChats(result)) {
			for_const (auto &chat, chats->v) {
				if (auto peer = App::feedChat(chat)) {
					if (!peer->isChannel() || peer->userName().isEmpty()) {
						continue;
					}

					auto row = ChatRow(peer);
					row.peer = peer;
					row.name.setText(st::contactsNameStyle, peer->name, _textNameOptions);
					row.status.setText(st::defaultTextStyle, Messenger::Instance().createInternalLink(textcmdLink(1, peer->userName())), _textDlgOptions);
					_rows.push_back(std::move(row));
				}
			}
		}
		resize(width(), _rows.size() * _rowHeight);
		update();
	}).send();
}

RevokePublicLinkBox::RevokePublicLinkBox(QWidget*, base::lambda<void()> revokeCallback)
: _aboutRevoke(this, lang(lng_channels_too_much_public_about), Ui::FlatLabel::InitType::Simple, st::aboutRevokePublicLabel)
, _revokeCallback(std::move(revokeCallback)) {
}

void RevokePublicLinkBox::prepare() {
	_innerTop = st::boxPadding.top() + _aboutRevoke->height() + st::boxPadding.top();
	_inner = setInnerWidget(object_ptr<Inner>(this, [this] {
		closeBox();
		if (_revokeCallback) {
			_revokeCallback();
		}
	}), st::boxLayerScroll, _innerTop);

	addButton(langFactory(lng_cancel), [this] { closeBox(); });

	subscribe(Auth().downloaderTaskFinished(), [this] { update(); });

	_inner->resizeToWidth(st::boxWideWidth);
	setDimensions(st::boxWideWidth, _innerTop + _inner->height());
}

void RevokePublicLinkBox::Inner::mouseMoveEvent(QMouseEvent *e) {
	updateSelected();
}

void RevokePublicLinkBox::Inner::updateSelected() {
	auto point = mapFromGlobal(QCursor::pos());
	PeerData *selected = nullptr;
	auto top = _rowsTop;
	for_const (auto &row, _rows) {
		auto revokeLink = rtlrect(width() - st::contactsPadding.right() - st::contactsCheckPosition.x() - _revokeWidth, top + st::contactsPadding.top() + (st::contactsPhotoSize - st::normalFont->height) / 2, _revokeWidth, st::normalFont->height, width());
		if (revokeLink.contains(point)) {
			selected = row.peer;
			break;
		}
		top += _rowHeight;
	}
	if (selected != _selected) {
		_selected = selected;
		setCursor((_selected || _pressed) ? style::cur_pointer : style::cur_default);
		update();
	}
}

void RevokePublicLinkBox::Inner::mousePressEvent(QMouseEvent *e) {
	if (_pressed != _selected) {
		_pressed = _selected;
		update();
	}
}

void RevokePublicLinkBox::Inner::mouseReleaseEvent(QMouseEvent *e) {
	auto pressed = base::take(_pressed);
	setCursor((_selected || _pressed) ? style::cur_pointer : style::cur_default);
	if (pressed && pressed == _selected) {
		auto text_method = pressed->isMegagroup() ? lng_channels_too_much_public_revoke_confirm_group : lng_channels_too_much_public_revoke_confirm_channel;
		auto text = text_method(lt_link, Messenger::Instance().createInternalLink(pressed->userName()), lt_group, pressed->name);
		auto confirmText = lang(lng_channels_too_much_public_revoke);
		_weakRevokeConfirmBox = Ui::show(Box<ConfirmBox>(text, confirmText, base::lambda_guarded(this, [this, pressed]() {
			if (_revokeRequestId) return;
			_revokeRequestId = request(MTPchannels_UpdateUsername(pressed->asChannel()->inputChannel, MTP_string(""))).done([this](const MTPBool &result) {
				if (_weakRevokeConfirmBox) {
					_weakRevokeConfirmBox->closeBox();
				}
				if (_revokeCallback) {
					_revokeCallback();
				}
			}).send();
		})), KeepOtherLayers);
	}
}

void RevokePublicLinkBox::Inner::paintEvent(QPaintEvent *e) {
	Painter p(this);
	p.translate(0, _rowsTop);
	for_const (auto &row, _rows) {
		paintChat(p, row, (row.peer == _selected));
		p.translate(0, _rowHeight);
	}
}

void RevokePublicLinkBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	_aboutRevoke->moveToLeft(st::boxPadding.left(), st::boxPadding.top());
}

void RevokePublicLinkBox::Inner::paintChat(Painter &p, const ChatRow &row, bool selected) const {
	auto peer = row.peer;
	peer->paintUserpicLeft(p, st::contactsPadding.left(), st::contactsPadding.top(), width(), st::contactsPhotoSize);

	p.setPen(st::contactsNameFg);

	int32 namex = st::contactsPadding.left() + st::contactsPhotoSize + st::contactsPadding.left();
	int32 namew = width() - namex - st::contactsPadding.right() - (_revokeWidth + st::contactsCheckPosition.x() * 2);
	if (peer->isVerified()) {
		auto icon = &st::dialogsVerifiedIcon;
		namew -= icon->width();
		icon->paint(p, namex + qMin(row.name.maxWidth(), namew), st::contactsPadding.top() + st::contactsNameTop, width());
	}
	row.name.drawLeftElided(p, namex, st::contactsPadding.top() + st::contactsNameTop, namew, width());

	p.setFont(selected ? st::linkOverFont : st::linkFont);
	p.setPen(selected ? st::defaultLinkButton.overColor : st::defaultLinkButton.color);
	p.drawTextRight(st::contactsPadding.right() + st::contactsCheckPosition.x(), st::contactsPadding.top() + (st::contactsPhotoSize - st::normalFont->height) / 2, width(), lang(lng_channels_too_much_public_revoke), _revokeWidth);

	p.setPen(st::contactsStatusFg);
	p.setTextPalette(st::revokePublicLinkStatusPalette);
	row.status.drawLeftElided(p, namex, st::contactsPadding.top() + st::contactsStatusTop, namew, width());
	p.restoreTextPalette();
}
