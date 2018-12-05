/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/confirm_box.h"

#include "styles/style_boxes.h"
#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "apiwrap.h"
#include "application.h"
#include "history/history.h"
#include "history/history_item.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/toast/toast.h"
#include "ui/image/image.h"
#include "ui/empty_userpic.h"
#include "core/click_handler_types.h"
#include "storage/localstorage.h"
#include "data/data_session.h"
#include "auth_session.h"
#include "observer_peer.h"

namespace {

void ConvertToSupergroupDone(const MTPUpdates &updates) {
	Auth().api().applyUpdates(updates);

	auto handleChats = [](const MTPVector<MTPChat> &chats) {
		for (const auto &chat : chats.v) {
			if (chat.type() == mtpc_channel) {
				const auto channel = App::channel(chat.c_channel().vid.v);
				Ui::showPeerHistory(channel, ShowAtUnreadMsgId);
				Auth().api().requestParticipantsCountDelayed(channel);
			}
		}
	};

	switch (updates.type()) {
	case mtpc_updates:
		handleChats(updates.c_updates().vchats);
		break;
	case mtpc_updatesCombined:
		handleChats(updates.c_updatesCombined().vchats);
		break;
	default:
		LOG(("API Error: unexpected update cons %1 "
			"(ConvertToSupergroupBox::convertDone)").arg(updates.type()));
		break;
	}
}

} // namespace

TextParseOptions _confirmBoxTextOptions = {
	TextParseLinks | TextParseMultiline | TextParseRichText, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};

ConfirmBox::ConfirmBox(QWidget*, const QString &text, FnMut<void()> confirmedCallback, FnMut<void()> cancelledCallback)
: _confirmText(lang(lng_box_ok))
, _cancelText(lang(lng_cancel))
, _confirmStyle(st::defaultBoxButton)
, _text(st::boxWidth - st::boxPadding.left() - st::boxButtonPadding.right())
, _confirmedCallback(std::move(confirmedCallback))
, _cancelledCallback(std::move(cancelledCallback)) {
	init(text);
}

ConfirmBox::ConfirmBox(QWidget*, const QString &text, const QString &confirmText, FnMut<void()> confirmedCallback, FnMut<void()> cancelledCallback)
: _confirmText(confirmText)
, _cancelText(lang(lng_cancel))
, _confirmStyle(st::defaultBoxButton)
, _text(st::boxWidth - st::boxPadding.left() - st::boxButtonPadding.right())
, _confirmedCallback(std::move(confirmedCallback))
, _cancelledCallback(std::move(cancelledCallback)) {
	init(text);
}

ConfirmBox::ConfirmBox(QWidget*, const TextWithEntities &text, const QString &confirmText, FnMut<void()> confirmedCallback, FnMut<void()> cancelledCallback)
: _confirmText(confirmText)
, _cancelText(lang(lng_cancel))
, _confirmStyle(st::defaultBoxButton)
, _text(st::boxWidth - st::boxPadding.left() - st::boxButtonPadding.right())
, _confirmedCallback(std::move(confirmedCallback))
, _cancelledCallback(std::move(cancelledCallback)) {
	init(text);
}

ConfirmBox::ConfirmBox(QWidget*, const QString &text, const QString &confirmText, const style::RoundButton &confirmStyle, FnMut<void()> confirmedCallback, FnMut<void()> cancelledCallback)
: _confirmText(confirmText)
, _cancelText(lang(lng_cancel))
, _confirmStyle(confirmStyle)
, _text(st::boxWidth - st::boxPadding.left() - st::boxButtonPadding.right())
, _confirmedCallback(std::move(confirmedCallback))
, _cancelledCallback(std::move(cancelledCallback)) {
	init(text);
}

ConfirmBox::ConfirmBox(QWidget*, const QString &text, const QString &confirmText, const QString &cancelText, FnMut<void()> confirmedCallback, FnMut<void()> cancelledCallback)
: _confirmText(confirmText)
, _cancelText(cancelText)
, _confirmStyle(st::defaultBoxButton)
, _text(st::boxWidth - st::boxPadding.left() - st::boxButtonPadding.right())
, _confirmedCallback(std::move(confirmedCallback))
, _cancelledCallback(std::move(cancelledCallback)) {
	init(text);
}

