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
Copyright (c) 2014-2015 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "lang.h"

#include "application.h"
#include "addcontactbox.h"
#include "contactsbox.h"
#include "mainwidget.h"
#include "window.h"

AddContactBox::AddContactBox(QString fname, QString lname, QString phone) :
	_peer(0),
	_addButton(this, lang(lng_add_contact), st::btnSelectDone),
	_retryButton(this, lang(lng_try_other_contact), st::btnSelectDone),
	_cancelButton(this, lang(lng_cancel), st::btnSelectCancel),
    _firstInput(this, st::inpAddContact, lang(lng_signup_firstname), fname),
    _lastInput(this, st::inpAddContact, lang(lng_signup_lastname), lname),
    _phoneInput(this, st::inpAddContact, lang(lng_contact_phone), phone.isEmpty() ? phone : App::formatPhone(phone)),
	_invertOrder(langFirstNameGoesSecond()),
	_contactId(0), _addRequest(0) {

	if (!phone.isEmpty()) {
		_phoneInput.setDisabled(true);
	}

	initBox();
}

AddContactBox::AddContactBox(PeerData *peer) :
	_peer(peer),
	_addButton(this, lang(lng_settings_save), st::btnSelectDone),
	_retryButton(this, lang(lng_try_other_contact), st::btnSelectDone),
	_cancelButton(this, lang(lng_cancel), st::btnSelectCancel),
	_firstInput(this, st::inpAddContact, lang(peer->isUser() ? lng_signup_firstname : lng_dlg_new_group_name), peer->isUser() ? peer->asUser()->firstName : peer->name),
	_lastInput(this, st::inpAddContact, lang(lng_signup_lastname), peer->isUser() ? peer->asUser()->lastName : QString()),
    _phoneInput(this, st::inpAddContact, lang(lng_contact_phone)),
	_invertOrder((!peer || !peer->isChat()) && langFirstNameGoesSecond()),
	_contactId(0), _addRequest(0) {

	initBox();
}

void AddContactBox::initBox() {
	if (_invertOrder) {
		setTabOrder(&_lastInput, &_firstInput);
	}
	if (_peer) {
		if (_peer->isUser()) {
			_boxTitle = lang(_peer == App::self() ? lng_edit_self_title : lng_edit_contact_title);
			setMaxHeight(st::old_boxTitleHeight + st::addContactPadding.top() + 2 * _firstInput.height() + 1 * st::addContactDelta + st::addContactPadding.bottom() + _addButton.height());
		} else if (_peer->isChat()) {
			_boxTitle = lang(lng_edit_group_title);
			setMaxHeight(st::old_boxTitleHeight + st::addContactPadding.top() + 1 * _firstInput.height() + st::addContactPadding.bottom() + _addButton.height());
		}
	} else {
		bool readyToAdd = !_phoneInput.text().isEmpty() && (!_firstInput.text().isEmpty() || !_lastInput.text().isEmpty());
		_boxTitle = lang(readyToAdd ? lng_confirm_contact_data : lng_enter_contact_data);
		setMaxHeight(st::old_boxTitleHeight + st::addContactPadding.top() + 3 * _firstInput.height() + 2 * st::addContactDelta + st::addContactPadding.bottom() + _addButton.height());
	}
	_retryButton.hide();

	connect(&_addButton, SIGNAL(clicked()), this, SLOT(onSend()));
	connect(&_retryButton, SIGNAL(clicked()), this, SLOT(onRetry()));
	connect(&_cancelButton, SIGNAL(clicked()), this, SLOT(onClose()));

	prepare();
}

void AddContactBox::hideAll() {
	_firstInput.hide();
	_lastInput.hide();
	_phoneInput.hide();
	_addButton.hide();
	_retryButton.hide();
	_cancelButton.hide();
}

void AddContactBox::showAll() {
	_firstInput.show();
	if (_peer && (_peer->isChat() || _peer->isChannel())) {
		_lastInput.hide();
	} else {
		_lastInput.show();
	}
	if (_peer) {
		_phoneInput.hide();
	} else {
		_phoneInput.show();
	}
	_addButton.show();
	_cancelButton.show();
}

void AddContactBox::showDone() {
	if ((_firstInput.text().isEmpty() && _lastInput.text().isEmpty()) || _phoneInput.isHidden() || !_phoneInput.isEnabled()) {
		(_invertOrder ? _lastInput : _firstInput).setFocus();
	} else {
		_phoneInput.setFocus();
	}
}

void AddContactBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		if (_firstInput.hasFocus()) {
			if (_peer && (_peer->isChat() || _peer->isChannel())) {
				if (_firstInput.text().trimmed().isEmpty()) {
					_firstInput.setFocus();
					_firstInput.notaBene();
				} else {
					onSend();
				}
			} else {
				_lastInput.setFocus();
			}
		} else if (_lastInput.hasFocus()) {
			if (_peer) {
				if (_firstInput.text().trimmed().isEmpty()) {
					_firstInput.setFocus();
					_firstInput.notaBene();
				} else if (_lastInput.text().trimmed().isEmpty()) {
					_lastInput.setFocus();
					_lastInput.notaBene();
				} else {
					onSend();
				}
			} else if (_phoneInput.isEnabled()) {
				_phoneInput.setFocus();
			} else {
				onSend();
			}
		} else if (_phoneInput.hasFocus()) {
			if (_firstInput.text().trimmed().isEmpty()) {
				_firstInput.setFocus();
				_firstInput.notaBene();
			} else if (_lastInput.text().trimmed().isEmpty()) {
				_lastInput.setFocus();
				_lastInput.notaBene();
			} else {
				onSend();
			}
		}
	} else {
		AbstractBox::keyPressEvent(e);
	}
}

void AddContactBox::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (paint(p)) return;

	if (_retryButton.isHidden()) {
		paintTitle(p, _boxTitle, true);
	} else {
		// draw box text
		p.setPen(st::black->p);
		p.setFont(st::old_boxTitleFont->f);
		int32 h = size().height() - st::boxPadding.top() * 2 - _retryButton.height() - st::boxPadding.bottom();
		p.drawText(QRect(st::boxPadding.left(), st::boxPadding.top(), width() - st::boxPadding.left() - st::boxPadding.right(), h), lng_contact_not_joined(lt_name, _sentName), style::al_topleft);
	}

	// paint shadows
	p.fillRect(0, size().height() - st::btnSelectCancel.height - st::scrollDef.bottomsh, width(), st::scrollDef.bottomsh, st::scrollDef.shColor->b);

	// paint button sep
	p.fillRect(st::btnSelectCancel.width, size().height() - st::btnSelectCancel.height, st::lineWidth, st::btnSelectCancel.height, st::btnSelectSep->b);
}

void AddContactBox::resizeEvent(QResizeEvent *e) {
	if (_invertOrder) {
		_lastInput.setGeometry(st::addContactPadding.left(), st::old_boxTitleHeight + st::addContactPadding.top(), width() - st::addContactPadding.left() - st::addContactPadding.right(), _lastInput.height());
		_firstInput.setGeometry(st::addContactPadding.left(), _lastInput.y() + _lastInput.height() + st::addContactDelta, _lastInput.width(), _lastInput.height());
		_phoneInput.setGeometry(st::addContactPadding.left(), _firstInput.y() + _firstInput.height() + st::addContactDelta, _lastInput.width(), _lastInput.height());
	} else {
		_firstInput.setGeometry(st::addContactPadding.left(), st::old_boxTitleHeight + st::addContactPadding.top(), width() - st::addContactPadding.left() - st::addContactPadding.right(), _firstInput.height());
		_lastInput.setGeometry(st::addContactPadding.left(), _firstInput.y() + _firstInput.height() + st::addContactDelta, _firstInput.width(), _firstInput.height());
		_phoneInput.setGeometry(st::addContactPadding.left(), _lastInput.y() + _lastInput.height() + st::addContactDelta, _lastInput.width(), _lastInput.height());
	}

	_cancelButton.move(0, height() - _cancelButton.height());
	_addButton.move(width() - _addButton.width(), height() - _addButton.height());
	_retryButton.move(width() - _retryButton.width(), height() - _retryButton.height());
}

