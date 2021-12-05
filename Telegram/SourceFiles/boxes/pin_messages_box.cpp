/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/pin_messages_box.h"

#include "apiwrap.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"

namespace {

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

	auto height = st::boxPadding.top()
		+ _text->height()
		+ st::boxPadding.bottom();
	if (_checkbox) {
		height += st::boxMediumSkip + _checkbox->heightNoMargins();
	}
	setDimensions(st::boxWidth, height);
}

void PinMessageBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);
	_text->moveToLeft(st::boxPadding.left(), st::boxPadding.top());
	if (_checkbox) {
		_checkbox->moveToLeft(
			st::boxPadding.left(),
			_text->y() + _text->height() + st::boxMediumSkip);
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
	if (_requestId) {
		return;
	}

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
	}).fail([=] {
		Ui::hideLayer();
	}).send();
}