ConfirmBox::ConfirmBox(QWidget*, const QString &text, const QString &confirmText, const style::RoundButton &confirmStyle, const QString &cancelText, FnMut<void()> confirmedCallback, FnMut<void()> cancelledCallback)
: _confirmText(confirmText)
, _cancelText(cancelText)
, _confirmStyle(st::defaultBoxButton)
, _text(st::boxWidth - st::boxPadding.left() - st::boxButtonPadding.right())
, _confirmedCallback(std::move(confirmedCallback))
, _cancelledCallback(std::move(cancelledCallback)) {
	init(text);
}

ConfirmBox::ConfirmBox(const InformBoxTag &, const QString &text, const QString &doneText, Fn<void()> closedCallback)
: _confirmText(doneText)
, _confirmStyle(st::defaultBoxButton)
, _informative(true)
, _text(st::boxWidth - st::boxPadding.left() - st::boxButtonPadding.right())
, _confirmedCallback(generateInformCallback(closedCallback))
, _cancelledCallback(generateInformCallback(closedCallback)) {
	init(text);
}

ConfirmBox::ConfirmBox(const InformBoxTag &, const TextWithEntities &text, const QString &doneText, Fn<void()> closedCallback)
: _confirmText(doneText)
, _confirmStyle(st::defaultBoxButton)
, _informative(true)
, _text(st::boxWidth - st::boxPadding.left() - st::boxButtonPadding.right())
, _confirmedCallback(generateInformCallback(closedCallback))
, _cancelledCallback(generateInformCallback(closedCallback)) {
	init(text);
}

FnMut<void()> ConfirmBox::generateInformCallback(Fn<void()> closedCallback) {
	return crl::guard(this, [this, closedCallback] {
		closeBox();
		if (closedCallback) {
			closedCallback();
		}
	});
}

void ConfirmBox::init(const QString &text) {
	_text.setText(st::boxLabelStyle, text, _informative ? _confirmBoxTextOptions : _textPlainOptions);
}

void ConfirmBox::init(const TextWithEntities &text) {
	_text.setMarkedText(st::boxLabelStyle, text, _confirmBoxTextOptions);
}

void ConfirmBox::prepare() {
	addButton([this] { return _confirmText; }, [this] { confirmed(); }, _confirmStyle);
	if (!_informative) {
		addButton([this] { return _cancelText; }, [this] { _cancelled = true; closeBox(); });
	}

	boxClosing() | rpl::start_with_next([=] {
		if (!_confirmed && (!_strictCancel || _cancelled)) {
			if (auto callback = std::move(_cancelledCallback)) {
				callback();
			}
		}
	}, lifetime());

	textUpdated();
}

void ConfirmBox::setMaxLineCount(int count) {
	if (_maxLineCount != count) {
		_maxLineCount = count;
		textUpdated();
	}
}

void ConfirmBox::textUpdated() {
	_textWidth = st::boxWidth - st::boxPadding.left() - st::boxButtonPadding.right();
	_textHeight = _text.countHeight(_textWidth);
	if (_maxLineCount > 0) {
		accumulate_min(_textHeight, _maxLineCount * st::boxLabelStyle.lineHeight);
	}
	setDimensions(st::boxWidth, st::boxPadding.top() + _textHeight + st::boxPadding.bottom());

	setMouseTracking(_text.hasLinks());
}

