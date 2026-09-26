/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_community_added.h"

#include "core/click_handler_types.h" // ClickHandlerContext
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "history/view/media/history_view_media_generic.h"
#include "history/view/media/history_view_unique_gift.h" // MakeGenericButtonPart
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/dynamic_image.h"
#include "ui/painter.h"
#include "ui/text/text_utilities.h"
#include "ui/userpic_view.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_menu_icons.h"
#include "styles/style_premium.h"

namespace HistoryView {
namespace {

// The single place where the empty-state community icon is chosen.
// The feature owner will swap the final SVG here later.
[[nodiscard]] const style::icon &CommunityServiceEmptyIcon() {
	return st::menuIconCommunity;
}

// A rounded-square service userpic for the community-added card. When the
// community has a real photo it paints (and auto-updates) the userpic; when it
// has none it fills the rounded-square with the service-bubble background and
// paints a centered group-style icon in the service foreground color.
class CommunityServiceUserpic final : public Ui::DynamicImage {
public:
	explicit CommunityServiceUserpic(not_null<ChannelData*> community);

	std::shared_ptr<Ui::DynamicImage> clone() override;

	QImage image(int size) override;
	void subscribeToUpdates(Fn<void()> callback) override;

private:
	struct Subscribed {
		explicit Subscribed(Fn<void()> callback)
		: callback(std::move(callback)) {
		}

		Ui::PeerUserpicView view;
		Fn<void()> callback;
		InMemoryKey key;
		int paletteVersion = 0;
		bool hadUserpic = false;
		rpl::lifetime photoLifetime;
		rpl::lifetime downloadLifetime;
	};

	[[nodiscard]] bool waitingUserpicLoad() const;
	void processNewPhoto();

	const not_null<ChannelData*> _community;
	QImage _frame;
	std::unique_ptr<Subscribed> _subscribed;

};

CommunityServiceUserpic::CommunityServiceUserpic(
	not_null<ChannelData*> community)
: _community(community) {
}

std::shared_ptr<Ui::DynamicImage> CommunityServiceUserpic::clone() {
	return std::make_shared<CommunityServiceUserpic>(_community);
}

QImage CommunityServiceUserpic::image(int size) {
	Expects(_subscribed != nullptr);

	const auto hasUserpic = _community->hasUserpic();
	const auto good = (_frame.width() == size * _frame.devicePixelRatio());
	const auto key = _community->userpicUniqueKey(_subscribed->view);
	const auto paletteVersion = style::PaletteVersion();
	if (!good
		|| _subscribed->hadUserpic != hasUserpic
		|| (_subscribed->paletteVersion != paletteVersion
			&& (!hasUserpic
				|| _community->useEmptyUserpic(_subscribed->view)))
		|| (_subscribed->key != key && !waitingUserpicLoad())) {
		_subscribed->key = key;
		_subscribed->paletteVersion = paletteVersion;
		_subscribed->hadUserpic = hasUserpic;

		const auto ratio = style::DevicePixelRatio();
		if (!good) {
			_frame = QImage(
				QSize(size, size) * ratio,
				QImage::Format_ARGB32_Premultiplied);
			_frame.setDevicePixelRatio(ratio);
		}
		_frame.fill(Qt::transparent);

		if (hasUserpic) {
			auto p = Painter(&_frame);
			_community->paintUserpic(p, _subscribed->view, {
				.position = QPoint(),
				.size = size,
				.shape = Ui::PeerUserpicShape::Forum,
			});
		} else {
			auto p = Painter(&_frame);
			auto hq = PainterHighQualityEnabler(p);
			const auto radius = size * Ui::ForumUserpicRadiusMultiplier();
			p.setPen(Qt::NoPen);
			p.setBrush(st::msgServiceBg);
			p.drawRoundedRect(QRect(0, 0, size, size), radius, radius);
			CommunityServiceEmptyIcon().paintInCenter(
				p,
				QRect(0, 0, size, size),
				st::msgServiceFg->c);
		}
	}
	return _frame;
}

bool CommunityServiceUserpic::waitingUserpicLoad() const {
	return _community->hasUserpic()
		&& _community->useEmptyUserpic(_subscribed->view);
}

void CommunityServiceUserpic::subscribeToUpdates(Fn<void()> callback) {
	if (!callback) {
		_subscribed = nullptr;
		return;
	}
	const auto old = std::exchange(
		_subscribed,
		std::make_unique<Subscribed>(std::move(callback)));

	_community->session().changes().peerUpdates(
		_community,
		Data::PeerUpdate::Flag::Photo
	) | rpl::on_next([=] {
		_subscribed->callback();
		processNewPhoto();
	}, _subscribed->photoLifetime);

	processNewPhoto();
}

void CommunityServiceUserpic::processNewPhoto() {
	Expects(_subscribed != nullptr);

	if (!waitingUserpicLoad()) {
		_subscribed->downloadLifetime.destroy();
		return;
	}
	_community->session().downloaderTaskFinished(
	) | rpl::filter([=] {
		return !waitingUserpicLoad();
	}) | rpl::on_next([=] {
		_subscribed->callback();
		_subscribed->downloadLifetime.destroy();
	}, _subscribed->downloadLifetime);
}

} // namespace

auto GenerateCommunityAddedMedia(
	not_null<Element*> parent,
	not_null<ChannelData*> community)
-> Fn<void(
		not_null<MediaGeneric*>,
		Fn<void(std::unique_ptr<MediaGenericPart>)>)> {
	return [=](
			not_null<MediaGeneric*> media,
			Fn<void(std::unique_ptr<MediaGenericPart>)> push) {
		const auto from = parent->data()->from();
		const auto peer = parent->data()->history()->peer;
		const auto fromChatItself = (from == peer);

		const auto open = std::make_shared<LambdaClickHandler>([=](
				ClickContext context) {
			const auto my = context.other.value<ClickHandlerContext>();
			if (const auto controller = my.sessionWindow.get()) {
				controller->showPeerInfo(community);
			}
		});

		push(std::make_unique<DynamicImagePart>(
			parent,
			std::make_shared<CommunityServiceUserpic>(community),
			st::msgServiceCommunityAddedPhoto,
			QMargins(
				0,
				st::msgServiceGiftBoxButtonMargins.top() * 2,
				0,
				st::msgServiceGiftBoxButtonMargins.bottom()),
			open,
			true)); // Paint the community stacked-cards effect behind it.

		auto caption = fromChatItself
			? (peer->isBroadcast()
				? tr::lng_action_community_added_channel
				: tr::lng_action_community_added_chat)(
					tr::now,
					lt_community,
					tr::bold(community->name()),
					tr::rich)
			: tr::lng_action_community_added(
				tr::now,
				lt_from,
				tr::bold(from->shortName()),
				lt_community,
				tr::bold(community->name()),
				tr::rich);
		push(std::make_unique<MediaGenericTextPart>(
			std::move(caption),
			QMargins(
				st::msgPadding.left(),
				0,
				st::msgPadding.right(),
				st::msgServiceGiftBoxTitlePadding.bottom()),
			st::premiumPreviewAbout.style));

		push(MakeGenericButtonPart(
			tr::lng_community_view(tr::now),
			QMargins(
				0,
				st::msgServiceGiftBoxButtonMargins.top(),
				0,
				st::msgServiceGiftBoxButtonMargins.bottom()),
			[=] { parent->repaint(); },
			open));
	};
}

} // namespace HistoryView
