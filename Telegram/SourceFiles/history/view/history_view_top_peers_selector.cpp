// This file is part of Telegram Desktop,
// the official desktop application for the Telegram messaging service.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
//
#include "history/view/history_view_top_peers_selector.h"

#include "apiwrap.h"
#include "base/unique_qptr.h"
#include "chat_helpers/share_message_phrase_factory.h"
#include "data/components/top_peers.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "info/profile/info_profile_values.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "main/session/session_show.h"
#include "ui/controls/dynamic_images_strip.h"
#include "ui/controls/popup_selector.h"
#include "ui/dynamic_image.h"
#include "ui/dynamic_thumbnails.h"
#include "ui/effects/animations.h"
#include "ui/rect.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/tooltip.h"
#include "ui/wrap/padding_wrap.h"
#include "styles/style_chat_helpers.h"

namespace HistoryView {
namespace {

constexpr auto kMaxPeers = 5;

[[nodiscard]] std::vector<not_null<PeerData*>> CollectPeers(
		not_null<Main::Session*> session) {
	const auto user = session->user();
	auto topPeers = session->topPeers().list();
	const auto it = ranges::find(topPeers, user);
	if (it != topPeers.end()) {
		topPeers.erase(it);
	}
	auto result = std::vector<not_null<PeerData*>>();
	result.push_back(user);
	for (const auto &peer : topPeers | ranges::views::take(kMaxPeers - 1)) {
		result.push_back(peer);
	}
	return result;
}

} // namespace

void ShowTopPeersSelector(
		not_null<Ui::RpWidget*> parent,
		std::shared_ptr<Main::SessionShow> show,
		FullMsgId fullId,
		QPoint globalPos) {
	const auto session = &show->session();
	const auto peers = CollectPeers(session);
	auto thumbnails = std::vector<std::shared_ptr<Ui::DynamicImage>>();
	thumbnails.reserve(peers.size());
	for (const auto &peer : peers) {
		thumbnails.push_back(peer->isSelf()
			? Ui::MakeSavedMessagesThumbnail()
			: Ui::MakeUserpicThumbnail(peer));
	}

	const auto send = [=](not_null<PeerData*> peer) {
		if (const auto item = session->data().message(fullId)) {
			session->api().forwardMessages(
				Data::ResolvedForwardDraft{ .items = { item } },
				Api::SendAction(session->data().history(peer)),
				[=] {
					using namespace ChatHelpers;
					auto text = rpl::variable<TextWithEntities>(
						ForwardedMessagePhrase({
							.toCount = 1,
							.singleMessage = 1,
							.to1 = peer,
						})).current();
					show->showToast(std::move(text));
				});
		}
	};

	const auto contentWidth = peers.size() * st::topPeersSelectorUserpicSize
		+ (peers.size() - 1) * st::topPeersSelectorUserpicGap;
	const auto contentHeight = int(
		st::topPeersSelectorUserpicSize
			* (1. + st::topPeersSelectorUserpicExpand));
	const auto selectorWidth = contentWidth
		+ 2 * st::topPeersSelectorPadding;
	const auto selectorHeight = contentHeight
		+ 2 * st::topPeersSelectorPadding;

	struct State {
		base::unique_qptr<Ui::PopupSelector> selector;
		base::unique_qptr<Ui::ImportantTooltip> tooltip;
		Ui::Animations::Simple animation;
		bool finishing = false;
	};
	const auto state = std::make_shared<State>();

	state->selector = base::make_unique_q<Ui::PopupSelector>(
		parent,
		QSize(selectorWidth, selectorHeight));
	const auto selector = state->selector.get();
	selector->setHideFinishedCallback([=, state = std::weak_ptr(state)] {
		if (const auto s = state.lock()) {
			s->selector = nullptr;
			s->tooltip = nullptr;
		}
	});
	const auto userpicsWidget = Ui::CreateChild<Ui::DynamicImagesStrip>(
		selector,
		std::move(thumbnails),
		st::topPeersSelectorUserpicSize,
		st::topPeersSelectorUserpicGap);
	const auto margins = selector->marginsForShadow();
	const auto x = (selectorWidth - contentWidth) / 2 + margins.left();
	const auto y = (selectorHeight - contentHeight) / 2 + margins.top();
	userpicsWidget->setGeometry(
		QRect(x, y, contentWidth, contentHeight)
			+ Margins(int(st::topPeersSelectorUserpicSize
				* st::topPeersSelectorUserpicExpand)));
	userpicsWidget->setCursor(style::cur_pointer);

	const auto hideAll = [=] {
		state->finishing = true;
		if (state->tooltip) {
			state->tooltip->toggleAnimated(false);
		}
		selector->setAttribute(Qt::WA_TransparentForMouseEvents, true);
		selector->hideAnimated();
	};

	userpicsWidget->setClickCallback([=](int index) {
		if (state->finishing) {
			return;
		}
		send(peers[index]);
		hideAll();
	});
	userpicsWidget->hoveredItemValue(
	) | rpl::on_next([=](Ui::HoveredItemInfo info) {
		if (info.index < 0) {
			state->tooltip = nullptr;
			return;
		}
		using namespace Info::Profile;
		state->tooltip = base::make_unique_q<Ui::ImportantTooltip>(
			parent,
			object_ptr<Ui::PaddingWrap<Ui::FlatLabel>>(
				selector,
				Ui::MakeNiceTooltipLabel(
					parent,
					peers[info.index]->isSelf()
						? tr::lng_saved_messages(tr::rich)
						: NameValue(peers[info.index]) | rpl::map(tr::rich),
					userpicsWidget->width(),
					st::topPeersSelectorImportantTooltipLabel),
				st::topPeersSelectorImportantTooltip.padding),
			st::topPeersSelectorImportantTooltip);
		state->tooltip->setWindowFlags(Qt::WindowFlags(Qt::ToolTip)
			| Qt::BypassWindowManagerHint
			| Qt::NoDropShadowWindowHint
			| Qt::FramelessWindowHint);
		state->tooltip->setAttribute(Qt::WA_NoSystemBackground, true);
		state->tooltip->setAttribute(Qt::WA_TranslucentBackground, true);
		state->tooltip->setAttribute(Qt::WA_TransparentForMouseEvents, true);
		const auto step = st::topPeersSelectorUserpicSize
			+ st::topPeersSelectorUserpicGap;
		const auto shift = (userpicsWidget->height()
			- st::topPeersSelectorUserpicSize) / 2;
		const auto localX = info.index * step + shift;
		const auto avatarRect = QRect(
			localX,
			-shift,
			st::topPeersSelectorUserpicSize,
			st::topPeersSelectorUserpicSize);
		const auto globalRect = QRect(
			userpicsWidget->mapToGlobal(avatarRect.topLeft()),
			avatarRect.size());
		state->tooltip->pointAt(globalRect, RectPart::Top);
		state->tooltip->toggleAnimated(true);
	}, selector->lifetime());
	selector->updateShowState(0, 0, true);
	selector->popup((!globalPos.isNull() ? globalPos : QCursor::pos())
		- QPoint(selector->width() / 2, selector->height())
		+ st::topPeersSelectorSkip);
	selector->events(
	) | rpl::on_next([=](not_null<QEvent*> e) {
		if (e->type() == QEvent::KeyPress) {
			const auto key = static_cast<QKeyEvent*>(e.get());
			if (key->key() == Qt::Key_Escape) {
				hideAll();
			} else {
				userpicsWidget->handleKeyPressEvent(key);
			}
		}
	}, selector->lifetime());
	crl::on_main(selector, [=] {
		selector->setFocus();
	});

	constexpr auto kShift = 0.15;
	state->animation.start([=](float64 value) {
		const auto userpicsProgress = std::clamp((value - kShift), 0., 1.);
		userpicsWidget->setProgress(anim::easeInQuint(1, userpicsProgress));
		value = std::clamp(value, 0., 1.);
		selector->updateShowState(value, value, true);
	}, 0., 1. + kShift, st::fadeWrapDuration * 3, anim::easeOutQuint);
}

} // namespace HistoryView