void AddContactBox::onSend() {
	if (_addRequest) return;

	QString firstName = _firstInput.text().trimmed(), lastName = _lastInput.text().trimmed(), phone = _phoneInput.text().trimmed();
	if (firstName.isEmpty() && lastName.isEmpty()) {
		if (_invertOrder) {
			_lastInput.setFocus();
			_lastInput.notaBene();
		} else {
			_firstInput.setFocus();
			_firstInput.notaBene();
		}
		return;
	} else if (!_peer && !App::isValidPhone(phone)) {
		_phoneInput.setFocus();
		_phoneInput.notaBene();
		return;
	}
	if (firstName.isEmpty()) {
		firstName = lastName;
		lastName = QString();
	}
	_sentName = firstName;
	if (_peer == App::self()) {
		_addRequest = MTP::send(MTPaccount_UpdateProfile(MTP_string(firstName), MTP_string(lastName)), rpcDone(&AddContactBox::onSaveSelfDone), rpcFail(&AddContactBox::onSaveSelfFail));
	} else if (_peer) {
		if (_peer->isChat()) {
			_addRequest = MTP::send(MTPmessages_EditChatTitle(_peer->asChat()->inputChat, MTP_string(firstName)), rpcDone(&AddContactBox::onSaveChatDone), rpcFail(&AddContactBox::onSaveFail));
		} else {
			_contactId = MTP::nonce<uint64>();
			QVector<MTPInputContact> v(1, MTP_inputPhoneContact(MTP_long(_contactId), MTP_string(_peer->asUser()->phone), MTP_string(firstName), MTP_string(lastName)));
			_addRequest = MTP::send(MTPcontacts_ImportContacts(MTP_vector<MTPInputContact>(v), MTP_bool(false)), rpcDone(&AddContactBox::onSaveUserDone), rpcFail(&AddContactBox::onSaveFail));
		}
	} else {
		_contactId = MTP::nonce<uint64>();
		QVector<MTPInputContact> v(1, MTP_inputPhoneContact(MTP_long(_contactId), MTP_string(phone), MTP_string(firstName), MTP_string(lastName)));
		_addRequest = MTP::send(MTPcontacts_ImportContacts(MTP_vector<MTPInputContact>(v), MTP_bool(false)), rpcDone(&AddContactBox::onImportDone));
	}
}

void AddContactBox::onSaveSelfDone(const MTPUser &user) {
	App::feedUsers(MTP_vector<MTPUser>(1, user));
	emit closed();
}

bool AddContactBox::onSaveSelfFail(const RPCError &error) {
	if (error.type().startsWith(qsl("FLOOD_WAIT_"))) return false;

	QString err(error.type());
	QString firstName = textOneLine(_firstInput.text()), lastName = textOneLine(_lastInput.text());
	if (err == "NAME_NOT_MODIFIED") {
		App::self()->setName(firstName, lastName, QString(), textOneLine(App::self()->username));
		emit closed();
		return true;
	} else if (err == "FIRSTNAME_INVALID") {
		_firstInput.setFocus();
		_firstInput.notaBene();
		return true;
	} else if (err == "LASTNAME_INVALID") {
		_lastInput.setFocus();
		_lastInput.notaBene();
		return true;
	}
	_firstInput.setFocus();
	return true;
}

bool AddContactBox::onSaveFail(const RPCError &error) {
	if (error.type().startsWith(qsl("FLOOD_WAIT_"))) return false;

	_addRequest = 0;
	QString err(error.type());
	QString firstName = _firstInput.text().trimmed(), lastName = _lastInput.text().trimmed();
	if (err == "CHAT_TITLE_NOT_MODIFIED") {
		_peer->updateName(firstName, QString(), QString());
		emit closed();
		return true;
	} else if (err == "NO_CHAT_TITLE") {
		_firstInput.setFocus();
		_firstInput.notaBene();
		return true;
	}
	_firstInput.setFocus();
	return true;
}

void AddContactBox::onImportDone(const MTPcontacts_ImportedContacts &res) {
	if (isHidden() || !App::main()) return;

	const MTPDcontacts_importedContacts &d(res.c_contacts_importedContacts());
	App::feedUsers(d.vusers);

	const QVector<MTPImportedContact> &v(d.vimported.c_vector().v);
	int32 uid = 0;
	if (!v.isEmpty()) {
		const MTPDimportedContact &c(v.front().c_importedContact());
		if (c.vclient_id.v != _contactId) return;

		uid = c.vuser_id.v;
		if (uid && !App::userLoaded(uid)) {
			uid = 0;
		}
	}
	if (uid) {
		App::main()->addNewContact(uid);
		App::wnd()->hideLayer();
	} else {
		_addButton.hide();
		_firstInput.hide();
		_lastInput.hide();
		_phoneInput.hide();
		_retryButton.show();
		int32 theight = st::old_boxTitleFont->m.boundingRect(0, 0, width() - st::boxPadding.left() - st::boxPadding.right(), 1, Qt::TextWordWrap, lng_contact_not_joined(lt_name, _sentName)).height();
		int32 h = st::boxPadding.top() * 2 + theight + _retryButton.height() + st::boxPadding.bottom();
		setMaxHeight(h);
		update();
	}
}

