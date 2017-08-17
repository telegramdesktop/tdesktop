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
#include "boxes/confirm_box.h"

#include "styles/style_boxes.h"
#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "apiwrap.h"
#include "application.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/toast/toast.h"
#include "core/click_handler_types.h"
#include "storage/localstorage.h"
#include "auth_session.h"
#include "observer_peer.h"

TextParseOptions _confirmBoxTextOptions = {
	TextParseLinks | TextParseMultiline | TextParseRichText, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};

ConfirmBox::ConfirmBox(QWidget*, const QString &text, base::lambda_once<void()> confirmedCallback, base::lambda_once<void()> cancelledCallback)
: _confirmText(lang(lng_box_ok))
, _cancelText(lang(lng_cancel))
, _confirmStyle(st::defaultBoxButton)
, _text(st::boxWidth - st::boxPadding.left() - st::boxButtonPadding.right())
, _confirmedCallback(std::move(confirmedCallback))
, _cancelledCallback(std::move(cancelledCallback)) {
	init(text);
}

ConfirmBox::ConfirmBox(QWidget*, const QString &text, const QString &confirmText, base::lambda_once<void()> confirmedCallback, base::lambda_once<void()> cancelledCallback)
: _confirmText(confirmText)
, _cancelText(lang(lng_cancel))
, _confirmStyle(st::defaultBoxButton)
, _text(st::boxWidth - st::boxPadding.left() - st::boxButtonPadding.right())
, _confirmedCallback(std::move(confirmedCallback))
, _cancelledCallback(std::move(cancelledCallback)) {
	init(text);
}

ConfirmBox::ConfirmBox(QWidget*, const QString &text, const QString &confirmText, const style::RoundButton &confirmStyle, base::lambda_once<void()> confirmedCallback, base::lambda_once<void()> cancelledCallback)
: _confirmText(confirmText)
, _cancelText(lang(lng_cancel))
, _confirmStyle(confirmStyle)
, _text(st::boxWidth - st::boxPadding.left() - st::boxButtonPadding.right())
, _confirmedCallback(std::move(confirmedCallback))
, _cancelledCallback(std::move(cancelledCallback)) {
	init(text);
}

ConfirmBox::ConfirmBox(QWidget*, const QString &text, const QString &confirmText, const QString &cancelText, base::lambda_once<void()> confirmedCallback, base::lambda_once<void()> cancelledCallback)
: _confirmText(confirmText)
, _cancelText(cancelText)
, _confirmStyle(st::defaultBoxButton)
, _text(st::boxWidth - st::boxPadding.left() - st::boxButtonPadding.right())
, _confirmedCallback(std::move(confirmedCallback))
, _cancelledCallback(std::move(cancelledCallback)) {
	init(text);
}

ConfirmBox::ConfirmBox(QWidget*, const QString &text, const QString &confirmText, const style::RoundButton &confirmStyle, const QString &cancelText, base::lambda_once<void()> confirmedCallback, base::lambda_once<void()> cancelledCallback)
: _confirmText(confirmText)
, _cancelText(cancelText)
, _confirmStyle(st::defaultBoxButton)
, _text(st::boxWidth - st::boxPadding.left() - st::boxButtonPadding.right())
, _confirmedCallback(std::move(confirmedCallback))
, _cancelledCallback(std::move(cancelledCallback)) {
	init(text);
}

ConfirmBox::ConfirmBox(const InformBoxTag &, const QString &text, const QString &doneText, base::lambda<void()> closedCallback)
: _confirmText(doneText)
, _confirmStyle(st::defaultBoxButton)
, _informative(true)
, _text(st::boxWidth - st::boxPadding.left() - st::boxButtonPadding.right())
, _confirmedCallback(generateInformCallback(closedCallback))
, _cancelledCallback(generateInformCallback(closedCallback)) {
	init(text);
}

base::lambda_once<void()> ConfirmBox::generateInformCallback(base::lambda<void()> closedCallback) {
	return base::lambda_guarded(this, [this, closedCallback] {
		closeBox();
		if (closedCallback) {
			closedCallback();
		}
	});
}

void ConfirmBox::init(const QString &text) {
	_text.setText(st::boxLabelStyle, text, _informative ? _confirmBoxTextOptions : _textPlainOptions);
}

