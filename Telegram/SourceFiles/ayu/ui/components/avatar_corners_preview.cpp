// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/components/avatar_corners_preview.h"

#include "ayu/utils/official_resources.h"
#include "apiwrap.h"
#include "data/data_peer.h"
#include "data/data_peer_id.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "styles/style_ayu_icons.h"
#include "styles/style_dialogs.h"
#include "styles/style_settings.h"
#include "ui/empty_userpic.h"
#include "ui/painter.h"
#include "ui/userpic_view.h"
#include "ui/effects/ripple_animation.h"
#include "window/window_session_controller.h"
#include "window/window_session_controller_link_info.h"

namespace {

[[nodiscard]] const auto &OfficialChannel() {
	return TeleForge::OfficialResources::kEntries.front();
}

[[nodiscard]] QString OfficialChannelLabel() {
	return QString::fromLatin1(OfficialChannel().label);
}

[[nodiscard]] QString OfficialChannelUsername() {
	return QString::fromLatin1(OfficialChannel().usernameOrId);
}

} // namespace

AvatarCornersPreview::AvatarCornersPreview(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: RpWidget(parent)
, _controller(controller)
, _emptyUserpic(
	Ui::EmptyUserpic::UserpicColor(
		Data::DecideColorIndex(
			peerFromChannel(ChannelId(2331068091)))),
	OfficialChannelLabel()) {
	const auto &row = st::defaultDialogRow;
	setFixedHeight(row.height);
	setCursor(Qt::PointingHandCursor);
	resolveChannel();
}

void AvatarCornersPreview::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);

	const auto &row = st::defaultDialogRow;
	const auto photoSize = row.photoSize;
	const auto xShift = st::settingsButtonNoIcon.padding.left()
		- row.padding.left();
	const auto userpicX = row.padding.left() + xShift;
	const auto userpicY = (height() - photoSize) / 2;

	p.fillRect(rect(), st::windowBg);

	if (_ripple) {
		_ripple->paint(p, 0, 0, width());
		if (_ripple->empty()) {
			_ripple.reset();
		}
	}

	if (_peer) {
		_peer->paintUserpicLeft(
			p, _userpicView, userpicX, userpicY, width(), photoSize);
	} else {
		_emptyUserpic.paintCircle(p, userpicX, userpicY, width(), photoSize);
	}

	const auto nameText = OfficialChannelLabel();
	p.setPen(st::dialogsNameFg);
	p.setFont(st::semiboldFont);
	p.drawText(row.nameLeft + xShift, row.nameTop + st::semiboldFont->ascent, nameText);

	const auto nameWidth = st::semiboldFont->width(nameText);
	const auto &badge = st::dialogsExteraOfficialIcon.icon;
	badge.paint(p, row.nameLeft + xShift + nameWidth, row.nameTop, width());

	p.setPen(st::dialogsTextFg);
	p.setFont(st::dialogsTextFont);
	p.drawText(row.textLeft + xShift, row.textTop + st::dialogsTextFont->ascent, u"Better late than never"_q);
}

void AvatarCornersPreview::mousePressEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton) {
		if (!_ripple) {
			auto mask = Ui::RippleAnimation::RectMask(size());
			_ripple = std::make_unique<Ui::RippleAnimation>(
				st::defaultRippleAnimation,
				std::move(mask),
				[=] { update(); });
		}
		_ripple->add(e->pos());
	}
}

void AvatarCornersPreview::mouseReleaseEvent(QMouseEvent *e) {
	if (_ripple) {
		_ripple->lastStop();
	}
	if (e->button() == Qt::LeftButton && rect().contains(e->pos())) {
		_controller->showPeerByLink(Window::PeerByLinkInfo{
			.usernameOrId = OfficialChannelUsername(),
		});
	}
}

void AvatarCornersPreview::resolveChannel() {
	const auto session = &_controller->session();
	_peer = session->data().peerByUsername(OfficialChannelUsername());
	if (_peer) {
		_peer->loadUserpic();
		subscribeToUpdates();
		return;
	}
	const auto weak = base::make_weak(this);
	session->api().request(MTPcontacts_ResolveUsername(
		MTP_flags(0),
		MTP_string(OfficialChannelUsername()),
		MTP_string()
	)).done([=](const MTPcontacts_ResolvedPeer &result) {
		if (const auto strong = weak.get()) {
			session->data().processUsers(result.data().vusers());
			session->data().processChats(result.data().vchats());
			strong->_peer = session->data().peerLoaded(
				peerFromMTP(result.data().vpeer()));
			if (strong->_peer) {
				strong->_peer->loadUserpic();
				strong->subscribeToUpdates();
			}
			strong->update();
		}
	}).send();
}

void AvatarCornersPreview::subscribeToUpdates() {
	if (!_peer) return;
	_peer->session().downloaderTaskFinished(
	) | rpl::on_next([=] {
		update();
	}, lifetime());
}