void AddContactBox::onSaveChatDone(const MTPUpdates &updates) {
	App::main()->sentUpdatesReceived(updates);
	emit closed();
}

void AddContactBox::onSaveUserDone(const MTPcontacts_ImportedContacts &res) {
	const MTPDcontacts_importedContacts &d(res.c_contacts_importedContacts());
	App::feedUsers(d.vusers);
	emit closed();
}

void AddContactBox::onRetry() {
	_addRequest = 0;
	_contactId = 0;
	_addButton.show();
	_cancelButton.move(_cancelButton.x(), _addButton.y());
	showAll();
	_firstInput.setText(QString());
	_firstInput.updatePlaceholder();
	_lastInput.setText(QString());
	_lastInput.updatePlaceholder();
	_phoneInput.setText(QString());
	_phoneInput.updatePlaceholder();
	_phoneInput.setDisabled(false);
	_retryButton.hide();
	_firstInput.setFocus();
	setMaxHeight(st::old_boxTitleHeight + st::addContactPadding.top() + 3 * _firstInput.height() + 2 * st::addContactDelta + st::addContactPadding.bottom() + _addButton.height());
	update();
}

EditChannelBox::EditChannelBox(ChannelData *channel) :
_channel(channel),
_saveButton(this, lang(lng_settings_save), st::btnSelectDone),
_cancelButton(this, lang(lng_cancel), st::btnSelectCancel),
_title(this, st::inpAddContact, lang(lng_dlg_new_channel_name), _channel->name),
_descriptionOver(false),
a_descriptionBg(st::newGroupName.bgColor->c, st::newGroupName.bgColor->c),
a_descriptionBorder(st::newGroupName.borderColor->c, st::newGroupName.borderColor->c),
a_description(animFunc(this, &EditChannelBox::descriptionAnimStep)),
_description(this, st::newGroupDescription, lang(lng_create_group_description), _channel->about),
_publicLink(this, lang(channel->isPublic() ? lng_profile_edit_public_link : lng_profile_create_public_link)),
_saveTitleRequestId(0), _saveDescriptionRequestId(0) {
	_boxTitle = lang(lng_edit_channel_title);

	_description.installEventFilter(this);

	connect(App::main(), SIGNAL(peerNameChanged(PeerData*, const PeerData::Names&, const PeerData::NameFirstChars&)), this, SLOT(peerUpdated(PeerData*)));

	setMouseTracking(true);

	_description.resize(width() - st::newGroupPadding.left() - st::newGroupPadding.right() - st::newGroupDescriptionPadding.left() - st::newGroupDescriptionPadding.right(), _title.height() - st::newGroupDescriptionPadding.top() - st::newGroupDescriptionPadding.bottom());
	_description.setMinHeight(_description.height());
	_description.setMaxHeight(3 * _description.height() + 2 * st::newGroupDescriptionPadding.top() + 2 * st::newGroupDescriptionPadding.bottom());

	updateMaxHeight();
	_description.setMaxLength(MaxChannelDescription);
	connect(&_description, SIGNAL(resized()), this, SLOT(onDescriptionResized()));
	connect(&_description, SIGNAL(submitted(bool)), this, SLOT(onSave()));
	connect(&_description, SIGNAL(cancelled()), this, SLOT(onClose()));

	connect(&_saveButton, SIGNAL(clicked()), this, SLOT(onSave()));
	connect(&_cancelButton, SIGNAL(clicked()), this, SLOT(onClose()));

	connect(&_publicLink, SIGNAL(clicked()), this, SLOT(onPublicLink()));

	prepare();
}

void EditChannelBox::hideAll() {
	_title.hide();
	_description.hide();
	_saveButton.hide();
	_cancelButton.hide();
	_publicLink.hide();
}