void ConfirmBox::confirmed() {
	if (!_confirmed) {
		_confirmed = true;
		if (auto callback = std::move(_confirmedCallback)) {
			callback();
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
	if (const auto activated = ClickHandler::unpressed()) {
		Ui::hideLayer();
		App::activateClickHandler(activated, e->button());
		return;
	}
	BoxContent::mouseReleaseEvent(e);
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
	if (_maxLineCount > 0) {
		_text.drawLeftElided(p, st::boxPadding.left(), st::boxPadding.top(), _textWidth, width(), _maxLineCount, style::al_left);
	} else {
		_text.drawLeft(p, st::boxPadding.left(), st::boxPadding.top(), _textWidth, width(), style::al_left);
	}
}

InformBox::InformBox(QWidget*, const QString &text, Fn<void()> closedCallback) : ConfirmBox(ConfirmBox::InformBoxTag(), text, lang(lng_box_ok), std::move(closedCallback)) {
}

InformBox::InformBox(QWidget*, const QString &text, const QString &doneText, Fn<void()> closedCallback) : ConfirmBox(ConfirmBox::InformBoxTag(), text, doneText, std::move(closedCallback)) {
}

InformBox::InformBox(QWidget*, const TextWithEntities &text, Fn<void()> closedCallback) : ConfirmBox(ConfirmBox::InformBoxTag(), text, lang(lng_box_ok), std::move(closedCallback)) {
}

InformBox::InformBox(QWidget*, const TextWithEntities &text, const QString &doneText, Fn<void()> closedCallback) : ConfirmBox(ConfirmBox::InformBoxTag(), text, doneText, std::move(closedCallback)) {
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
	ConvertToSupergroupDone(updates);
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

PinMessageBox::PinMessageBox(
	QWidget*,
	not_null<PeerData*> peer,
	MsgId msgId)
: _peer(peer)
, _msgId(msgId)
, _text(this, lang(lng_pinned_pin_sure), Ui::FlatLabel::InitType::Simple, st::boxLabel) {
}

void PinMessageBox::prepare() {
	addButton(langFactory(lng_pinned_pin), [this] { pinMessage(); });
	addButton(langFactory(lng_cancel), [this] { closeBox(); });

	if (_peer->isChat() || _peer->isMegagroup()) {
		_notify.create(this, lang(lng_pinned_notify), true, st::defaultBoxCheckbox);
	}

	auto height = st::boxPadding.top() + _text->height() + st::boxPadding.bottom();
	if (_notify) {
		height += st::boxMediumSkip + _notify->heightNoMargins();
	}
	setDimensions(st::boxWidth, height);
}

void PinMessageBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);
	_text->moveToLeft(st::boxPadding.left(), st::boxPadding.top());
	if (_notify) {
		_notify->moveToLeft(st::boxPadding.left(), _text->y() + _text->height() + st::boxMediumSkip);
	}
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

	auto flags = MTPmessages_UpdatePinnedMessage::Flags(0);
	if (_notify && !_notify->checked()) {
		flags |= MTPmessages_UpdatePinnedMessage::Flag::f_silent;
	}
	_requestId = MTP::send(
		MTPmessages_UpdatePinnedMessage(
			MTP_flags(flags),
			_peer->input,
			MTP_int(_msgId)),
		rpcDone(&PinMessageBox::pinDone),
		rpcFail(&PinMessageBox::pinFail));
}

void PinMessageBox::pinDone(const MTPUpdates &updates) {
	Auth().api().applyUpdates(updates);
	Ui::hideLayer();
}

bool PinMessageBox::pinFail(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;
	Ui::hideLayer();
	return true;
}

DeleteMessagesBox::DeleteMessagesBox(
	QWidget*,
	not_null<HistoryItem*> item,
	bool suggestModerateActions)
: _ids(1, item->fullId())
, _singleItem(true) {
	if (suggestModerateActions) {
		_moderateBan = item->suggestBanReport();
		_moderateDeleteAll = item->suggestDeleteAllReport();
		if (_moderateBan || _moderateDeleteAll) {
			_moderateFrom = item->from()->asUser();
			_moderateInChannel = item->history()->peer->asChannel();
		}
	}
}

DeleteMessagesBox::DeleteMessagesBox(
	QWidget*,
	MessageIdsList &&selected)
: _ids(std::move(selected)) {
	Expects(!_ids.empty());
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
		auto now = unixtime();
		auto deleteForUser = (UserData*)nullptr;
		auto peer = (PeerData*)nullptr;
		auto forEveryoneText = lang(lng_delete_for_everyone_check);
		for (const auto fullId : std::as_const(_ids)) {
			if (const auto item = App::histItemById(fullId)) {
				peer = item->history()->peer;
				if (!item->canDeleteForEveryone(now)) {
					canDeleteAllForEveryone = false;
					break;
				} else if (auto user = item->history()->peer->asUser()) {
					if (!deleteForUser || deleteForUser == user) {
						deleteForUser = user;
						forEveryoneText = lng_delete_for_other_check(
							lt_user,
							user->firstName);
					} else {
						forEveryoneText = lang(lng_delete_for_everyone_check);
					}
				}
			} else {
				canDeleteAllForEveryone = false;
			}
		}
		auto count = int(_ids.size());
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
		auto availableWidth = width() - 2 * st::boxPadding.left();
		_forEveryone->resizeToNaturalWidth(availableWidth);
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
	if (_moderateFrom) {
		if (_banUser && _banUser->checked()) {
			Auth().api().kickParticipant(
				_moderateInChannel,
				_moderateFrom,
				MTP_channelBannedRights(MTP_flags(0), MTP_int(0)));
		}
		if (_reportSpam->checked()) {
			MTP::send(
				MTPchannels_ReportSpam(
					_moderateInChannel->inputChannel,
					_moderateFrom->inputUser,
					MTP_vector<MTPint>(1, MTP_int(_ids[0].msg))));
		}
		if (_deleteAll && _deleteAll->checked()) {
			Auth().api().deleteAllFromUser(
				_moderateInChannel,
				_moderateFrom);
		}
	}

	if (_deleteConfirmedCallback) {
		_deleteConfirmedCallback();
	}

	QMap<PeerData*, QVector<MTPint>> idsByPeer;
	for (const auto itemId : _ids) {
		if (auto item = App::histItemById(itemId)) {
			auto history = item->history();
			auto wasOnServer = (item->id > 0);
			auto wasLast = (history->lastMessage() == item);
			item->destroy();

			if (wasOnServer) {
				idsByPeer[history->peer].push_back(MTP_int(itemId.msg));
			} else if (wasLast && !history->lastMessageKnown()) {
				Auth().api().requestDialogEntry(history);
			}
		}
	}

	auto forEveryone = _forEveryone ? _forEveryone->checked() : false;
	for (auto i = idsByPeer.cbegin(), e = idsByPeer.cend(); i != e; ++i) {
		App::main()->deleteMessages(i.key(), i.value(), forEveryone);
	}
	Ui::hideLayer();
	Auth().data().sendHistoryChangeNotifications();
}

ConfirmInviteBox::ConfirmInviteBox(
	QWidget*,
	const MTPDchatInvite &data,
	Fn<void()> submit)
: _submit(std::move(submit))
, _title(this, st::confirmInviteTitle)
, _status(this, st::confirmInviteStatus)
, _participants(GetParticipants(data))
, _isChannel(data.is_channel() && !data.is_megagroup()) {
	const auto title = qs(data.vtitle);
	const auto count = data.vparticipants_count.v;
	const auto status = [&] {
		if (_participants.empty() || _participants.size() >= count) {
			if (count > 0) {
				return lng_chat_status_members(lt_count, count);
			} else {
				return lang(_isChannel
					? lng_channel_status
					: lng_group_status);
			}
		} else {
			return lng_group_invite_members(lt_count, count);
		}
	}();
	_title->setText(title);
	_status->setText(status);
	if (data.vphoto.type() == mtpc_chatPhoto) {
		const auto &photo = data.vphoto.c_chatPhoto();
		const auto size = 160;
		const auto location = StorageImageLocation::FromMTP(
			size,
			size,
			photo.vphoto_small);
		if (!location.isNull()) {
			_photo = Images::Create(location);
			if (!_photo->loaded()) {
				subscribe(Auth().downloaderTaskFinished(), [=] {
					update();
				});
				_photo->load(Data::FileOrigin());
			}
		}
	}
	if (!_photo) {
		_photoEmpty = std::make_unique<Ui::EmptyUserpic>(
			Data::PeerUserpicColor(0),
			title);
	}
}

std::vector<not_null<UserData*>> ConfirmInviteBox::GetParticipants(
		const MTPDchatInvite &data) {
	if (!data.has_participants()) {
		return {};
	}
	const auto &v = data.vparticipants.v;
	auto result = std::vector<not_null<UserData*>>();
	result.reserve(v.size());
	for (const auto &participant : v) {
		if (const auto user = App::feedUser(participant)) {
			result.push_back(user);
		}
	}
	return result;
}

void ConfirmInviteBox::prepare() {
	const auto joinKey = _isChannel
		? lng_profile_join_channel
		: lng_profile_join_group;
	addButton(langFactory(joinKey), _submit);
	addButton(langFactory(lng_cancel), [=] { closeBox(); });

	while (_participants.size() > 4) {
		_participants.pop_back();
	}

	auto newHeight = st::confirmInviteStatusTop + _status->height() + st::boxPadding.bottom();
	if (!_participants.empty()) {
		int skip = (st::boxWideWidth - 4 * st::confirmInviteUserPhotoSize) / 5;
		int padding = skip / 2;
		_userWidth = (st::confirmInviteUserPhotoSize + 2 * padding);
		int sumWidth = _participants.size() * _userWidth;
		int left = (st::boxWideWidth - sumWidth) / 2;
		for (const auto user : _participants) {
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
		p.drawPixmap(
			(width() - st::confirmInvitePhotoSize) / 2,
			st::confirmInvitePhotoTop,
			_photo->pixCircled(
				Data::FileOrigin(),
				st::confirmInvitePhotoSize,
				st::confirmInvitePhotoSize));
	} else {
		_photoEmpty->paint(
			p,
			(width() - st::confirmInvitePhotoSize) / 2,
			st::confirmInvitePhotoTop,
			width(),
			st::confirmInvitePhotoSize);
	}

	int sumWidth = _participants.size() * _userWidth;
	int left = (width() - sumWidth) / 2;
	for_const (auto user, _participants) {
		user->paintUserpicLeft(
			p,
			left + (_userWidth - st::confirmInviteUserPhotoSize) / 2,
			st::confirmInviteUserPhotoTop,
			width(),
			st::confirmInviteUserPhotoSize);
		left += _userWidth;
	}
}

ConfirmInviteBox::~ConfirmInviteBox() = default;

ConfirmDontWarnBox::ConfirmDontWarnBox(
	QWidget*,
	const QString &text,
	const QString &checkbox,
	const QString &confirm,
	FnMut<void(bool)> callback)
: _confirm(confirm)
, _content(setupContent(text, checkbox, std::move(callback))) {
}

void ConfirmDontWarnBox::prepare() {
	setDimensionsToContent(st::boxWidth, _content);
	addButton([=] { return _confirm; }, [=] { _callback(); });
	addButton(langFactory(lng_cancel), [=] { closeBox(); });
}

not_null<Ui::RpWidget*> ConfirmDontWarnBox::setupContent(
		const QString &text,
		const QString &checkbox,
		FnMut<void(bool)> callback) {
	const auto result = Ui::CreateChild<Ui::VerticalLayout>(this);
	result->add(
		object_ptr<Ui::FlatLabel>(
			result,
			text,
			Ui::FlatLabel::InitType::Rich,
			st::boxLabel),
		st::boxPadding);
	const auto control = result->add(
		object_ptr<Ui::Checkbox>(
			result,
			checkbox,
			false,
			st::defaultBoxCheckbox),
		style::margins(
			st::boxPadding.left(),
			st::boxPadding.bottom(),
			st::boxPadding.right(),
			st::boxPadding.bottom()));
	_callback = [=, callback = std::move(callback)]() mutable {
		const auto checked = control->checked();
		auto local = std::move(callback);
		closeBox();
		local(checked);
	};
	return result;
}