void ConfirmBox::prepare() {
	addButton([this] { return _confirmText; }, [this] { confirmed(); }, _confirmStyle);
	if (!_informative) {
		addButton([this] { return _cancelText; }, [this] { _cancelled = true; closeBox(); });
	}
	subscribe(boxClosing, [this] {
		if (!_confirmed && (!_strictCancel || _cancelled) && _cancelledCallback) {
			_cancelledCallback();
		}
	});
	textUpdated();
}

void ConfirmBox::textUpdated() {
	_textWidth = st::boxWidth - st::boxPadding.left() - st::boxButtonPadding.right();
	_textHeight = qMin(_text.countHeight(_textWidth), 16 * st::boxLabelStyle.lineHeight);
	setDimensions(st::boxWidth, st::boxPadding.top() + _textHeight + st::boxPadding.bottom());

	setMouseTracking(_text.hasLinks());
}

void ConfirmBox::confirmed() {
	if (!_confirmed) {
		_confirmed = true;
		if (_confirmedCallback) {
			_confirmedCallback();
		}
	}
}

void ConfirmBox::mouseMoveEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	updateHover();
}

void ConfirmBox::mousePressEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	updateHover();
	ClickHandler::pressed();
	return BoxContent::mousePressEvent(e);
}

void ConfirmBox::mouseReleaseEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	updateHover();
	if (auto activated = ClickHandler::unpressed()) {
		Ui::hideLayer();
		App::activateClickHandler(activated, e->button());
	}
	return BoxContent::mouseReleaseEvent(e);
}

void ConfirmBox::leaveEventHook(QEvent *e) {
	ClickHandler::clearActive(this);
}

void ConfirmBox::clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) {
	setCursor(active ? style::cur_pointer : style::cur_default);
	update();
}

void ConfirmBox::clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) {
	update();
}

void ConfirmBox::updateLink() {
	_lastMousePos = QCursor::pos();
	updateHover();
}

void ConfirmBox::updateHover() {
	auto m = mapFromGlobal(_lastMousePos);
	auto state = _text.getStateLeft(m - QPoint(st::boxPadding.left(), st::boxPadding.top()), _textWidth, width());

	ClickHandler::setActive(state.link, this);
}

void ConfirmBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		confirmed();
	} else {
		BoxContent::keyPressEvent(e);
	}
}

void ConfirmBox::paintEvent(QPaintEvent *e) {
	BoxContent::paintEvent(e);

	Painter p(this);

	// draw box title / text
	p.setPen(st::boxTextFg);
	_text.drawLeftElided(p, st::boxPadding.left(), st::boxPadding.top(), _textWidth, width(), 16, style::al_left);
}

InformBox::InformBox(QWidget*, const QString &text, base::lambda<void()> closedCallback) : ConfirmBox(ConfirmBox::InformBoxTag(), text, lang(lng_box_ok), std::move(closedCallback)) {
}

InformBox::InformBox(QWidget*, const QString &text, const QString &doneText, base::lambda<void()> closedCallback) : ConfirmBox(ConfirmBox::InformBoxTag(), text, doneText, std::move(closedCallback)) {
}

MaxInviteBox::MaxInviteBox(QWidget*, not_null<ChannelData*> channel) : BoxContent()
, _channel(channel)
, _text(st::boxLabelStyle, lng_participant_invite_sorry(lt_count, Global::ChatSizeMax()), _confirmBoxTextOptions, st::boxWidth - st::boxPadding.left() - st::boxButtonPadding.right()) {
}

void MaxInviteBox::prepare() {
	setMouseTracking(true);

	addButton(langFactory(lng_box_ok), [this] { closeBox(); });

	_textWidth = st::boxWidth - st::boxPadding.left() - st::boxButtonPadding.right();
	_textHeight = qMin(_text.countHeight(_textWidth), 16 * st::boxLabelStyle.lineHeight);
	setDimensions(st::boxWidth, st::boxPadding.top() + _textHeight + st::boxTextFont->height + st::boxTextFont->height * 2 + st::newGroupLinkPadding.bottom());

	subscribe(Notify::PeerUpdated(), Notify::PeerUpdatedHandler(Notify::PeerUpdate::Flag::InviteLinkChanged, [this](const Notify::PeerUpdate &update) {
		if (update.peer == _channel) {
			rtlupdate(_invitationLink);
		}
	}));
}

void MaxInviteBox::mouseMoveEvent(QMouseEvent *e) {
	updateSelected(e->globalPos());
}

