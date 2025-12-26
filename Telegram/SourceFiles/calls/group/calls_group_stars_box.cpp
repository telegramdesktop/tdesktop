/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/group/calls_group_stars_box.h"

#include "boxes/send_credits_box.h"
#include "chat_helpers/compose/compose_show.h"
#include "data/data_message_reactions.h"
#include "data/data_peer.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "payments/ui/payments_reaction_box.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/dynamic_thumbnails.h"
#include "window/window_session_controller.h"

namespace Calls::Group {
namespace {

constexpr auto kMaxStarsFallback = 10'000;
constexpr auto kDefaultStars = 10;

} // namespace

int MaxVideoStreamStarsCount(not_null<Main::Session*> session) {
	const auto appConfig = &session->appConfig();
	return std::max(
		appConfig->get<int>(
			u"stars_groupcall_message_amount_max"_q,
			kMaxStarsFallback),
		2);
}

void VideoStreamStarsBox(
		not_null<Ui::GenericBox*> box,
		VideoStreamStarsBoxArgs &&args) {
	args.show->session().credits().load();

	const auto admin = args.admin;
	const auto sending = args.sending;
	auto submitText = [=](rpl::producer<int> amount) {
		auto nice = std::move(amount) | rpl::map([=](int count) {
			return Ui::CreditsEmojiSmall().append(
				Lang::FormatCountDecimal(count));
		});
		return admin
			? tr::lng_box_ok(tr::marked)
			: (sending
				? tr::lng_paid_reaction_button
				: tr::lng_paid_comment_button)(
					lt_stars,
					std::move(nice),
					tr::rich);
	};
	const auto &show = args.show;
	const auto session = &show->session();
	const auto max = std::max(args.min, MaxVideoStreamStarsCount(session));
	const auto chosen = std::clamp(
		args.current ? args.current : kDefaultStars,
		args.min,
		max);
	auto top = std::vector<Ui::PaidReactionTop>();
	const auto add = [&](const Data::MessageReactionsTopPaid &entry) {
		const auto peer = entry.peer;
		const auto name = peer
			? peer->shortName()
			: tr::lng_paid_react_anonymous(tr::now);
		const auto open = [=] {
			if (const auto controller = show->resolveWindow()) {
				controller->showPeerInfo(peer);
			}
		};
		top.push_back({
			.name = name,
			.photo = (peer
				? Ui::MakeUserpicThumbnail(peer)
				: Ui::MakeHiddenAuthorThumbnail()),
			.barePeerId = peer ? uint64(peer->id.value) : 0,
			.count = int(entry.count),
			.click = peer ? open : Fn<void()>(),
			.my = (entry.my == 1),
		});
	};

	top.reserve(args.top.size() + 1);
	for (const auto &entry : args.top) {
		add(entry);
	}

	auto myAdded = base::flat_set<uint64>();
	const auto i = ranges::find(top, true, &Ui::PaidReactionTop::my);
	if (i != end(top)) {
		myAdded.emplace(i->barePeerId);
	}
	const auto myCount = uint32((i != end(top)) ? i->count : 0);
	const auto myAdd = [&](not_null<PeerData*> peer) {
		const auto barePeerId = uint64(peer->id.value);
		if (!myAdded.emplace(barePeerId).second) {
			return;
		}
		add(Data::MessageReactionsTopPaid{
			.peer = peer,
			.count = myCount,
			.my = true,
		});
	};
	myAdd(session->user());
	ranges::stable_sort(top, ranges::greater(), &Ui::PaidReactionTop::count);
	const auto weak = base::make_weak(box);
	Ui::PaidReactionsBox(box, {
		.min = args.min,
		.chosen = chosen,
		.max = max,
		.top = std::move(top),
		.session = &show->session(),
		.name = args.name,
		.submit = std::move(submitText),
		.colorings = show->session().appConfig().groupCallColorings(),
		.balanceValue = session->credits().balanceValue(),
		.send = [=, save = args.save](int count, uint64 barePeerId) {
			if (!admin) {
				save(count);
			}
			if (const auto strong = weak.get()) {
				strong->closeBox();
			}
		},
		.videoStreamChoosing = !sending,
		.videoStreamSending = sending,
		.videoStreamAdmin = admin,
		.dark = true,
	});
}

object_ptr<Ui::BoxContent> MakeVideoStreamStarsBox(
		VideoStreamStarsBoxArgs &&args) {
	return Box(VideoStreamStarsBox, std::move(args));
}

} // namespace Calls::Group