void EditChannelBox::showAll() {
	_title.show();
	_description.show();
	_saveButton.show();
	_cancelButton.show();
	_publicLink.show();
}

void EditChannelBox::showDone() {
	_title.setFocus();
}

void EditChannelBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		if (_title.hasFocus()) {
			onSave();
		}
	} else {
		AbstractBox::keyPressEvent(e);
	}
}

void EditChannelBox::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (paint(p)) return;

	paintTitle(p, _boxTitle, true);

	QRect descRect(descriptionRect());
	if (descRect.intersects(e->rect())) {
		p.fillRect(descRect, a_descriptionBg.current());
		if (st::newGroupName.borderWidth) {
			QBrush b(a_descriptionBorder.current());
			p.fillRect(descRect.x(), descRect.y(), descRect.width() - st::newGroupName.borderWidth, st::newGroupName.borderWidth, b);
			p.fillRect(descRect.x() + descRect.width() - st::newGroupName.borderWidth, descRect.y(), st::newGroupName.borderWidth, descRect.height() - st::newGroupName.borderWidth, b);
			p.fillRect(descRect.x() + st::newGroupName.borderWidth, descRect.y() + descRect.height() - st::newGroupName.borderWidth, descRect.width() - st::newGroupName.borderWidth, st::newGroupName.borderWidth, b);
			p.fillRect(descRect.x(), descRect.y() + st::newGroupName.borderWidth, st::newGroupName.borderWidth, descRect.height() - st::newGroupName.borderWidth, b);
		}
		if (descRect.contains(e->rect())) {
			return;
		}
	}

	// paint shadows
	p.fillRect(0, size().height() - st::btnSelectCancel.height - st::scrollDef.bottomsh, width(), st::scrollDef.bottomsh, st::scrollDef.shColor->b);

	// paint button sep
	p.fillRect(st::btnSelectCancel.width, size().height() - st::btnSelectCancel.height, st::lineWidth, st::btnSelectCancel.height, st::btnSelectSep->b);
}

bool EditChannelBox::descriptionAnimStep(float64 ms) {
	float dt = ms / st::newGroupName.phDuration;
	bool res = true;
	if (dt >= 1) {
		res = false;
		a_descriptionBg.finish();
		a_descriptionBorder.finish();
	} else {
		a_descriptionBg.update(dt, st::newGroupName.phColorFunc);
		a_descriptionBorder.update(dt, st::newGroupName.phColorFunc);
	}
	update(descriptionRect());
	return res;
}

void EditChannelBox::peerUpdated(PeerData *peer) {
	if (peer == _channel) {
		_publicLink.setText(lang(_channel->isPublic() ? lng_profile_edit_public_link : lng_profile_create_public_link));
	}
}

void EditChannelBox::onDescriptionResized() {
	updateMaxHeight();
	update();
}

QRect EditChannelBox::descriptionRect() const {
	return rtlrect(_description.x() - st::newGroupDescriptionPadding.left(), _description.y() - st::newGroupDescriptionPadding.top(), _description.width() + st::newGroupDescriptionPadding.left() + st::newGroupDescriptionPadding.right(), _description.height() + st::newGroupDescriptionPadding.top() + st::newGroupDescriptionPadding.bottom(), width());
}

void EditChannelBox::updateMaxHeight() {
	int32 h = st::old_boxTitleHeight + st::newGroupPadding.top() + _title.height();
	h += st::newGroupDescriptionSkip + st::newGroupDescriptionPadding.top() + _description.height() + st::newGroupDescriptionPadding.bottom();
	h += st::newGroupPublicLinkSkip + _publicLink.height();
	h += st::newGroupPadding.bottom() + _saveButton.height();
	setMaxHeight(h);
}

bool EditChannelBox::eventFilter(QObject *obj, QEvent *e) {
	if (obj == &_description) {
		if (e->type() == QEvent::FocusIn) {
			a_descriptionBorder.start(st::newGroupName.borderActive->c);
			a_descriptionBg.start(st::newGroupName.bgActive->c);
			a_description.start();
		} else if (e->type() == QEvent::FocusOut) {
			a_descriptionBorder.start(st::newGroupName.borderColor->c);
			a_descriptionBg.start(st::newGroupName.bgColor->c);
			a_description.start();
		}
	}
	return AbstractBox::eventFilter(obj, e);
}

