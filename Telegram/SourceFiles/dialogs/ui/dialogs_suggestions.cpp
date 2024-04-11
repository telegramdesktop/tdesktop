/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/ui/dialogs_suggestions.h"

#include "data/components/top_peers.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/elastic_scroll.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/dynamic_thumbnails.h"
#include "styles/style_dialogs.h"
#include "styles/style_layers.h"

namespace Dialogs {

Suggestions::Suggestions(
	not_null<QWidget*> parent,
	rpl::producer<TopPeersList> topPeers)
: RpWidget(parent)
, _scroll(std::make_unique<Ui::ElasticScroll>(this))
, _content(_scroll->setOwnedWidget(object_ptr<Ui::VerticalLayout>(this)))
, _topPeersWrap(_content->add(object_ptr<Ui::SlideWrap<TopPeersStrip>>(
	this,
	object_ptr<TopPeersStrip>(this, std::move(topPeers)))))
, _topPeers(_topPeersWrap->entity())
, _divider(_content->add(setupDivider())) {
	_topPeers->emptyValue() | rpl::start_with_next([=](bool empty) {
		_topPeersWrap->toggle(!empty, anim::type::instant);
	}, _topPeers->lifetime());

	_topPeers->clicks() | rpl::start_with_next([=](uint64 peerIdRaw) {
		_topPeerChosen.fire(PeerId(peerIdRaw));
	}, _topPeers->lifetime());
}

Suggestions::~Suggestions() = default;

void Suggestions::paintEvent(QPaintEvent *e) {
	QPainter(this).fillRect(e->rect(), st::windowBg);
}

void Suggestions::resizeEvent(QResizeEvent *e) {
	_scroll->setGeometry(rect());
	_content->resizeToWidth(width());
}

object_ptr<Ui::RpWidget> Suggestions::setupDivider() {
	auto result = object_ptr<Ui::FixedHeightWidget>(
		this,
		st::searchedBarHeight);
	const auto raw = result.data();
	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		raw,
		tr::lng_recent_title(),
		st::searchedBarLabel);
	const auto clear = Ui::CreateChild<Ui::LinkButton>(
		raw,
		tr::lng_recent_clear(tr::now),
		st::searchedBarLink);
	rpl::combine(
		raw->sizeValue(),
		clear->widthValue()
	) | rpl::start_with_next([=](QSize size, int width) {
		const auto x = st::searchedBarPosition.x();
		const auto y = st::searchedBarPosition.y();
		clear->moveToRight(0, 0, size.width());
		label->resizeToWidth(size.width() - x - width);
		label->moveToLeft(x, y, size.width());
	}, raw->lifetime());
	raw->paintRequest() | rpl::start_with_next([=](QRect clip) {
		QPainter(raw).fillRect(clip, st::searchedBarBg);
	}, raw->lifetime());
	return result;
}

TopPeersList TopPeersContent(not_null<Main::Session*> session) {
	auto base = TopPeersList();
	const auto top = session->topPeers().list();
	for (const auto &peer : top) {
		base.entries.push_back(TopPeersEntry{
			.id = peer->id.value,
			.name = peer->shortName(),
			.userpic = Ui::MakeUserpicThumbnail(peer),
		});
	}
	return base;
}

} // namespace Dialogs
