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
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "lang.h"

#include "confirmbox.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "apiwrap.h"
#include "application.h"
#include "core/click_handler_types.h"

TextParseOptions _confirmBoxTextOptions = {
	TextParseLinks | TextParseMultiline | TextParseRichText, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};

ConfirmBox::ConfirmBox(const QString &text, const QString &doneText, const style::BoxButton &doneStyle, const QString &cancelText, const style::BoxButton &cancelStyle) : AbstractBox(st::boxWidth)
, _informative(false)
, _text(100)
, _confirm(this, doneText.isEmpty() ? lang(lng_box_ok) : doneText, doneStyle)
, _cancel(this, cancelText.isEmpty() ? lang(lng_cancel) : cancelText, cancelStyle) {
	init(text);
}

ConfirmBox::ConfirmBox(const QString &text, const QString &doneText, const style::BoxButton &doneStyle, bool informative) : AbstractBox(st::boxWidth)
, _informative(true)
, _text(100)
, _confirm(this, doneText.isEmpty() ? lang(lng_box_ok) : doneText, doneStyle)
, _cancel(this, QString(), st::cancelBoxButton) {
	init(text);
}

void ConfirmBox::init(const QString &text) {
	_text.setText(st::boxTextFont, text, _informative ? _confirmBoxTextOptions : _textPlainOptions);

	textstyleSet(&st::boxTextStyle);
	_textWidth = st::boxWidth - st::boxPadding.left() - st::boxButtonPadding.right();
	_textHeight = qMin(_text.countHeight(_textWidth), 16 * int(st::boxTextStyle.lineHeight));
	setMaxHeight(st::boxPadding.top() + _textHeight + st::boxPadding.bottom() + st::boxButtonPadding.top() + _confirm.height() + st::boxButtonPadding.bottom());
	textstyleRestore();

	connect(&_confirm, SIGNAL(clicked()), this, SIGNAL(confirmed()));
	connect(&_cancel, SIGNAL(clicked()), this, SLOT(onCancel()));
	if (_informative) {
		_cancel.hide();
		connect(this, SIGNAL(confirmed()), this, SLOT(onCancel()));
	}
	setMouseTracking(_text.hasLinks());

	prepare();
}

void ConfirmBox::onCancel() {
	emit cancelPressed();
	onClose();
}

void ConfirmBox::mouseMoveEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	updateHover();
}

void ConfirmBox::mousePressEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	updateHover();
	ClickHandler::pressed();
	return LayeredWidget::mousePressEvent(e);
}

void ConfirmBox::mouseReleaseEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	updateHover();
	if (ClickHandlerPtr activated = ClickHandler::unpressed()) {
		Ui::hideLayer();
		App::activateClickHandler(activated, e->button());
	}
}

void ConfirmBox::leaveEvent(QEvent *e) {
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
	QPoint m(mapFromGlobal(_lastMousePos));

	textstyleSet(&st::boxTextStyle);
	auto state = _text.getStateLeft(m.x() - st::boxPadding.left(), m.y() - st::boxPadding.top(), _textWidth, width());
	textstyleRestore();

	ClickHandler::setActive(state.link, this);
}

void ConfirmBox::closePressed() {
	emit cancelled();
}

void ConfirmBox::hideAll() {
	_confirm.hide();
	_cancel.hide();
}

void ConfirmBox::showAll() {
	if (_informative) {
		_confirm.show();
	} else {
		_confirm.show();
		_cancel.show();
	}
}

void ConfirmBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		emit confirmed();
	} else {
		AbstractBox::keyPressEvent(e);
	}
}

void ConfirmBox::paintEvent(QPaintEvent *e) {
	QPainter p(this);
	if (paint(p)) return;

	// draw box title / text
	p.setPen(st::black->p);
	textstyleSet(&st::boxTextStyle);
	_text.drawLeftElided(p, st::boxPadding.left(), st::boxPadding.top(), _textWidth, width(), 16, style::al_left);
	textstyleRestore();
}

void ConfirmBox::resizeEvent(QResizeEvent *e) {
	_confirm.moveToRight(st::boxButtonPadding.right(), height() - st::boxButtonPadding.bottom() - _confirm.height());
	_cancel.moveToRight(st::boxButtonPadding.right() + _confirm.width() + st::boxButtonPadding.left(), _confirm.y());
}

SharePhoneConfirmBox::SharePhoneConfirmBox(PeerData *recipient)
: ConfirmBox(lang(lng_bot_share_phone), lang(lng_bot_share_phone_confirm))
, _recipient(recipient) {
	connect(this, SIGNAL(confirmed()), this, SLOT(onConfirm()));
}

void SharePhoneConfirmBox::onConfirm() {
	emit confirmed(_recipient);
}