void MaxInviteBox::mousePressEvent(QMouseEvent *e) {
	mouseMoveEvent(e);
	if (_linkOver) {
		if (_channel->inviteLink().isEmpty()) {
			Auth().api().exportInviteLink(_channel);
		} else {
			QGuiApplication::clipboard()->setText(_channel->inviteLink());
			Ui::Toast::Show(lang(lng_create_channel_link_copied));
		}
	}
}

void MaxInviteBox::leaveEventHook(QEvent *e) {
	updateSelected(QCursor::pos());
}

void MaxInviteBox::updateSelected(const QPoint &cursorGlobalPosition) {
	QPoint p(mapFromGlobal(cursorGlobalPosition));

	bool linkOver = _invitationLink.contains(p);
	if (linkOver != _linkOver) {
		_linkOver = linkOver;
		update();
		setCursor(_linkOver ? style::cur_pointer : style::cur_default);
	}
}

void MaxInviteBox::paintEvent(QPaintEvent *e) {
	BoxContent::paintEvent(e);

	Painter p(this);

	// draw box title / text
	p.setPen(st::boxTextFg);
	_text.drawLeftElided(p, st::boxPadding.left(), st::boxPadding.top(), _textWidth, width(), 16, style::al_left);

	QTextOption option(style::al_left);
	option.setWrapMode(QTextOption::WrapAnywhere);
	p.setFont(_linkOver ? st::defaultInputField.font->underline() : st::defaultInputField.font);
	p.setPen(st::defaultLinkButton.color);
	auto inviteLinkText = _channel->inviteLink().isEmpty() ? lang(lng_group_invite_create) : _channel->inviteLink();
	p.drawText(_invitationLink, inviteLinkText, option);
}

void MaxInviteBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);
	_invitationLink = myrtlrect(st::boxPadding.left(), st::boxPadding.top() + _textHeight + st::boxTextFont->height, width() - st::boxPadding.left() - st::boxPadding.right(), 2 * st::boxTextFont->height);
}

ConvertToSupergroupBox::ConvertToSupergroupBox(QWidget*, ChatData *chat)
: _chat(chat)
, _text(100)
, _note(100) {
}

void ConvertToSupergroupBox::prepare() {
	QStringList text;
	text.push_back(lang(lng_profile_convert_feature1));
	text.push_back(lang(lng_profile_convert_feature2));
	text.push_back(lang(lng_profile_convert_feature3));
	text.push_back(lang(lng_profile_convert_feature4));

	setTitle(langFactory(lng_profile_convert_title));

	addButton(langFactory(lng_profile_convert_confirm), [this] { convertToSupergroup(); });
	addButton(langFactory(lng_cancel), [this] { closeBox(); });

	_text.setText(st::boxLabelStyle, text.join('\n'), _confirmBoxTextOptions);
	_note.setText(st::boxLabelStyle, lng_profile_convert_warning(lt_bold_start, textcmdStartSemibold(), lt_bold_end, textcmdStopSemibold()), _confirmBoxTextOptions);
	_textWidth = st::boxWideWidth - st::boxPadding.left() - st::boxButtonPadding.right();
	_textHeight = _text.countHeight(_textWidth);
	setDimensions(st::boxWideWidth, _textHeight + st::boxPadding.bottom() + _note.countHeight(_textWidth));
}

void ConvertToSupergroupBox::convertToSupergroup() {
	MTP::send(MTPmessages_MigrateChat(_chat->inputChat), rpcDone(&ConvertToSupergroupBox::convertDone), rpcFail(&ConvertToSupergroupBox::convertFail));
}

void ConvertToSupergroupBox::convertDone(const MTPUpdates &updates) {
	Ui::hideLayer();
	App::main()->sentUpdatesReceived(updates);

	auto handleChats = [](auto &mtpChats) {
		for_const (auto &mtpChat, mtpChats.v) {
			if (mtpChat.type() == mtpc_channel) {
				auto channel = App::channel(mtpChat.c_channel().vid.v);
				Ui::showPeerHistory(channel, ShowAtUnreadMsgId);
				Auth().api().requestParticipantsCountDelayed(channel);
			}
		}
	};

	switch (updates.type()) {
	case mtpc_updates: handleChats(updates.c_updates().vchats); break;
	case mtpc_updatesCombined: handleChats(updates.c_updatesCombined().vchats); break;
	default: LOG(("API Error: unexpected update cons %1 (ConvertToSupergroupBox::convertDone)").arg(updates.type())); break;
	}
}

