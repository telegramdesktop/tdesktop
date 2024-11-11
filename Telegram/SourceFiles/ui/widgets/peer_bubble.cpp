/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/widgets/peer_bubble.h"

#include "data/data_peer.h"
#include "info/profile/info_profile_values.h"
#include "ui/controls/userpic_button.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/widgets/labels.h"
#include "styles/style_boxes.h"
#include "styles/style_channel_earn.h"
#include "styles/style_chat.h"
#include "styles/style_layers.h"

namespace Ui {

object_ptr<Ui::RpWidget> CreatePeerBubble(
		not_null<Ui::RpWidget*> parent,
		not_null<PeerData*> peer) {
	auto owned = object_ptr<Ui::RpWidget>(parent);
	const auto peerBubble = owned.data();
	peerBubble->setAttribute(Qt::WA_TransparentForMouseEvents);
	const auto left = Ui::CreateChild<Ui::UserpicButton>(
		peerBubble,
		peer,
		st::uploadUserpicButton);
	const auto right = Ui::CreateChild<Ui::FlatLabel>(
		peerBubble,
		Info::Profile::NameValue(peer),
		st::channelEarnSemiboldLabel);
	const auto padding = st::chatGiveawayPeerPadding
		+ QMargins(st::chatGiveawayPeerPadding.left(), 0, 0, 0);
	rpl::combine(
		left->sizeValue(),
		right->sizeValue()
	) | rpl::start_with_next([=](
			const QSize &leftSize,
			const QSize &rightSize) {
		peerBubble->resize(
			leftSize.width() + rightSize.width() + rect::m::sum::h(padding),
			leftSize.height());
		left->moveToLeft(0, 0);
		right->moveToRight(padding.right() + st::lineWidth, padding.top());
		const auto maxRightSize = parent->width()
			- rect::m::sum::h(st::boxRowPadding)
			- rect::m::sum::h(padding)
			- leftSize.width();
		if ((rightSize.width() > maxRightSize) && (maxRightSize > 0)) {
			right->resizeToWidth(maxRightSize);
		}
	}, peerBubble->lifetime());
	peerBubble->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(peerBubble);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgOver);
		const auto rect = peerBubble->rect();
		const auto radius = rect.height() / 2;
		p.drawRoundedRect(rect, radius, radius);
	}, peerBubble->lifetime());

	return owned;
}

} // namespace Ui