ConfirmLinkBox::ConfirmLinkBox(const QString &url) : ConfirmBox(lang(lng_open_this_link) + qsl("\n\n") + url, lang(lng_open_link))
, _url(url) {
	connect(this, SIGNAL(confirmed()), this, SLOT(onOpenLink()));
}

void ConfirmLinkBox::onOpenLink() {
	Ui::hideLayer();
	UrlClickHandler::doOpen(_url);
}

MaxInviteBox::MaxInviteBox(const QString &link) : AbstractBox(st::boxWidth)
, _close(this, lang(lng_box_ok), st::defaultBoxButton)
, _text(st::boxTextFont, lng_participant_invite_sorry(lt_count, Global::ChatSizeMax()), _confirmBoxTextOptions, st::boxWidth - st::boxPadding.left() - st::boxButtonPadding.right())
, _link(link)
, _linkOver(false)
, a_goodOpacity(0, 0)
, _a_good(animation(this, &MaxInviteBox::step_good)) {
	setMouseTracking(true);

	_textWidth = st::boxWidth - st::boxPadding.left() - st::boxButtonPadding.right();
	_textHeight = qMin(_text.countHeight(_textWidth), 16 * int(st::boxTextStyle.lineHeight));
	setMaxHeight(st::boxPadding.top() + _textHeight + st::boxTextFont->height + st::boxTextFont->height * 2 + st::newGroupLinkPadding.bottom() + st::boxButtonPadding.top() + _close.height() + st::boxButtonPadding.bottom());

	connect(&_close, SIGNAL(clicked()), this, SLOT(onClose()));

	prepare();
}

void MaxInviteBox::mouseMoveEvent(QMouseEvent *e) {
	updateSelected(e->globalPos());
}

void MaxInviteBox::mousePressEvent(QMouseEvent *e) {
	mouseMoveEvent(e);
	if (_linkOver) {
		Application::clipboard()->setText(_link);
		_goodTextLink = lang(lng_create_channel_link_copied);
		a_goodOpacity = anim::fvalue(1, 0);
		_a_good.start();
	}
}

void MaxInviteBox::leaveEvent(QEvent *e) {
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

void MaxInviteBox::step_good(float64 ms, bool timer) {
	float dt = ms / st::newGroupLinkFadeDuration;
	if (dt >= 1) {
		_a_good.stop();
		a_goodOpacity.finish();
	} else {
		a_goodOpacity.update(dt, anim::linear);
	}
	if (timer) update();
}

void MaxInviteBox::hideAll() {
	_close.hide();
}

void MaxInviteBox::showAll() {
	_close.show();
}

void MaxInviteBox::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (paint(p)) return;

	// draw box title / text
	p.setPen(st::black->p);
	_text.drawLeftElided(p, st::boxPadding.left(), st::boxPadding.top(), _textWidth, width(), 16, style::al_left);

	QTextOption option(style::al_left);
	option.setWrapMode(QTextOption::WrapAnywhere);
	p.setFont(_linkOver ? st::defaultInputField.font->underline() : st::defaultInputField.font);
	p.setPen(st::btnDefLink.color);
	p.drawText(_invitationLink, _link, option);
	if (!_goodTextLink.isEmpty() && a_goodOpacity.current() > 0) {
		p.setOpacity(a_goodOpacity.current());
		p.setPen(st::setGoodColor->p);
		p.setFont(st::boxTextFont->f);
		p.drawTextLeft(st::boxPadding.left(), height() - st::boxButtonPadding.bottom() - _close.height() + st::defaultBoxButton.textTop + st::defaultBoxButton.font->ascent - st::boxTextFont->ascent, width(), _goodTextLink);
		p.setOpacity(1);
	}
}

void MaxInviteBox::resizeEvent(QResizeEvent *e) {
	_close.moveToRight(st::boxButtonPadding.right(), height() - st::boxButtonPadding.bottom() - _close.height());
	_invitationLink = myrtlrect(st::boxPadding.left(), st::boxPadding.top() + _textHeight + st::boxTextFont->height, width() - st::boxPadding.left() - st::boxPadding.right(), 2 * st::boxTextFont->height);
}