bool ConvertToSupergroupBox::convertFail(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;
	Ui::hideLayer();
	return true;
}

void ConvertToSupergroupBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		convertToSupergroup();
	} else {
		BoxContent::keyPressEvent(e);
	}
}

void ConvertToSupergroupBox::paintEvent(QPaintEvent *e) {
	BoxContent::paintEvent(e);

	Painter p(this);

	// draw box title / text
	p.setPen(st::boxTextFg);
	_text.drawLeft(p, st::boxPadding.left(), 0, _textWidth, width());
	_note.drawLeft(p, st::boxPadding.left(), _textHeight + st::boxPadding.bottom(), _textWidth, width());
}

PinMessageBox::PinMessageBox(QWidget*, ChannelData *channel, MsgId msgId)
: _channel(channel)
, _msgId(msgId)
, _text(this, lang(lng_pinned_pin_sure), Ui::FlatLabel::InitType::Simple, st::boxLabel)
, _notify(this, lang(lng_pinned_notify), true, st::defaultBoxCheckbox) {
}

void PinMessageBox::prepare() {
	addButton(langFactory(lng_pinned_pin), [this] { pinMessage(); });
	addButton(langFactory(lng_cancel), [this] { closeBox(); });

	setDimensions(st::boxWidth, st::boxPadding.top() + _text->height() + st::boxMediumSkip + _notify->heightNoMargins() + st::boxPadding.bottom());
}

void PinMessageBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);
	_text->moveToLeft(st::boxPadding.left(), st::boxPadding.top());
	_notify->moveToLeft(st::boxPadding.left(), _text->y() + _text->height() + st::boxMediumSkip);
}

void PinMessageBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		pinMessage();
	} else {
		BoxContent::keyPressEvent(e);
	}
}

void PinMessageBox::pinMessage() {
	if (_requestId) return;

	auto flags = MTPchannels_UpdatePinnedMessage::Flags(0);
	if (!_notify->checked()) {
		flags |= MTPchannels_UpdatePinnedMessage::Flag::f_silent;
	}
	_requestId = MTP::send(MTPchannels_UpdatePinnedMessage(MTP_flags(flags), _channel->inputChannel, MTP_int(_msgId)), rpcDone(&PinMessageBox::pinDone), rpcFail(&PinMessageBox::pinFail));
}

void PinMessageBox::pinDone(const MTPUpdates &updates) {
	if (App::main()) {
		App::main()->sentUpdatesReceived(updates);
	}
	Ui::hideLayer();
}

bool PinMessageBox::pinFail(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;
	Ui::hideLayer();
	return true;
}

DeleteMessagesBox::DeleteMessagesBox(QWidget*, HistoryItem *item, bool suggestModerateActions) : _singleItem(true) {
	_ids.push_back(item->fullId());
	if (suggestModerateActions) {
		_moderateBan = item->suggestBanReport();
		_moderateDeleteAll = item->suggestDeleteAllReport();
		if (_moderateBan || _moderateDeleteAll) {
			_moderateFrom = item->from()->asUser();
			_moderateInChannel = item->history()->peer->asChannel();
		}
	}
}

DeleteMessagesBox::DeleteMessagesBox(QWidget*, const SelectedItemSet &selected) {
	auto count = selected.size();
	Assert(count > 0);
	_ids.reserve(count);
	for_const (auto item, selected) {
		_ids.push_back(item->fullId());
	}
}

