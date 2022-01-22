/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/max_invite_box.h"

#include "api/api_invite_links.h"
#include "apiwrap.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mtproto/mtproto_config.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QClipboard>

namespace {

TextParseOptions kInformBoxTextOptions = {
	(TextParseLinks
		| TextParseMultiline
		| TextParseMarkdown), // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};

} // namespace

MaxInviteBox::MaxInviteBox(QWidget*, not_null<ChannelData*> channel)
: BoxContent()
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

	_textWidth = st::boxWidth
		- st::boxPadding.left()
		- st::defaultBox.buttonPadding.right();
	_textHeight = std::min(
		_text.countHeight(_textWidth),
		16 * st::boxLabelStyle.lineHeight);
	setDimensions(
		st::boxWidth,
		st::boxPadding.top()
			+ _textHeight
			+ st::boxTextFont->height
			+ st::boxTextFont->height * 2
			+ st::newGroupLinkPadding.bottom());

	if (_channel->inviteLink().isEmpty()) {
		_channel->session().api().requestFullPeer(_channel);
	}
	_channel->session().changes().peerUpdates(
		_channel,
		Data::PeerUpdate::Flag::InviteLinks
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
		if (!_channel->inviteLink().isEmpty()) {
			QGuiApplication::clipboard()->setText(_channel->inviteLink());
			Ui::Toast::Show(tr::lng_create_channel_link_copied(tr::now));
		} else if (_channel->isFullLoaded() && !_creatingInviteLink) {
			_creatingInviteLink = true;
			_channel->session().api().inviteLinks().create(_channel);
		}
	}
}

void MaxInviteBox::leaveEventHook(QEvent *e) {
	updateSelected(QCursor::pos());
}

void MaxInviteBox::updateSelected(const QPoint &cursorGlobalPosition) {
	const auto p = QPoint(mapFromGlobal(cursorGlobalPosition));

	const auto linkOver = _invitationLink.contains(p);
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
	_text.drawLeftElided(
		p,
		st::boxPadding.left(),
		st::boxPadding.top(),
		_textWidth,
		width(),
		16,
		style::al_left);

	auto option = QTextOption(style::al_left);
	option.setWrapMode(QTextOption::WrapAnywhere);
	p.setFont(_linkOver
		? st::defaultInputField.font->underline()
		: st::defaultInputField.font);
	p.setPen(st::defaultLinkButton.color);
	const auto inviteLinkText = _channel->inviteLink().isEmpty()
		? tr::lng_group_invite_create(tr::now)
		: _channel->inviteLink();
	p.drawText(_invitationLink, inviteLinkText, option);
}

void MaxInviteBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);
	_invitationLink = myrtlrect(
		st::boxPadding.left(),
		st::boxPadding.top() + _textHeight + st::boxTextFont->height,
		width() - st::boxPadding.left() - st::boxPadding.right(),
		2 * st::boxTextFont->height);
}