ConvertToSupergroupBox::ConvertToSupergroupBox(ChatData *chat) : AbstractBox(st::boxWideWidth)
, _chat(chat)
, _text(100)
, _note(100)
, _convert(this, lang(lng_profile_convert_confirm), st::defaultBoxButton)
, _cancel(this, lang(lng_cancel), st::cancelBoxButton) {
	QStringList text;
	text.push_back(lang(lng_profile_convert_feature1));
	text.push_back(lang(lng_profile_convert_feature2));
	text.push_back(lang(lng_profile_convert_feature3));
	text.push_back(lang(lng_profile_convert_feature4));

	textstyleSet(&st::boxTextStyle);
	_text.setText(st::boxTextFont, text.join('\n'), _confirmBoxTextOptions);
	_note.setText(st::boxTextFont, lng_profile_convert_warning(lt_bold_start, textcmdStartSemibold(), lt_bold_end, textcmdStopSemibold()), _confirmBoxTextOptions);
	_textWidth = st::boxWideWidth - st::boxPadding.left() - st::boxButtonPadding.right();
	_textHeight = _text.countHeight(_textWidth);
	setMaxHeight(st::boxTitleHeight + _textHeight + st::boxPadding.bottom() + _note.countHeight(_textWidth) + st::boxButtonPadding.top() + _convert.height() + st::boxButtonPadding.bottom());
	textstyleRestore();

	connect(&_convert, SIGNAL(clicked()), this, SLOT(onConvert()));
	connect(&_cancel, SIGNAL(clicked()), this, SLOT(onClose()));

	prepare();
}

void ConvertToSupergroupBox::onConvert() {
	MTP::send(MTPmessages_MigrateChat(_chat->inputChat), rpcDone(&ConvertToSupergroupBox::convertDone), rpcFail(&ConvertToSupergroupBox::convertFail));
}

void ConvertToSupergroupBox::convertDone(const MTPUpdates &updates) {
	Ui::hideLayer();
	App::main()->sentUpdatesReceived(updates);
	const QVector<MTPChat> *v = 0;
	switch (updates.type()) {
	case mtpc_updates: v = &updates.c_updates().vchats.c_vector().v; break;
	case mtpc_updatesCombined: v = &updates.c_updatesCombined().vchats.c_vector().v; break;
	default: LOG(("API Error: unexpected update cons %1 (ConvertToSupergroupBox::convertDone)").arg(updates.type())); break;
	}

	PeerData *peer = 0;
	if (v && !v->isEmpty()) {
		for (int32 i = 0, l = v->size(); i < l; ++i) {
			if (v->at(i).type() == mtpc_channel) {
				peer = App::channel(v->at(i).c_channel().vid.v);
				Ui::showPeerHistory(peer, ShowAtUnreadMsgId);
				QTimer::singleShot(ReloadChannelMembersTimeout, App::api(), SLOT(delayedRequestParticipantsCount()));
			}
		}
	}
	if (!peer) {
		LOG(("API Error: channel not found in updates (ProfileInner::migrateDone)"));
	}
}

bool ConvertToSupergroupBox::convertFail(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;
	Ui::hideLayer();
	return true;
}

void ConvertToSupergroupBox::hideAll() {
	_convert.hide();
	_cancel.hide();
}

void ConvertToSupergroupBox::showAll() {
	_convert.show();
	_cancel.show();
}

void ConvertToSupergroupBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		onConvert();
	} else {
		AbstractBox::keyPressEvent(e);
	}
}

void ConvertToSupergroupBox::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (paint(p)) return;

	paintTitle(p, lang(lng_profile_convert_title));

	// draw box title / text
	p.setPen(st::black);
	textstyleSet(&st::boxTextStyle);
	_text.drawLeft(p, st::boxPadding.left(), st::boxTitleHeight, _textWidth, width());
	_note.drawLeft(p, st::boxPadding.left(), st::boxTitleHeight + _textHeight + st::boxPadding.bottom(), _textWidth, width());
	textstyleRestore();
}

void ConvertToSupergroupBox::resizeEvent(QResizeEvent *e) {
	_convert.moveToRight(st::boxButtonPadding.right(), height() - st::boxButtonPadding.bottom() - _convert.height());
	_cancel.moveToRight(st::boxButtonPadding.right() + _convert.width() + st::boxButtonPadding.left(), _convert.y());
}

PinMessageBox::PinMessageBox(ChannelData *channel, MsgId msgId) : AbstractBox(st::boxWidth)
, _channel(channel)
, _msgId(msgId)
, _text(this, lang(lng_pinned_pin_sure), st::boxLabel)
, _notify(this, lang(lng_pinned_notify), true)
, _pin(this, lang(lng_pinned_pin), st::defaultBoxButton)
, _cancel(this, lang(lng_cancel), st::cancelBoxButton)
, _requestId(0) {
	_text.resizeToWidth(st::boxWidth - st::boxPadding.left() - st::boxButtonPadding.right());
	setMaxHeight(st::boxPadding.top() + _text.height() + st::boxMediumSkip + _notify.height() + st::boxPadding.bottom() + st::boxButtonPadding.top() + _pin.height() + st::boxButtonPadding.bottom());

	connect(&_pin, SIGNAL(clicked()), this, SLOT(onPin()));
	connect(&_cancel, SIGNAL(clicked()), this, SLOT(onClose()));
}