void DeleteMessagesBox::prepare() {
	auto text = QString();
	if (_moderateFrom) {
		Assert(_moderateInChannel != nullptr);
		text = lang(lng_selected_delete_sure_this);
		if (_moderateBan) {
			_banUser.create(this, lang(lng_ban_user), false, st::defaultBoxCheckbox);
		}
		_reportSpam.create(this, lang(lng_report_spam), false, st::defaultBoxCheckbox);
		if (_moderateDeleteAll) {
			_deleteAll.create(this, lang(lng_delete_all_from), false, st::defaultBoxCheckbox);
		}
	} else {
		text = _singleItem ? lang(lng_selected_delete_sure_this) : lng_selected_delete_sure(lt_count, _ids.size());
		auto canDeleteAllForEveryone = true;
		auto now = ::date(unixtime());
		auto deleteForUser = (UserData*)nullptr;
		auto peer = (PeerData*)nullptr;
		auto forEveryoneText = lang(lng_delete_for_everyone_check);
		for_const (auto fullId, _ids) {
			if (auto item = App::histItemById(fullId)) {
				peer = item->history()->peer;
				if (!item->canDeleteForEveryone(now)) {
					canDeleteAllForEveryone = false;
					break;
				} else if (auto user = item->history()->peer->asUser()) {
					if (!deleteForUser || deleteForUser == user) {
						deleteForUser = user;
						forEveryoneText = lng_delete_for_other_check(lt_user, user->firstName);
					} else {
						forEveryoneText = lang(lng_delete_for_everyone_check);
					}
				}
			} else {
				canDeleteAllForEveryone = false;
			}
		}
		auto count = qMax(1, _ids.size());
		if (canDeleteAllForEveryone) {
			_forEveryone.create(this, forEveryoneText, false, st::defaultBoxCheckbox);
		} else if (peer && peer->isChannel()) {
			if (peer->isMegagroup()) {
				text += qsl("\n\n") + lng_delete_for_everyone_hint(lt_count, count);
			}
		} else if (peer->isChat()) {
			text += qsl("\n\n") + lng_delete_for_me_chat_hint(lt_count, count);
		} else if (!peer->isSelf()) {
			text += qsl("\n\n") + lng_delete_for_me_hint(lt_count, count);
		}
	}
	_text.create(this, text, Ui::FlatLabel::InitType::Simple, st::boxLabel);

	addButton(langFactory(lng_box_delete), [this] { deleteAndClear(); });
	addButton(langFactory(lng_cancel), [this] { closeBox(); });

	auto fullHeight = st::boxPadding.top() + _text->height() + st::boxPadding.bottom();
	if (_moderateFrom) {
		fullHeight += st::boxMediumSkip;
		if (_banUser) {
			fullHeight += _banUser->heightNoMargins() + st::boxLittleSkip;
		}
		fullHeight += _reportSpam->heightNoMargins();
		if (_deleteAll) {
			fullHeight += st::boxLittleSkip + _deleteAll->heightNoMargins();
		}
	} else if (_forEveryone) {
		fullHeight += st::boxMediumSkip + _forEveryone->heightNoMargins();
	}
	setDimensions(st::boxWidth, fullHeight);
}

void DeleteMessagesBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);
	_text->moveToLeft(st::boxPadding.left(), st::boxPadding.top());
	if (_moderateFrom) {
		auto top = _text->bottomNoMargins() + st::boxMediumSkip;
		if (_banUser) {
			_banUser->moveToLeft(st::boxPadding.left(), top);
			top += _banUser->heightNoMargins() + st::boxLittleSkip;
		}
		_reportSpam->moveToLeft(st::boxPadding.left(), top);
		top += _reportSpam->heightNoMargins() + st::boxLittleSkip;
		if (_deleteAll) {
			_deleteAll->moveToLeft(st::boxPadding.left(), top);
		}
	} else if (_forEveryone) {
		_forEveryone->moveToLeft(st::boxPadding.left(), _text->bottomNoMargins() + st::boxMediumSkip);
	}
}

void DeleteMessagesBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		deleteAndClear();
	} else {
		BoxContent::keyPressEvent(e);
	}
}

void DeleteMessagesBox::deleteAndClear() {
	if (!App::main()) {
		return;
	}

	if (_moderateFrom) {
		if (_banUser && _banUser->checked()) {
			Auth().api().kickParticipant(_moderateInChannel, _moderateFrom, MTP_channelBannedRights(MTP_flags(0), MTP_int(0)));
		}
		if (_reportSpam->checked()) {
			MTP::send(MTPchannels_ReportSpam(_moderateInChannel->inputChannel, _moderateFrom->inputUser, MTP_vector<MTPint>(1, MTP_int(_ids[0].msg))));
		}
		if (_deleteAll && _deleteAll->checked()) {
			App::main()->deleteAllFromUser(_moderateInChannel, _moderateFrom);
		}
	}

	if (!_singleItem) {
		App::main()->clearSelectedItems();
	}

	QMap<PeerData*, QVector<MTPint>> idsByPeer;
	for_const (auto fullId, _ids) {
		if (auto item = App::histItemById(fullId)) {
			auto history = item->history();
			auto wasOnServer = (item->id > 0);
			auto wasLast = (history->lastMsg == item);
			item->destroy();

			if (wasOnServer) {
				idsByPeer[history->peer].push_back(MTP_int(fullId.msg));
			} else if (wasLast) {
				App::main()->checkPeerHistory(history->peer);
			}
		}
	}

	auto forEveryone = _forEveryone ? _forEveryone->checked() : false;
	for (auto i = idsByPeer.cbegin(), e = idsByPeer.cend(); i != e; ++i) {
		App::main()->deleteMessages(i.key(), i.value(), forEveryone);
	}
	Ui::hideLayer();
}