void EditChannelBox::resizeEvent(QResizeEvent *e) {
	_title.resize(width() - st::newGroupPadding.left() - st::newGroupPadding.right(), _title.height());
	_title.moveToLeft(st::newGroupPadding.left(), st::old_boxTitleHeight + st::newGroupPadding.top(), width());

	_description.moveToLeft(st::newGroupPadding.left() + st::newGroupDescriptionPadding.left(), _title.y() + _title.height() + st::newGroupDescriptionSkip + st::newGroupDescriptionPadding.top(), width());

	_publicLink.moveToLeft(st::newGroupPadding.left(), _description.y() + _description.height() + st::newGroupDescriptionPadding.bottom() + st::newGroupPublicLinkSkip, width());

	int32 buttonTop = _publicLink.y() + _publicLink.height() + st::newGroupPadding.bottom();
	_cancelButton.move(0, buttonTop);
	_saveButton.move(width() - _saveButton.width(), buttonTop);
}

void EditChannelBox::mouseMoveEvent(QMouseEvent *e) {
	updateSelected(e->globalPos());
}

void EditChannelBox::updateSelected(const QPoint &cursorGlobalPosition) {
	QPoint p(mapFromGlobal(cursorGlobalPosition));

	bool descriptionOver = descriptionRect().contains(p);
	if (descriptionOver != _descriptionOver) {
		_descriptionOver = descriptionOver;
	}

	setCursor(_descriptionOver ? style::cur_text : style::cur_default);
}

void EditChannelBox::mousePressEvent(QMouseEvent *e) {
	mouseMoveEvent(e);
	if (_descriptionOver) {
		_description.setFocus();
	}
}

void EditChannelBox::leaveEvent(QEvent *e) {
	updateSelected(QCursor::pos());
}

void EditChannelBox::onSave() {
	if (_saveTitleRequestId || _saveDescriptionRequestId) return;

	QString title = _title.text().trimmed(), description = _description.getLastText().trimmed();
	if (title.isEmpty()) {
		_title.setFocus();
		_title.notaBene();
		return;
	}
	_sentTitle = title;
	_sentDescription = description;
	_saveTitleRequestId = MTP::send(MTPchannels_EditTitle(_channel->inputChannel, MTP_string(_sentTitle)), rpcDone(&EditChannelBox::onSaveTitleDone), rpcFail(&EditChannelBox::onSaveFail));
}

void EditChannelBox::onPublicLink() {
	App::wnd()->replaceLayer(new SetupChannelBox(_channel, true));
}

void EditChannelBox::saveDescription() {
	_saveDescriptionRequestId = MTP::send(MTPchannels_EditAbout(_channel->inputChannel, MTP_string(_sentDescription)), rpcDone(&EditChannelBox::onSaveDescriptionDone), rpcFail(&EditChannelBox::onSaveFail));
}

bool EditChannelBox::onSaveFail(const RPCError &error, mtpRequestId req) {
	if (mtpIsFlood(error)) return false;

	QString err(error.type());
	if (req == _saveTitleRequestId) {
		_saveTitleRequestId = 0;
		if (err == qstr("CHAT_NOT_MODIFIED") || err == qstr("CHAT_TITLE_NOT_MODIFIED")) {
			_channel->setName(_sentTitle, _channel->username);
			saveDescription();
			return true;
		} else if (err == qstr("NO_CHAT_TITLE")) {
			_title.setFocus();
			_title.notaBene();
			return true;
		} else {
			_title.setFocus();
		}
	} else if (req == _saveDescriptionRequestId) {
		_saveDescriptionRequestId = 0;
		if (err == qstr("CHAT_ABOUT_NOT_MODIFIED")) {
			_channel->about = _sentDescription;
			if (App::api()) emit App::api()->fullPeerUpdated(_channel);
			onClose();
		} else {
			_description.setFocus();
		}
	}
	return true;
}

void EditChannelBox::onSaveTitleDone(const MTPUpdates &updates) {
	_saveTitleRequestId = 0;
	App::main()->sentUpdatesReceived(updates);
	saveDescription();
}

void EditChannelBox::onSaveDescriptionDone(const MTPBool &result) {
	_saveDescriptionRequestId = 0;
	_channel->about = _sentDescription;
	if (App::api()) emit App::api()->fullPeerUpdated(_channel);
	onClose();
}