void PinMessageBox::resizeEvent(QResizeEvent *e) {
	_text.moveToLeft(st::boxPadding.left(), st::boxPadding.top());
	_notify.moveToLeft(st::boxPadding.left(), _text.y() + _text.height() + st::boxMediumSkip);
	_pin.moveToRight(st::boxButtonPadding.right(), height() - st::boxButtonPadding.bottom() - _pin.height());
	_cancel.moveToRight(st::boxButtonPadding.right() + _pin.width() + st::boxButtonPadding.left(), _pin.y());
}

void PinMessageBox::onPin() {
	if (_requestId) return;

	MTPchannels_UpdatePinnedMessage::Flags flags = 0;
	if (!_notify.checked()) {
		flags |= MTPchannels_UpdatePinnedMessage::Flag::f_silent;
	}
	_requestId = MTP::send(MTPchannels_UpdatePinnedMessage(MTP_flags(flags), _channel->inputChannel, MTP_int(_msgId)), rpcDone(&PinMessageBox::pinDone), rpcFail(&PinMessageBox::pinFail));
}

void PinMessageBox::showAll() {
	_text.show();
	_notify.show();
	_pin.show();
	_cancel.show();
}

void PinMessageBox::hideAll() {
	_text.hide();
	_notify.hide();
	_pin.hide();
	_cancel.hide();
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

RichDeleteMessageBox::RichDeleteMessageBox(ChannelData *channel, UserData *from, MsgId msgId) : AbstractBox(st::boxWidth)
, _channel(channel)
, _from(from)
, _msgId(msgId)
, _text(this, lang(lng_selected_delete_sure_this), st::boxLabel)
, _banUser(this, lang(lng_ban_user), false)
, _reportSpam(this, lang(lng_report_spam), false)
, _deleteAll(this, lang(lng_delete_all_from), false)
, _delete(this, lang(lng_box_delete), st::defaultBoxButton)
, _cancel(this, lang(lng_cancel), st::cancelBoxButton) {
	t_assert(_channel != nullptr);

	_text.resizeToWidth(st::boxWidth - st::boxPadding.left() - st::boxButtonPadding.right());
	setMaxHeight(st::boxPadding.top() + _text.height() + st::boxMediumSkip + _banUser.height() + st::boxLittleSkip + _reportSpam.height() + st::boxLittleSkip + _deleteAll.height() + st::boxPadding.bottom() + st::boxButtonPadding.top() + _delete.height() + st::boxButtonPadding.bottom());

	connect(&_delete, SIGNAL(clicked()), this, SLOT(onDelete()));
	connect(&_cancel, SIGNAL(clicked()), this, SLOT(onClose()));
}

void RichDeleteMessageBox::resizeEvent(QResizeEvent *e) {
	_text.moveToLeft(st::boxPadding.left(), st::boxPadding.top());
	_banUser.moveToLeft(st::boxPadding.left(), _text.y() + _text.height() + st::boxMediumSkip);
	_reportSpam.moveToLeft(st::boxPadding.left(), _banUser.y() + _banUser.height() + st::boxLittleSkip);
	_deleteAll.moveToLeft(st::boxPadding.left(), _reportSpam.y() + _reportSpam.height() + st::boxLittleSkip);
	_delete.moveToRight(st::boxButtonPadding.right(), height() - st::boxButtonPadding.bottom() - _delete.height());
	_cancel.moveToRight(st::boxButtonPadding.right() + _delete.width() + st::boxButtonPadding.left(), _delete.y());
}

void RichDeleteMessageBox::onDelete() {
	if (_banUser.checked()) {
		MTP::send(MTPchannels_KickFromChannel(_channel->inputChannel, _from->inputUser, MTP_boolTrue()), App::main()->rpcDone(&MainWidget::sentUpdatesReceived));
	}
	if (_reportSpam.checked()) {
		MTP::send(MTPchannels_ReportSpam(_channel->inputChannel, _from->inputUser, MTP_vector<MTPint>(1, MTP_int(_msgId))));
	}
	if (_deleteAll.checked()) {
		App::main()->deleteAllFromUser(_channel, _from);
	}
	if (HistoryItem *item = App::histItemById(_channel ? peerToChannel(_channel->id) : 0, _msgId)) {
		bool wasLast = (item->history()->lastMsg == item);
		item->destroy();
		if (_msgId > 0) {
			App::main()->deleteMessages(_channel, QVector<MTPint>(1, MTP_int(_msgId)));
		} else if (wasLast) {
			App::main()->checkPeerHistory(_channel);
		}
	}
	Ui::hideLayer();
}

void RichDeleteMessageBox::showAll() {
	_text.show();
	_banUser.show();
	_reportSpam.show();
	_deleteAll.show();
	_delete.show();
	_cancel.show();
}

void RichDeleteMessageBox::hideAll() {
	_text.hide();
	_banUser.hide();
	_reportSpam.hide();
	_deleteAll.hide();
	_delete.hide();
	_cancel.hide();
}
