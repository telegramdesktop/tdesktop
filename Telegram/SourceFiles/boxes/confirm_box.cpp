/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/confirm_box.h"

#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "apiwrap.h"
#include "history/history.h"
#include "history/history_item.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/toast/toast.h"
#include "ui/image/image.h"
#include "ui/text/text_utilities.h"
#include "ui/empty_userpic.h"
#include "core/click_handler_types.h"
#include "window/window_session_controller.h"
#include "storage/localstorage.h"
#include "data/data_scheduled_messages.h"
#include "data/data_session.h"
#include "data/data_photo.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "data/data_file_origin.h"
#include "data/data_histories.h"
#include "data/data_photo_media.h"
#include "data/data_changes.h"
#include "base/unixtime.h"
#include "main/main_session.h"
#include "mtproto/mtproto_config.h"
#include "facades.h" // Ui::showChatsList
#include "app.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QClipboard>

namespace {

TextParseOptions kInformBoxTextOptions = {
	(TextParseLinks
		| TextParseMultiline
		| TextParseMarkdown
		| TextParseRichText), // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};

TextParseOptions kMarkedTextBoxOptions = {
	(TextParseLinks
		| TextParseMultiline
		| TextParseMarkdown
		| TextParseRichText
		| TextParseMentions
		| TextParseHashtags), // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};

[[nodiscard]] bool IsOldForPin(MsgId id, not_null<PeerData*> peer) {
	const auto normal = peer->migrateToOrMe();
	const auto migrated = normal->migrateFrom();
	const auto top = Data::ResolveTopPinnedId(normal, migrated);
	if (!top) {
		return false;
	} else if (peer == migrated) {
		return top.channel || (id < top.msg);
	} else if (migrated) {
		return top.channel && (id < top.msg);
	} else {
		return (id < top.msg);
	}
}

} // namespace

ConfirmBox::ConfirmBox(
	QWidget*,
	const QString &text,
	ConfirmBox::ConfirmedCallback confirmedCallback,
	FnMut<void()> cancelledCallback)
: _confirmText(tr::lng_box_ok(tr::now))
, _cancelText(tr::lng_cancel(tr::now))
, _confirmStyle(st::defaultBoxButton)
, _text(st::boxWidth - st::boxPadding.left() - st::defaultBox.buttonPadding.right())
, _confirmedCallback(std::move(confirmedCallback))
, _cancelledCallback(std::move(cancelledCallback)) {
	init(text);
}

ConfirmBox::ConfirmBox(
	QWidget*,
	const QString &text,
	const QString &confirmText,
	ConfirmBox::ConfirmedCallback confirmedCallback,
	FnMut<void()> cancelledCallback)
: _confirmText(confirmText)
, _cancelText(tr::lng_cancel(tr::now))
, _confirmStyle(st::defaultBoxButton)
, _text(st::boxWidth - st::boxPadding.left() - st::defaultBox.buttonPadding.right())
, _confirmedCallback(std::move(confirmedCallback))
, _cancelledCallback(std::move(cancelledCallback)) {
	init(text);
}

ConfirmBox::ConfirmBox(
	QWidget*,
	const TextWithEntities &text,
	const QString &confirmText,
	ConfirmBox::ConfirmedCallback confirmedCallback,
	FnMut<void()> cancelledCallback)
: _confirmText(confirmText)
, _cancelText(tr::lng_cancel(tr::now))
, _confirmStyle(st::defaultBoxButton)
, _text(st::boxWidth - st::boxPadding.left() - st::defaultBox.buttonPadding.right())
, _confirmedCallback(std::move(confirmedCallback))
, _cancelledCallback(std::move(cancelledCallback)) {
	init(text);
}

ConfirmBox::ConfirmBox(
	QWidget*,
	const QString &text,
	const QString &confirmText,
	const style::RoundButton &confirmStyle,
	ConfirmBox::ConfirmedCallback confirmedCallback,
	FnMut<void()> cancelledCallback)
: _confirmText(confirmText)
, _cancelText(tr::lng_cancel(tr::now))
, _confirmStyle(confirmStyle)
, _text(st::boxWidth - st::boxPadding.left() - st::defaultBox.buttonPadding.right())
, _confirmedCallback(std::move(confirmedCallback))
, _cancelledCallback(std::move(cancelledCallback)) {
	init(text);
}

ConfirmBox::ConfirmBox(
	QWidget*,
	const QString &text,
	const QString &confirmText,
	const QString &cancelText,
	ConfirmBox::ConfirmedCallback confirmedCallback,
	FnMut<void()> cancelledCallback)
: _confirmText(confirmText)
, _cancelText(cancelText)
, _confirmStyle(st::defaultBoxButton)
, _text(st::boxWidth - st::boxPadding.left() - st::defaultBox.buttonPadding.right())
, _confirmedCallback(std::move(confirmedCallback))
, _cancelledCallback(std::move(cancelledCallback)) {
	init(text);
}

ConfirmBox::ConfirmBox(
	QWidget*,
	const QString &text,
	const QString &confirmText,
	const style::RoundButton &confirmStyle,
	const QString &cancelText,
	ConfirmBox::ConfirmedCallback confirmedCallback,
	FnMut<void()> cancelledCallback)
: _confirmText(confirmText)
, _cancelText(cancelText)
, _confirmStyle(st::defaultBoxButton)
, _text(st::boxWidth - st::boxPadding.left() - st::defaultBox.buttonPadding.right())
, _confirmedCallback(std::move(confirmedCallback))
, _cancelledCallback(std::move(cancelledCallback)) {
	init(text);
}

ConfirmBox::ConfirmBox(
	const InformBoxTag &,
	const QString &text,
	const QString &doneText,
	Fn<void()> closedCallback)
: _confirmText(doneText)
, _confirmStyle(st::defaultBoxButton)
, _informative(true)
, _text(st::boxWidth - st::boxPadding.left() - st::defaultBox.buttonPadding.right())
, _confirmedCallback(generateInformCallback(closedCallback))
, _cancelledCallback(generateInformCallback(closedCallback)) {
	init(text);
}

ConfirmBox::ConfirmBox(
	const InformBoxTag &,
	const TextWithEntities &text,
	const QString &doneText,
	Fn<void()> closedCallback)
: _confirmText(doneText)
, _confirmStyle(st::defaultBoxButton)
, _informative(true)
, _text(st::boxWidth - st::boxPadding.left() - st::defaultBox.buttonPadding.right())
, _confirmedCallback(generateInformCallback(closedCallback))
, _cancelledCallback(generateInformCallback(closedCallback)) {
	init(text);
}

FnMut<void()> ConfirmBox::generateInformCallback(
		Fn<void()> closedCallback) {
	return crl::guard(this, [=] {
		closeBox();
		if (closedCallback) {
			closedCallback();
		}
	});
}

void ConfirmBox::init(const QString &text) {
	_text.setText(
		st::boxLabelStyle,
		text,
		_informative ? kInformBoxTextOptions : _textPlainOptions);
}

void ConfirmBox::init(const TextWithEntities &text) {
	_text.setMarkedText(st::boxLabelStyle, text, kMarkedTextBoxOptions);
}

void ConfirmBox::prepare() {
	addButton(
		rpl::single(_confirmText),
		[=] { confirmed(); },
		_confirmStyle);
	if (!_informative) {
		addButton(
			rpl::single(_cancelText),
			[=] { _cancelled = true; closeBox(); });
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
	_textWidth = st::boxWidth - st::boxPadding.left() - st::defaultBox.buttonPadding.right();
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

		const auto confirmed = &_confirmedCallback;
		if (const auto callbackPtr = std::get_if<1>(confirmed)) {
			if (auto callback = base::take(*callbackPtr)) {
				callback();
			}
		} else if (const auto callbackPtr = std::get_if<2>(confirmed)) {
			if (auto callback = base::take(*callbackPtr)) {
				callback([=] { closeBox(); });
			}
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
		const auto guard = window();
		Ui::hideLayer();
		ActivateClickHandler(guard, activated, e->button());
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

InformBox::InformBox(QWidget*, const QString &text, Fn<void()> closedCallback) : ConfirmBox(ConfirmBox::InformBoxTag(), text, tr::lng_box_ok(tr::now), std::move(closedCallback)) {
}

InformBox::InformBox(QWidget*, const QString &text, const QString &doneText, Fn<void()> closedCallback) : ConfirmBox(ConfirmBox::InformBoxTag(), text, doneText, std::move(closedCallback)) {
}

InformBox::InformBox(QWidget*, const TextWithEntities &text, Fn<void()> closedCallback) : ConfirmBox(ConfirmBox::InformBoxTag(), text, tr::lng_box_ok(tr::now), std::move(closedCallback)) {
}

InformBox::InformBox(QWidget*, const TextWithEntities &text, const QString &doneText, Fn<void()> closedCallback) : ConfirmBox(ConfirmBox::InformBoxTag(), text, doneText, std::move(closedCallback)) {
}

MaxInviteBox::MaxInviteBox(QWidget*, not_null<ChannelData*> channel) : BoxContent()
, _channel(channel)
, _text(
	st::boxLabelStyle,
	tr::lng_participant_invite_sorry(
		tr::now,
		lt_count,
		channel->session().serverConfig().chatSizeMax),
	kInformBoxTextOptions,
	(st::boxWidth
		- st::boxPadding.left()
		- st::defaultBox.buttonPadding.right())) {
}

void MaxInviteBox::prepare() {
	setMouseTracking(true);

	addButton(tr::lng_box_ok(), [=] { closeBox(); });

	_textWidth = st::boxWidth - st::boxPadding.left() - st::defaultBox.buttonPadding.right();
	_textHeight = qMin(_text.countHeight(_textWidth), 16 * st::boxLabelStyle.lineHeight);
	setDimensions(st::boxWidth, st::boxPadding.top() + _textHeight + st::boxTextFont->height + st::boxTextFont->height * 2 + st::newGroupLinkPadding.bottom());

	_channel->session().changes().peerUpdates(
		_channel,
		Data::PeerUpdate::Flag::InviteLink
	) | rpl::start_with_next([=] {
		rtlupdate(_invitationLink);
	}, lifetime());
}

void MaxInviteBox::mouseMoveEvent(QMouseEvent *e) {
	updateSelected(e->globalPos());
}

void MaxInviteBox::mousePressEvent(QMouseEvent *e) {
	mouseMoveEvent(e);
	if (_linkOver) {
		if (_channel->inviteLink().isEmpty()) {
			_channel->session().api().exportInviteLink(_channel);
		} else {
			QGuiApplication::clipboard()->setText(_channel->inviteLink());
			Ui::Toast::Show(tr::lng_create_channel_link_copied(tr::now));
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
	auto inviteLinkText = _channel->inviteLink().isEmpty() ? tr::lng_group_invite_create(tr::now) : _channel->inviteLink();
	p.drawText(_invitationLink, inviteLinkText, option);
}

void MaxInviteBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);
	_invitationLink = myrtlrect(st::boxPadding.left(), st::boxPadding.top() + _textHeight + st::boxTextFont->height, width() - st::boxPadding.left() - st::boxPadding.right(), 2 * st::boxTextFont->height);
}

PinMessageBox::PinMessageBox(
	QWidget*,
	not_null<PeerData*> peer,
	MsgId msgId)
: _peer(peer)
, _api(&peer->session().mtp())
, _msgId(msgId)
, _pinningOld(IsOldForPin(msgId, peer))
, _text(
	this,
	(_pinningOld
		? tr::lng_pinned_pin_old_sure(tr::now)
		: (peer->isChat() || peer->isMegagroup())
		? tr::lng_pinned_pin_sure_group(tr::now)
		: tr::lng_pinned_pin_sure(tr::now)),
	st::boxLabel) {
}

void PinMessageBox::prepare() {
	addButton(tr::lng_pinned_pin(), [this] { pinMessage(); });
	addButton(tr::lng_cancel(), [this] { closeBox(); });

	if (_peer->isUser() && !_peer->isSelf()) {
		_pinForPeer.create(
			this,
			tr::lng_pinned_also_for_other(
				tr::now,
				lt_user,
				_peer->shortName()),
			false,
			st::defaultBoxCheckbox);
		_checkbox = _pinForPeer;
	} else if (!_pinningOld && (_peer->isChat() || _peer->isMegagroup())) {
		_notify.create(
			this,
			tr::lng_pinned_notify(tr::now),
			true,
			st::defaultBoxCheckbox);
		_checkbox = _notify;
	}

	auto height = st::boxPadding.top() + _text->height() + st::boxPadding.bottom();
	if (_checkbox) {
		height += st::boxMediumSkip + _checkbox->heightNoMargins();
	}
	setDimensions(st::boxWidth, height);
}

void PinMessageBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);
	_text->moveToLeft(st::boxPadding.left(), st::boxPadding.top());
	if (_checkbox) {
		_checkbox->moveToLeft(st::boxPadding.left(), _text->y() + _text->height() + st::boxMediumSkip);
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
	if (_pinForPeer && !_pinForPeer->checked()) {
		flags |= MTPmessages_UpdatePinnedMessage::Flag::f_pm_oneside;
	}
	_requestId = _api.request(MTPmessages_UpdatePinnedMessage(
		MTP_flags(flags),
		_peer->input,
		MTP_int(_msgId)
	)).done([=](const MTPUpdates &result) {
		_peer->session().api().applyUpdates(result);
		Ui::hideLayer();
	}).fail([=](const RPCError &error) {
		Ui::hideLayer();
	}).send();
}

DeleteMessagesBox::DeleteMessagesBox(
	QWidget*,
	not_null<HistoryItem*> item,
	bool suggestModerateActions)
: _session(&item->history()->session())
, _ids(1, item->fullId()) {
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
	not_null<Main::Session*> session,
	MessageIdsList &&selected)
: _session(session)
, _ids(std::move(selected)) {
	Expects(!_ids.empty());
}

DeleteMessagesBox::DeleteMessagesBox(
	QWidget*,
	not_null<PeerData*> peer,
	bool justClear)
: _session(&peer->session())
, _wipeHistoryPeer(peer)
, _wipeHistoryJustClear(justClear) {
}

void DeleteMessagesBox::prepare() {
	auto details = TextWithEntities();
	const auto appendDetails = [&](TextWithEntities &&text) {
		details.append(qstr("\n\n")).append(std::move(text));
	};
	auto deleteText = tr::lng_box_delete();
	auto deleteStyle = &st::defaultBoxButton;
	if (const auto peer = _wipeHistoryPeer) {
		if (_wipeHistoryJustClear) {
			details.text = peer->isSelf()
				? tr::lng_sure_delete_saved_messages(tr::now)
				: peer->isUser()
				? tr::lng_sure_delete_history(tr::now, lt_contact, peer->name)
				: tr::lng_sure_delete_group_history(tr::now, lt_group, peer->name);
			deleteStyle = &st::attentionBoxButton;
		} else {
			details.text = peer->isSelf()
				? tr::lng_sure_delete_saved_messages(tr::now)
				: peer->isUser()
				? tr::lng_sure_delete_history(tr::now, lt_contact, peer->name)
				: peer->isChat()
				? tr::lng_sure_delete_and_exit(tr::now, lt_group, peer->name)
				: peer->isMegagroup()
				? tr::lng_sure_leave_group(tr::now)
				: tr::lng_sure_leave_channel(tr::now);
			deleteText = _wipeHistoryPeer->isUser()
				? tr::lng_box_delete()
				: tr::lng_box_leave();
			deleteStyle = &(peer->isChannel()
				? st::defaultBoxButton
				: st::attentionBoxButton);
		}
		if (auto revoke = revokeText(peer)) {
			_revoke.create(this, revoke->checkbox, false, st::defaultBoxCheckbox);
			appendDetails(std::move(revoke->description));
		}
	} else if (_moderateFrom) {
		Assert(_moderateInChannel != nullptr);

		details.text = tr::lng_selected_delete_sure_this(tr::now);
		if (_moderateBan) {
			_banUser.create(this, tr::lng_ban_user(tr::now), false, st::defaultBoxCheckbox);
		}
		_reportSpam.create(this, tr::lng_report_spam(tr::now), false, st::defaultBoxCheckbox);
		if (_moderateDeleteAll) {
			_deleteAll.create(this, tr::lng_delete_all_from(tr::now), false, st::defaultBoxCheckbox);
		}
	} else {
		details.text = (_ids.size() == 1)
			? tr::lng_selected_delete_sure_this(tr::now)
			: tr::lng_selected_delete_sure(tr::now, lt_count, _ids.size());
		if (const auto peer = checkFromSinglePeer()) {
			auto count = int(_ids.size());
			if (hasScheduledMessages()) {
			} else if (auto revoke = revokeText(peer)) {
				_revoke.create(this, revoke->checkbox, false, st::defaultBoxCheckbox);
				appendDetails(std::move(revoke->description));
			} else if (peer->isChannel()) {
				if (peer->isMegagroup()) {
					appendDetails({ tr::lng_delete_for_everyone_hint(tr::now, lt_count, count) });
				}
			} else if (peer->isChat()) {
				appendDetails({ tr::lng_delete_for_me_chat_hint(tr::now, lt_count, count) });
			} else if (!peer->isSelf()) {
				appendDetails({ tr::lng_delete_for_me_hint(tr::now, lt_count, count) });
			}
		}
	}
	_text.create(this, rpl::single(std::move(details)), st::boxLabel);

	addButton(
		std::move(deleteText),
		[=] { deleteAndClear(); },
		*deleteStyle);
	addButton(tr::lng_cancel(), [=] { closeBox(); });

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
	} else if (_revoke) {
		fullHeight += st::boxMediumSkip + _revoke->heightNoMargins();
	}
	setDimensions(st::boxWidth, fullHeight);
}

bool DeleteMessagesBox::hasScheduledMessages() const {
	for (const auto fullId : std::as_const(_ids)) {
		if (const auto item = _session->data().message(fullId)) {
			if (item->isScheduled()) {
				return true;
			}
		}
	}
	return false;
}

PeerData *DeleteMessagesBox::checkFromSinglePeer() const {
	auto result = (PeerData*)nullptr;
	for (const auto fullId : std::as_const(_ids)) {
		if (const auto item = _session->data().message(fullId)) {
			const auto peer = item->history()->peer;
			if (!result) {
				result = peer;
			} else if (result != peer) {
				return nullptr;
			}
		}
	}
	return result;
}

auto DeleteMessagesBox::revokeText(not_null<PeerData*> peer) const
-> std::optional<RevokeConfig> {
	auto result = RevokeConfig();
	if (peer == _wipeHistoryPeer) {
		if (!peer->canRevokeFullHistory()) {
			return std::nullopt;
		} else if (const auto user = peer->asUser()) {
			result.checkbox = tr::lng_delete_for_other_check(
				tr::now,
				lt_user,
				user->firstName);
		} else {
			result.checkbox = tr::lng_delete_for_everyone_check(tr::now);
		}
		return result;
	}

	const auto items = ranges::view::all(
		_ids
	) | ranges::view::transform([&](FullMsgId id) {
		return peer->owner().message(id);
	}) | ranges::view::filter([](HistoryItem *item) {
		return (item != nullptr);
	}) | ranges::to_vector;

	if (items.size() != _ids.size()) {
		// We don't have information about all messages.
		return std::nullopt;
	}

	const auto now = base::unixtime::now();
	const auto canRevoke = [&](HistoryItem * item) {
		return item->canDeleteForEveryone(now);
	};
	const auto cannotRevoke = [&](HistoryItem *item) {
		return !item->canDeleteForEveryone(now);
	};
	const auto canRevokeAll = ranges::none_of(items, cannotRevoke);
	auto outgoing = items | ranges::view::filter(&HistoryItem::out);
	const auto canRevokeOutgoingCount = canRevokeAll
		? -1
		: ranges::count_if(outgoing, canRevoke);

	if (canRevokeAll) {
		if (const auto user = peer->asUser()) {
			result.checkbox = tr::lng_delete_for_other_check(
				tr::now,
				lt_user,
				user->firstName);
		} else {
			result.checkbox = tr::lng_delete_for_everyone_check(tr::now);
		}
		return result;
	} else if (canRevokeOutgoingCount > 0) {
		result.checkbox = tr::lng_delete_for_other_my(tr::now);
		if (const auto user = peer->asUser()) {
			if (canRevokeOutgoingCount == 1) {
				result.description = tr::lng_selected_unsend_about_user_one(
					tr::now,
					lt_user,
					Ui::Text::Bold(user->shortName()),
					Ui::Text::WithEntities);
			} else {
				result.description = tr::lng_selected_unsend_about_user(
					tr::now,
					lt_count,
					canRevokeOutgoingCount,
					lt_user,
					Ui::Text::Bold(user->shortName()),
					Ui::Text::WithEntities);
			}
		} else if (canRevokeOutgoingCount == 1) {
			result.description = tr::lng_selected_unsend_about_group_one(
				tr::now,
				Ui::Text::WithEntities);
		} else {
			result.description = tr::lng_selected_unsend_about_group(
				tr::now,
				lt_count,
				canRevokeOutgoingCount,
				Ui::Text::WithEntities);
		}
		return result;
	}
	return std::nullopt;
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
	} else if (_revoke) {
		const auto availableWidth = width() - 2 * st::boxPadding.left();
		_revoke->resizeToNaturalWidth(availableWidth);
		_revoke->moveToLeft(st::boxPadding.left(), _text->bottomNoMargins() + st::boxMediumSkip);
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
	const auto revoke = _revoke ? _revoke->checked() : false;
	if (const auto peer = _wipeHistoryPeer) {
		const auto justClear = _wipeHistoryJustClear;
		closeBox();

		if (justClear) {
			peer->session().api().clearHistory(peer, revoke);
		} else {
			for (const auto controller : peer->session().windows()) {
				if (controller->activeChatCurrent().peer() == peer) {
					Ui::showChatsList(&peer->session());
				}
			}
			// Don't delete old history by default,
			// because Android app doesn't.
			//
			//if (const auto from = peer->migrateFrom()) {
			//	peer->session().api().deleteConversation(from, false);
			//}
			peer->session().api().deleteConversation(peer, revoke);
		}
		return;
	}
	if (_moderateFrom) {
		if (_banUser && _banUser->checked()) {
			_moderateInChannel->session().api().kickParticipant(
				_moderateInChannel,
				_moderateFrom,
				MTP_chatBannedRights(MTP_flags(0), MTP_int(0)));
		}
		if (_reportSpam->checked()) {
			_moderateInChannel->session().api().request(
				MTPchannels_ReportSpam(
					_moderateInChannel->inputChannel,
					_moderateFrom->inputUser,
					MTP_vector<MTPint>(1, MTP_int(_ids[0].msg)))
			).send();
		}
		if (_deleteAll && _deleteAll->checked()) {
			_moderateInChannel->session().api().deleteAllFromUser(
				_moderateInChannel,
				_moderateFrom);
		}
	}

	if (_deleteConfirmedCallback) {
		_deleteConfirmedCallback();
	}

	_session->data().histories().deleteMessages(_ids, revoke);

	const auto session = _session;
	Ui::hideLayer();
	session->data().sendHistoryChangeNotifications();
}

ConfirmDontWarnBox::ConfirmDontWarnBox(
	QWidget*,
	rpl::producer<TextWithEntities> text,
	const QString &checkbox,
	rpl::producer<QString> confirm,
	FnMut<void(bool)> callback)
: _confirm(std::move(confirm))
, _content(setupContent(std::move(text), checkbox, std::move(callback))) {
}

void ConfirmDontWarnBox::prepare() {
	setDimensionsToContent(st::boxWidth, _content);
	addButton(std::move(_confirm), [=] { _callback(); });
	addButton(tr::lng_cancel(), [=] { closeBox(); });
}

not_null<Ui::RpWidget*> ConfirmDontWarnBox::setupContent(
		rpl::producer<TextWithEntities> text,
		const QString &checkbox,
		FnMut<void(bool)> callback) {
	const auto result = Ui::CreateChild<Ui::VerticalLayout>(this);
	result->add(
		object_ptr<Ui::FlatLabel>(
			result,
			std::move(text),
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