ConfirmInviteBox::ConfirmInviteBox(QWidget*, const QString &title, bool isChannel, const MTPChatPhoto &photo, int count, const QVector<UserData*> &participants)
: _title(this, st::confirmInviteTitle)
, _status(this, st::confirmInviteStatus)
, _participants(participants) {
	_title->setText(title);
	QString status;
	if (_participants.isEmpty() || _participants.size() >= count) {
		if (count > 0) {
			status = lng_chat_status_members(lt_count, count);
		} else {
			status = lang(isChannel ? lng_channel_status : lng_group_status);
		}
	} else {
		status = lng_group_invite_members(lt_count, count);
	}
	_status->setText(status);
	if (photo.type() == mtpc_chatPhoto) {
		auto &d = photo.c_chatPhoto();
		auto location = App::imageLocation(160, 160, d.vphoto_small);
		if (!location.isNull()) {
			_photo = ImagePtr(location);
			if (!_photo->loaded()) {
				subscribe(Auth().downloaderTaskFinished(), [this] { update(); });
				_photo->load();
			}
		}
	}
	if (!_photo) {
		_photoEmpty.set(0, title);
	}
}

void ConfirmInviteBox::prepare() {
	addButton(langFactory(lng_group_invite_join), [this] {
		if (auto main = App::main()) {
			main->onInviteImport();
		}
	});
	addButton(langFactory(lng_cancel), [this] { closeBox(); });

	if (_participants.size() > 4) {
		_participants.resize(4);
	}

	auto newHeight = st::confirmInviteStatusTop + _status->height() + st::boxPadding.bottom();
	if (!_participants.isEmpty()) {
		int skip = (st::boxWideWidth - 4 * st::confirmInviteUserPhotoSize) / 5;
		int padding = skip / 2;
		_userWidth = (st::confirmInviteUserPhotoSize + 2 * padding);
		int sumWidth = _participants.size() * _userWidth;
		int left = (st::boxWideWidth - sumWidth) / 2;
		for_const (auto user, _participants) {
			auto name = new Ui::FlatLabel(this, st::confirmInviteUserName);
			name->resizeToWidth(st::confirmInviteUserPhotoSize + padding);
			name->setText(user->firstName.isEmpty() ? App::peerName(user) : user->firstName);
			name->moveToLeft(left + (padding / 2), st::confirmInviteUserNameTop);
			left += _userWidth;
		}

		newHeight += st::confirmInviteUserHeight;
	}
	setDimensions(st::boxWideWidth, newHeight);
}

void ConfirmInviteBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);
	_title->move((width() - _title->width()) / 2, st::confirmInviteTitleTop);
	_status->move((width() - _status->width()) / 2, st::confirmInviteStatusTop);
}

void ConfirmInviteBox::paintEvent(QPaintEvent *e) {
	BoxContent::paintEvent(e);

	Painter p(this);

	if (_photo) {
		p.drawPixmap((width() - st::confirmInvitePhotoSize) / 2, st::confirmInvitePhotoTop, _photo->pixCircled(st::confirmInvitePhotoSize, st::confirmInvitePhotoSize));
	} else {
		_photoEmpty.paint(p, (width() - st::confirmInvitePhotoSize) / 2, st::confirmInvitePhotoTop, width(), st::confirmInvitePhotoSize);
	}

	int sumWidth = _participants.size() * _userWidth;
	int left = (width() - sumWidth) / 2;
	for_const (auto user, _participants) {
		user->paintUserpicLeft(p, left + (_userWidth - st::confirmInviteUserPhotoSize) / 2, st::confirmInviteUserPhotoTop, width(), st::confirmInviteUserPhotoSize);
		left += _userWidth;
	}
}
