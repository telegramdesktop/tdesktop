/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/star_gift_auction_box.h"

#include "api/api_text_entities.h"
#include "base/random.h"
#include "base/timer_rpl.h"
#include "base/unixtime.h"
#include "boxes/peers/replace_boost_box.h"
#include "boxes/premium_preview_box.h"
#include "boxes/send_credits_box.h" // CreditsEmojiSmall
#include "boxes/share_box.h"
#include "boxes/star_gift_box.h"
#include "boxes/star_gift_preview_box.h"
#include "boxes/star_gift_resale_box.h"
#include "calls/group/calls_group_common.h"
#include "core/application.h"
#include "core/credits_amount.h"
#include "core/ui_integration.h"
#include "data/components/credits.h"
#include "data/components/gift_auctions.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/data_document.h"
#include "data/data_message_reactions.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/view/controls/history_view_suggest_options.h"
#include "info/channel_statistics/earn/earn_icons.h"
#include "info/peer_gifts/info_peer_gifts_common.h"
#include "lang/lang_keys.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "payments/ui/payments_reaction_box.h"
#include "payments/payments_checkout_process.h"
#include "settings/settings_credits_graphics.h"
#include "storage/storage_account.h"
#include "ui/boxes/confirm_box.h"
#include "ui/controls/button_labels.h"
#include "ui/controls/feature_list.h"
#include "ui/controls/table_rows.h"
#include "ui/controls/userpic_button.h"
#include "ui/effects/premium_bubble.h"
#include "ui/layers/generic_box.h"
#include "ui/text/custom_emoji_helper.h"
#include "ui/text/custom_emoji_text_badge.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/widgets/fields/number_input.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/table_layout.h"
#include "ui/color_int_conversion.h"
#include "ui/dynamic_thumbnails.h"
#include "ui/vertical_list.h"
#include "window/window_session_controller.h"
#include "styles/style_calls.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_credits.h"
#include "styles/style_giveaway.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_premium.h"
#include "styles/style_settings.h"

#include <QtWidgets/QApplication>
#include <QtGui/QClipboard>

namespace Ui {
namespace {

constexpr auto kAuctionAboutShownPref = "gift_auction_about_shown"_cs;
constexpr auto kBidPlacedToastDuration = 5 * crl::time(1000);
constexpr auto kSwitchPreviewCoverInterval = 3 * crl::time(1000);
constexpr auto kMaxShownBid = 30'000;
constexpr auto kShowTopPlaces = 3;

enum class BidType {
	Setting,
	Winning,
	Loosing,
};
struct BidRowData {
	UserData *user = nullptr;
	int stars = 0;
	int position = 0;
	int winners = 0;
	QString place;
	BidType type = BidType::Setting;

	friend inline bool operator==(
		const BidRowData &,
		const BidRowData &) = default;
};

struct BidSliderValues {
	int min = 0;
	int explicitlyAllowed = 0;
	int max = 0;

	friend inline bool operator==(
		const BidSliderValues &,
		const BidSliderValues &) = default;
};

[[nodiscard]] std::optional<QColor> BidColorOverride(int position, int per) {
	return (position <= per)
		? st::boxTextFgGood->c
		: st::attentionButtonFg->c;
	//switch (type) {
	//case BidType::Setting: return {};
	//case BidType::Winning: return st::boxTextFgGood->c;
	//case BidType::Loosing: return st::attentionButtonFg->c;
	//}
	//Unexpected("Type in BidType.");
}

[[nodiscard]] rpl::producer<int> MinutesLeftTillValue(TimeId endDate) {
	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();

		const auto now = base::unixtime::now();
		if (endDate <= now) {
			consumer.put_next(0);
			consumer.put_done();
			return lifetime;
		}

		const auto timer = lifetime.make_state<base::Timer>();
		const auto callback = [=] {
			const auto now = base::unixtime::now();
			const auto left = (endDate > now) ? endDate - now : 0;
			const auto minutes = (left + 59) / 60;
			consumer.put_next_copy(minutes);
			if (minutes) {
				const auto next = left % 60;
				const auto wait = next ? next : 60;
				timer->callOnce(wait * crl::time(1000));
			} else {
				consumer.put_done();
			}
		};
		timer->setCallback(callback);
		callback();

		return lifetime;
	};
}

[[nodiscard]] rpl::producer<int> SecondsLeftTillValue(TimeId endDate) {
	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();

		const auto now = base::unixtime::now();
		const auto left = (endDate > now) ? endDate - now : 0;
		if (!left) {
			consumer.put_next(0);
			consumer.put_done();
			return lifetime;
		}

		const auto starts = crl::now();
		const auto ends = starts + left * crl::time(1000);
		const auto timer = lifetime.make_state<base::Timer>();
		const auto callback = [=] {
			const auto now = crl::now();
			const auto left = (ends > now) ? ends - now : 0;
			const auto seconds = (left + 999) / 1000;
			consumer.put_next_copy(seconds);
			if (seconds) {
				const auto next = left % 1000;
				const auto wait = next ? next : 1000;
				timer->callOnce(wait);
			} else {
				consumer.put_done();
			}
		};
		timer->setCallback(callback);
		callback();

		return lifetime;
	};
}

[[nodiscard]] QString NiceCountdownText(int seconds) {
	const auto minutes = seconds / 60;
	const auto hours = minutes / 60;
	return hours
		? u"%1:%2:%3"_q
		.arg(hours)
		.arg((minutes % 60), 2, 10, QChar('0'))
		.arg((seconds % 60), 2, 10, QChar('0'))
		: u"%1:%2"_q.arg(minutes).arg((seconds % 60), 2, 10, QChar('0'));
}

[[nodiscard]] object_ptr<RpWidget> MakeBidRow(
		not_null<RpWidget*> parent,
		std::shared_ptr<ChatHelpers::Show> show,
		rpl::producer<BidRowData> data) {
	auto result = object_ptr<RpWidget>(parent.get());
	const auto raw = result.data();

	raw->setAttribute(Qt::WA_TransparentForMouseEvents);

	struct State {
		std::unique_ptr<FlatLabel> place;
		std::unique_ptr<UserpicButton> userpic;
		std::unique_ptr<FlatLabel> name;
		std::unique_ptr<FlatLabel> stars;
		UserData *user = nullptr;
	};
	const auto state = raw->lifetime().make_state<State>();
	state->place = std::make_unique<FlatLabel>(
		raw,
		rpl::duplicate(data) | rpl::map(&BidRowData::place),
		st::auctionBidPlace);

	auto name = rpl::duplicate(data) | rpl::map([](const BidRowData &bid) {
		return bid.user ? bid.user->name() : QString();
	});
	state->name = std::make_unique<FlatLabel>(
		raw,
		std::move(name),
		st::auctionBidName);

	auto helper = Text::CustomEmojiHelper(Core::TextContext({
		.session = &show->session(),
	}));
	const auto star = helper.paletteDependent(Ui::Earn::IconCreditsEmoji());
	auto stars = rpl::duplicate(data) | rpl::map([=](const BidRowData &bid) {
		return tr::marked(star).append(' ').append(
			Lang::FormatCountDecimal(bid.stars));
	});
	state->stars = std::make_unique<FlatLabel>(
		raw,
		std::move(stars),
		st::auctionBidStars,
		st::defaultPopupMenu,
		helper.context());

	const auto kHuge = u"99999"_q;
	const auto userpicLeft = st::auctionBidPlace.style.font->width(kHuge);

	std::move(data) | rpl::on_next([=](BidRowData bid) {
		state->place->setTextColorOverride(
			BidColorOverride(bid.position, bid.winners));
		if (state->user != bid.user) {
			state->user = bid.user;
			if (state->user) {
				if (auto was = state->userpic.release()) {
					was->hide();
					crl::on_main(was, [=] { delete was; });
				}
				state->userpic = std::make_unique<UserpicButton>(
					raw,
					state->user,
					st::auctionBidUserpic);
				state->userpic->show();
				state->userpic->moveToLeft(userpicLeft, 0);
				raw->resize(raw->width(), state->userpic->height());
			} else {
				raw->resize(raw->width(), 0);
			}
		}
	}, raw->lifetime());

	rpl::combine(
		raw->widthValue(),
		state->stars->widthValue()
	) | rpl::on_next([=](int outer, int stars) {
		const auto userpicSize = st::auctionBidUserpic.size;
		const auto top = (userpicSize.height() - st::normalFont->height) / 2;
		state->place->moveToLeft(0, top, outer);
		if (state->userpic) {
			state->userpic->moveToLeft(userpicLeft, 0, outer);
		}
		state->stars->moveToRight(0, top, outer);

		const auto userpicRight = userpicLeft + userpicSize.width();
		const auto nameLeft = userpicRight + st::auctionBidSkip;
		const auto nameRight = stars + st::auctionBidSkip;
		state->name->resizeToWidth(outer - nameLeft - nameRight);
		state->name->moveToLeft(nameLeft, top, outer);
	}, raw->lifetime());

	return result;
}

Fn<void(not_null<Ui::PopupMenu*>)> MakeAuctionFillMenuCallback(
		std::shared_ptr<ChatHelpers::Show> show,
		const Data::GiftAuctionState &state) {
	const auto url = show->session().createInternalLinkFull(
		u"auction/"_q + state.gift->auctionSlug);
	const auto rounds = state.totalRounds;
	const auto perRound = state.gift->auctionGiftsPerRound;;
	return [=](not_null<Ui::PopupMenu*> menu) {
		menu->addAction(tr::lng_auction_menu_about(tr::now), [=] {
			show->show(Box(AuctionAboutBox, rounds, perRound, nullptr));
		}, &st::menuIconInfo);

		menu->addAction(tr::lng_auction_menu_copy_link(tr::now), [=] {
			QApplication::clipboard()->setText(url);
			show->showToast(tr::lng_username_copied(tr::now));
		}, &st::menuIconLink);

		menu->addAction(tr::lng_auction_menu_share(tr::now), [=] {
			FastShareLink(show, url);
		}, &st::menuIconShare);
	};
}

Fn<void()> MakeAuctionMenuCallback(
		not_null<QWidget*> parent,
		std::shared_ptr<ChatHelpers::Show> show,
		const Data::GiftAuctionState &state) {
	const auto menu = std::make_shared<base::unique_qptr<PopupMenu>>();
	return [=, fill = MakeAuctionFillMenuCallback(show, state)] {
		*menu = base::make_unique_q<Ui::PopupMenu>(
			parent,
			st::popupMenuWithIcons);
		fill(menu->get());
		(*menu)->popup(QCursor::pos());
	};
}

void PlaceAuctionBid(
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<PeerData*> to,
		int64 amount,
		const Data::GiftAuctionState &state,
		std::unique_ptr<Info::PeerGifts::GiftSendDetails> details,
		Fn<void(Payments::CheckoutResult)> done) {
	auto paymentDone = [=](
			Payments::CheckoutResult result,
			const MTPUpdates *updates) {
		done(result);
	};
	const auto passDetails = (details || !state.my.bid);
	const auto hideName = passDetails && details && details->anonymous;
	const auto text = details ? details->text : TextWithEntities();

	using Flag = MTPDinputInvoiceStarGiftAuctionBid::Flag;
	const auto invoice = MTP_inputInvoiceStarGiftAuctionBid(
		MTP_flags((state.my.bid ? Flag::f_update_bid : Flag())
			| (passDetails ? Flag::f_peer : Flag())
			| (passDetails ? Flag::f_message : Flag())
			| (hideName ? Flag::f_hide_name : Flag())),
		passDetails ? to->input() : MTP_inputPeerEmpty(),
		MTP_long(state.gift->id),
		MTP_long(amount),
		MTP_textWithEntities(
			MTP_string(text.text),
			Api::EntitiesToMTP(&to->session(), text.entities)));
	RequestOurForm(show, invoice, [=](
			uint64 formId,
			CreditsAmount price,
			std::optional<Payments::CheckoutResult> failure) {
		if (failure) {
			paymentDone(*failure, nullptr);
		} else {
			SubmitStarsForm(
				show,
				invoice,
				formId,
				price.whole(),
				paymentDone);
		}
	});
}

object_ptr<RpWidget> MakeAuctionInfoBlocks(
		not_null<RpWidget*> box,
		not_null<Main::Session*> session,
		rpl::producer<Data::GiftAuctionState> stateValue,
		Fn<void()> setMinimal) {
	auto helper = Text::CustomEmojiHelper(Core::TextContext({
		.session = session,
	}));
	const auto star = helper.paletteDependent(Ui::Earn::IconCreditsEmoji());

	auto bidTitle = rpl::duplicate(
		stateValue
	) | rpl::map([=](const Data::GiftAuctionState &state) {
		const auto count = int(state.my.minBidAmount
			? state.my.minBidAmount
			: state.minBidAmount);
		const auto text = (count >= 10'000'000)
			? Lang::FormatCountToShort(count).string
			: (count >= 1000'000)
			? Lang::FormatCountToShort(count, true).string
			: Lang::FormatCountDecimal(count);
		return tr::marked(star).append(' ').append(text);
	});
	auto minimal = rpl::duplicate(
		stateValue
	) | rpl::map([=](const Data::GiftAuctionState &state) {
		return state.my.minBidAmount
			? state.my.minBidAmount
			: state.minBidAmount;
	}) | tr::to_count();
	auto untilTitle = rpl::duplicate(
		stateValue
	) | rpl::map([=](const Data::GiftAuctionState &state) {
		return SecondsLeftTillValue(state.startDate) | rpl::then(
			SecondsLeftTillValue(state.nextRoundAt
				? state.nextRoundAt
				: state.endDate));
	}) | rpl::flatten_latest(
	) | rpl::map(NiceCountdownText) | rpl::map(tr::marked);
	auto untilSubtext = rpl::duplicate(
		stateValue
	) | rpl::map([=](const Data::GiftAuctionState &state) {
		auto preview = SecondsLeftTillValue(
			state.startDate
		) | rpl::map(rpl::mappers::_1 > 0) | rpl::distinct_until_changed();
		return rpl::conditional(
			std::move(preview),
			tr::lng_auction_bid_before_start(),
			tr::lng_auction_bid_until());
	}) | rpl::flatten_latest();
	auto leftTitle = rpl::duplicate(
		stateValue
	) | rpl::map([=](const Data::GiftAuctionState &state) {
		return Data::SingleCustomEmoji(
			state.gift->document
		).append(' ').append(Lang::FormatCountDecimal(state.giftsLeft));
	});
	auto left = rpl::duplicate(
		stateValue
	) | rpl::map([=](const Data::GiftAuctionState &state) {
		return state.giftsLeft;
	}) | tr::to_count();
	return MakeStarSelectInfoBlocks(box, {
		{
			.title = std::move(bidTitle),
			.subtext = tr::lng_auction_bid_minimal(
				lt_count,
				std::move(minimal)),
			.click = setMinimal,
		},
		{
			.title = std::move(untilTitle),
			.subtext = std::move(untilSubtext),
		},
		{
			.title = std::move(leftTitle),
			.subtext = tr::lng_auction_bid_left(lt_count, std::move(left))
		},
	}, helper.context());
}

void AddBidPlaces(
		not_null<GenericBox*> box,
		std::shared_ptr<ChatHelpers::Show> show,
		rpl::producer<Data::GiftAuctionState> value,
		rpl::producer<int> chosen) {
	struct My {
		BidType type;
		int position = 0;

		inline bool operator==(const My &) const = default;
	};
	struct State {
		rpl::variable<My> my;
		rpl::variable<bool> started;
		rpl::variable<std::vector<BidRowData>> top;
		std::vector<Ui::PeerUserpicView> cache;
		int winners = 0;
	};
	const auto state = box->lifetime().make_state<State>();

	rpl::duplicate(
		value
	) | rpl::on_next([=](const Data::GiftAuctionState &value) {
		auto cache = std::vector<Ui::PeerUserpicView>();
		cache.reserve(value.topBidders.size());
		for (const auto &user : value.topBidders) {
			cache.push_back(user->createUserpicView());
		}
		state->winners = value.gift->auctionGiftsPerRound;
		state->cache = std::move(cache);
		state->started = SecondsLeftTillValue(
			value.startDate
		) | rpl::map(!rpl::mappers::_1);
	}, box->lifetime());

	state->my = rpl::combine(
		rpl::duplicate(value),
		rpl::duplicate(chosen)
	) | rpl::map([=](const Data::GiftAuctionState &value, int chosen) {
		const auto my = value.my.bid;
		const auto &levels = value.bidLevels;

		auto top = std::vector<BidRowData>();
		top.reserve(kShowTopPlaces);
		const auto pushTop = [&](auto i) {
			const auto index = int(i - begin(levels));
			if (top.size() >= kShowTopPlaces
				|| index >= value.topBidders.size()) {
				return false;
			} else if (!value.topBidders[index]->isSelf()) {
				top.push_back({ value.topBidders[index], int(i->amount) });
			}
			return true;
		};

		const auto setting = (chosen > my);
		const auto finishWith = [&](int position) {
			state->top = std::move(top);
			const auto type = setting
				? BidType::Setting
				: (position <= value.gift->auctionGiftsPerRound)
				? BidType::Winning
				: BidType::Loosing;
			return My{ type, position };
		};

		for (auto i = begin(levels), e = end(levels); i != e; ++i) {
			if (i->amount < chosen
				|| (!setting
					&& i->amount == chosen
					&& i->date >= value.my.date)) {
				top.push_back({ show->session().user(), chosen });
				for (auto j = i; j != e; ++j) {
					if (!pushTop(j)) {
						break;
					}
				}
				return finishWith(i->position);
			}
			pushTop(i);
		}
		top.push_back({ show->session().user(), chosen });
		return finishWith((levels.empty() ? 0 : levels.back().position) + 1);
	});
	auto myLabelText = rpl::combine(
		state->my.value(),
		state->started.value()
	) | rpl::map([](My my, bool started) {
		if (!started) {
			return tr::lng_auction_bid_your_title();
		}
		switch (my.type) {
		case BidType::Setting: return tr::lng_auction_bid_your_title();
		case BidType::Winning: return tr::lng_auction_bid_your_winning();
		case BidType::Loosing: return tr::lng_auction_bid_your_outbid();
		}
		Unexpected("Type in BidType.");
	}) | rpl::flatten_latest();
	const auto myLabel = AddSubsectionTitle(
		box->verticalLayout(),
		std::move(myLabelText));
	state->my.value() | rpl::on_next([=](My my) {
		myLabel->setTextColorOverride(
			BidColorOverride(my.position, state->winners));
	}, myLabel->lifetime());

	auto bid = rpl::combine(
		state->my.value(),
		rpl::duplicate(chosen)
	) | rpl::map([=, user = show->session().user()](My my, int stars) {
		const auto position = my.position;
		const auto winners = state->winners;
		const auto place = QString::number(position);
		return BidRowData{ user, stars, position, winners, place, my.type };
	});
	box->addRow(MakeBidRow(box, show, std::move(bid)));

	AddSubsectionTitle(
		box->verticalLayout(),
		tr::lng_auction_bid_winners_title(),
		{ 0, st::paidReactTitleSkip / 2, 0, 0 });
	for (auto i = 0; i != kShowTopPlaces; ++i) {
		auto icon = QString::fromUtf8("\xf0\x9f\xa5\x87");
		icon.back().unicode() += i;

		auto bid = state->top.value(
		) | rpl::map([=](const std::vector<BidRowData> &top) {
			auto result = (i < top.size()) ? top[i] : BidRowData();
			result.place = icon;
			return result;
		});
		box->addRow(MakeBidRow(box, show, std::move(bid)));
	}
}

void EditCustomBid(
		not_null<GenericBox*> box,
		std::shared_ptr<ChatHelpers::Show> show,
		Fn<void(int)> save,
		rpl::producer<int> minBid,
		int current) {
	box->setTitle(tr::lng_auction_bid_custom_title());

	const auto container = box->verticalLayout();

	box->addTopButton(st::boxTitleClose, [=] { box->closeBox(); });

	const auto starsField = HistoryView::AddStarsInputField(container, {
		.value = current,
	});

	const auto min = box->lifetime().make_state<rpl::variable<int>>(
		std::move(minBid));

	box->setFocusCallback([=] {
		starsField->setFocusFast();
	});

	const auto submit = [=] {
		const auto value = starsField->getLastText().toLongLong();
		if (value <= min->current() || value > 1'000'000'000) {
			starsField->showError();
			return;
		}
		save(value);
		box->closeBox();
	};
	QObject::connect(starsField, &Ui::NumberInput::submitted, submit);

	box->addButton(tr::lng_settings_save(), submit);
	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});
}

void AuctionBidBox(not_null<GenericBox*> box, AuctionBidBoxArgs &&args) {
	using namespace Info::PeerGifts;
	struct State {
		State(rpl::producer<Data::GiftAuctionState> value)
		: value(std::move(value)) {
		}

		rpl::variable<Data::GiftAuctionState> value;
		rpl::variable<BidSliderValues> sliderValues;
		rpl::variable<int> chosen;
		rpl::variable<QString> subtext;
		rpl::variable<bool> started;
		bool placing = false;
	};
	const auto state = box->lifetime().make_state<State>(
		std::move(args.state));
	state->started = state->value.value(
	) | rpl::map([=](const Data::GiftAuctionState &value) {
		return value.startDate;
	}) | rpl::distinct_until_changed(
	) | rpl::map([=](TimeId startTime) {
		return SecondsLeftTillValue(
			startTime
		) | rpl::map([=](int seconds) {
			return !seconds;
		});
	}) | rpl::flatten_latest();
	state->sliderValues = state->value.value(
	) | rpl::map([=](const Data::GiftAuctionState &value) {
		const auto mine = int(value.my.bid);
		const auto min = std::max(1, int(value.my.minBidAmount
			? value.my.minBidAmount
			: value.minBidAmount));
		const auto last = value.bidLevels.empty()
			? 0
			: value.bidLevels.front().amount;
		auto max = std::max({
			min + 1,
			kMaxShownBid,
			int(base::SafeRound(mine * 1.2)),
		});
		if (max < last * 1.05) {
			max = int(base::SafeRound(last * 1.2));
		}
		return BidSliderValues{
			.min = min,
			.explicitlyAllowed = mine,
			.max = max,
		};
	});

	const auto show = args.show;
	const auto giftId = state->value.current().gift->id;
	const auto &sliderValues = state->sliderValues.current();
	state->chosen = sliderValues.explicitlyAllowed
		? sliderValues.explicitlyAllowed
		: sliderValues.min;

	state->subtext = rpl::combine(
		state->value.value(),
		state->chosen.value()
	) | rpl::map([=](
			const Data::GiftAuctionState &value,
			int chosen) {
		if (value.my.bid == chosen) {
			return tr::lng_auction_bid_your(tr::now);
		} else if (chosen == state->sliderValues.current().max) {
			return tr::lng_auction_bid_custom(tr::now);
		} else if (value.my.bid && chosen > value.my.bid) {
			const auto delta = chosen - value.my.bid;
			return '+' + Lang::FormatCountDecimal(delta);
		}
		return QString();
	});

	args.peer->owner().giftAuctionGots(
	) | rpl::on_next([=](const Data::GiftAuctionGot &update) {
		if (update.giftId == giftId) {
			box->closeBox();

			if (const auto window = show->resolveWindow()) {
				window->showPeerHistory(
					update.to,
					Window::SectionShow::Way::ClearStack,
					ShowAtTheEndMsgId);
			}
		}
	}, box->lifetime());

	const auto details = args.details
		? *args.details
		: std::optional<GiftSendDetails>();
	const auto colorings = show->session().appConfig().groupCallColorings();

	box->setWidth(st::boxWideWidth);
	box->setStyle(st::paidReactBox);
	box->setNoContentMargin(true);

	const auto content = box->verticalLayout();
	AddSkip(content, st::boxTitleClose.height + st::paidReactBubbleTop);

	const auto activeFgOverride = [=](int count) {
		const auto coloring = Calls::Group::Ui::StarsColoringForCount(
			colorings,
			count);
		return ColorFromSerialized(coloring.bgLight);
	};
	const auto sliderWrap = content->add(
		object_ptr<VerticalLayout>(content));
	state->sliderValues.value(
	) | rpl::on_next([=](const BidSliderValues &values) {
		const auto initial = !sliderWrap->count();
		if (!initial) {
			while (sliderWrap->count()) {
				delete sliderWrap->widgetAt(0);
			}
			while (!sliderWrap->children().isEmpty()) {
				delete sliderWrap->children().front();
			}
		}

		const auto setCustom = [=] {
			auto min = state->value.value(
			) | rpl::map([=](const Data::GiftAuctionState &state) {
				return std::max(1, int(state.my.minBidAmount
					? state.my.minBidAmount
					: state.minBidAmount));
			});
			show->show(Box(EditCustomBid, show, crl::guard(box, [=](int v) {
				state->chosen = v;
			}), std::move(min), state->chosen.current()));
		};

		const auto bubble = AddStarSelectBubble(
			sliderWrap,
			initial ? BoxShowFinishes(box) : nullptr,
			state->chosen.value(),
			values.max,
			activeFgOverride);
		bubble->setAttribute(Qt::WA_TransparentForMouseEvents, false);
		bubble->setClickedCallback(setCustom);
		state->subtext.value() | rpl::on_next([=](QString &&text) {
			bubble->setSubtext(std::move(text));
		}, bubble->lifetime());

		PaidReactionSlider(
			sliderWrap,
			st::paidReactSlider,
			values.min,
			values.explicitlyAllowed,
			state->chosen.value(),
			values.max,
			[=](int count) { state->chosen = count; },
			activeFgOverride);

		sliderWrap->resizeToWidth(st::boxWideWidth);

		const auto custom = CreateChild<AbstractButton>(sliderWrap);
		state->chosen.changes() | rpl::on_next([=] {
			custom->update();
		}, custom->lifetime());
		custom->show();
		custom->setClickedCallback(setCustom);
		custom->resize(st::paidReactSlider.width, st::paidReactSlider.width);
		custom->paintOn([=](QPainter &p) {
			const auto rem = st::paidReactSlider.borderWidth * 2;
			const auto inner = custom->width() - 2 * rem;
			const auto sub = (inner - 1) / 2;
			const auto stroke = inner - (2 * sub);
			const auto color = activeFgOverride(state->chosen.current());
			p.fillRect(rem + sub, rem, stroke, sub, color);
			p.fillRect(rem, rem + sub, inner, stroke, color);
			p.fillRect(rem + sub, rem + inner - sub, stroke, sub, color);
		});
		sliderWrap->sizeValue() | rpl::on_next([=](QSize size) {
			custom->move(
				size.width() - st::boxRowPadding.right() - custom->width(),
				size.height() - custom->height());
		}, custom->lifetime());
	}, sliderWrap->lifetime());

	box->addTopButton(
		st::boxTitleClose,
		[=] { box->closeBox(); });
	if (const auto now = state->value.current(); !now.finished()) {
		box->addTopButton(
			st::boxTitleMenu,
			MakeAuctionMenuCallback(box, show, now));
	}

	const auto skip = st::paidReactTitleSkip;
	box->addRow(
		object_ptr<FlatLabel>(
			box,
			rpl::conditional(
				state->started.value(),
				tr::lng_auction_bid_title(),
				tr::lng_auction_bid_title_early()),
			st::boostCenteredTitle),
		st::boxRowPadding + QMargins(0, skip / 2, 0, 0),
		style::al_top);

	auto subtitle = tr::lng_auction_bid_subtitle(
		lt_count,
		state->value.value(
		) | rpl::map([=](const Data::GiftAuctionState &state) {
			return state.gift->auctionGiftsPerRound * 1.;
		}));
	box->addRow(
		object_ptr<FlatLabel>(
			box,
			std::move(subtitle),
			st::auctionCenteredSubtitle),
		style::al_top);

	const auto setMinimal = [=] {
		const auto &now = state->value.current();
		state->chosen = int(now.my.minBidAmount
			? now.my.minBidAmount
			: now.minBidAmount);
	};
	box->addRow(
		MakeAuctionInfoBlocks(
			box,
			&show->session(),
			state->value.value(),
			setMinimal),
		st::boxRowPadding + QMargins(0, skip / 2, 0, skip));

	AddBidPlaces(box, show, state->value.value(), state->chosen.value());

	AddSkip(content);
	AddSkip(content);

	const auto peer = args.peer;
	const auto button = box->addButton(rpl::single(QString()), [=] {
		const auto &current = state->value.current();
		const auto amount = state->chosen.current();
		if (amount <= current.my.bid) {
			box->closeBox();
			return;
		} else if (state->placing) {
			return;
		}
		state->placing = true;
		const auto was = (current.my.bid > 0);
		const auto perRound = current.gift->auctionGiftsPerRound;
		const auto done = [=](Payments::CheckoutResult result) {
			state->placing = false;
			if (result == Payments::CheckoutResult::Paid) {
				show->showToast({
					.title = (was
						? tr::lng_auction_bid_increased_title
						: tr::lng_auction_bid_placed_title)(
							tr::now),
					.text = tr::lng_auction_bid_done_text(
						tr::now,
						lt_count,
						perRound,
						tr::rich),
					.st = &st::auctionBidToast,
					.attach = RectPart::Top,
					.duration = kBidPlacedToastDuration,
				});
			}
		};
		auto owned = details
			? std::make_unique<GiftSendDetails>(*details)
			: nullptr;
		PlaceAuctionBid(show, peer, amount, current, std::move(owned), done);
	});

	button->setText(rpl::combine(
		state->value.value(),
		state->chosen.value()
	) | rpl::map([=](const Data::GiftAuctionState &state, int count) {
		return !state.my.bid
			? tr::lng_auction_bid_place(
				lt_stars,
				rpl::single(CreditsEmojiSmall().append(
					Lang::FormatCountDecimal(count))),
				tr::marked)
			: (count <= state.my.bid)
			? tr::lng_box_ok(tr::marked)
			: tr::lng_auction_bid_increase(
				lt_stars,
				rpl::single(CreditsEmojiSmall().append(
					Lang::FormatCountDecimal(count - state.my.bid))),
				tr::marked);
	}) | rpl::flatten_latest());

	show->session().credits().load(true);
	AddStarSelectBalance(
		box,
		&show->session(),
		show->session().credits().balanceValue());
}

[[nodiscard]] object_ptr<RpWidget> MakeAveragePriceValue(
		not_null<TableLayout*> table,
		std::shared_ptr<TableRowTooltipData> tooltip,
		const QString &name,
		int64 amount) {
	auto helper = Text::CustomEmojiHelper();
	const auto price = helper.paletteDependent(Earn::IconCreditsEmoji(
	)).append(' ').append(
		Lang::FormatCreditsAmountDecimal(CreditsAmount{ amount }));
	return MakeTableValueWithTooltip(
		table,
		std::move(tooltip),
		price,
		tr::lng_auction_average_tooltip(
			tr::now,
			lt_amount,
			tr::bold(
				Text::IconEmoji(&st::starIconEmojiInline).append(
					Lang::FormatCountDecimal(amount))),
			lt_gift,
			tr::bold(name),
			tr::marked),
		helper.context());
}

[[nodiscard]] std::vector<int> RandomIndicesSubset(int total, int subset) {
	const auto take = std::min(total, subset);
	if (!take) {
		return {};
	}
	auto result = std::vector<int>();
	auto taken = base::flat_set<int>();
	result.reserve(take);
	taken.reserve(take);
	for (auto i = 0; i < take; ++i) {
		auto index = base::RandomIndex(total - i);
		for (const auto already : taken) {
			if (index >= already) {
				++index;
			} else {
				break;
			}
		}
		taken.emplace(index);
		result.push_back(index);
	}
	return result;
}

[[nodiscard]] object_ptr<TableLayout> AuctionInfoTable(
		not_null<QWidget*> parent,
		not_null<VerticalLayout*> container,
		rpl::producer<Data::GiftAuctionState> value) {
	auto result = object_ptr<TableLayout>(parent.get(), st::defaultTable);
	const auto raw = result.data();

	struct State {
		rpl::variable<Data::GiftAuctionState> value;
		rpl::variable<bool> finished;
	};
	const auto state = raw->lifetime().make_state<State>();
	state->value = std::move(value);

	const auto &now = state->value.current();
	const auto preview = (now.startDate > base::unixtime::now());
	const auto name = now.gift->resellTitle;
	state->finished = now.finished()
		? (rpl::single(true) | rpl::type_erased)
		: (MinutesLeftTillValue(now.endDate) | rpl::map(!rpl::mappers::_1));

	const auto date = [&](TimeId time) {
		return rpl::single(
			tr::marked(langDateTime(base::unixtime::parse(time))));
	};
	AddTableRow(
		raw,
		(preview
			? tr::lng_auction_starts_label()
			: rpl::conditional(
				state->finished.value(),
				tr::lng_gift_link_label_first_sale(),
				tr::lng_auction_start_label())),
		date(now.startDate));
	AddTableRow(
		raw,
		rpl::conditional(
			state->finished.value(),
			tr::lng_gift_link_label_last_sale(),
			tr::lng_auction_end_label()),
		date(now.endDate));
	if (preview) {
		AddTableRow(
			raw,
			tr::lng_gift_unique_availability_label(),
			rpl::single(tr::marked(
				Lang::FormatCountDecimal(now.gift->limitedCount))));
		AddTableRow(
			raw,
			tr::lng_auction_rounds_label(),
			rpl::single(tr::marked(
				Lang::FormatCountDecimal(now.totalRounds))));
		const auto formatDuration = [&](TimeId value, bool exact) {
			return (!(value % 3600))
				? (exact ? tr::lng_hours : tr::lng_auction_rounds_hours)(
					tr::now,
					lt_count,
					value / 3600)
				: (!(value % 60))
				? (exact ? tr::lng_minutes : tr::lng_auction_rounds_minutes)(
					tr::now,
					lt_count,
					value / 60)
				: (exact ? tr::lng_seconds : tr::lng_auction_rounds_seconds)(
					tr::now,
					lt_count,
					value);
		};
		for (auto i = 0, n = int(now.roundParameters.size()); i != n; ++i) {
			const auto &that = now.roundParameters[i];
			const auto next = (i + 1 < n)
				? now.roundParameters[i + 1]
				: Data::GiftAuctionRound{ now.totalRounds + 1 };
			const auto exact = (next.number == that.number + 1);
			const auto extended = that.extendTop && that.extendDuration;
			const auto duration = formatDuration(that.duration, exact);
			const auto value = extended
				? tr::lng_auction_rounds_extended(
					tr::now,
					lt_duration,
					duration,
					lt_increase,
					formatDuration(that.extendDuration, true),
					lt_n,
					QString::number(that.extendTop))
				: duration;
			AddTableRow(
				raw,
				(exact
					? tr::lng_auction_rounds_exact(
						lt_n,
						rpl::single(QString::number(that.number)))
					: tr::lng_auction_rounds_range(
						lt_n,
						rpl::single(QString::number(that.number)),
						lt_last,
						rpl::single(QString::number(next.number - 1)))),
				object_ptr<FlatLabel>(
					raw,
					value,
					st::auctionInfoValueMultiline));
		}
	} else {
		auto roundText = state->value.value(
		) | rpl::map([](const Data::GiftAuctionState &state) {
			const auto wrapped = [](int count) {
				return rpl::single(tr::marked(Lang::FormatCountDecimal(count)));
			};
			return tr::lng_auction_round_value(
				lt_n,
				wrapped(state.currentRound),
				lt_amount,
				wrapped(state.totalRounds),
				tr::marked);
		}) | rpl::flatten_latest();
		const auto round = AddTableRow(
			raw,
			tr::lng_auction_round_label(),
			std::move(roundText));

		auto availabilityText = state->value.value(
		) | rpl::map([](const Data::GiftAuctionState &state) {
			const auto wrapped = [](int count) {
				return rpl::single(tr::marked(Lang::FormatCountDecimal(count)));
			};
			return tr::lng_auction_availability_value(
				lt_n,
				wrapped(state.giftsLeft),
				lt_amount,
				wrapped(state.gift->limitedCount),
				tr::marked);
		}) | rpl::flatten_latest();
		AddTableRow(
			raw,
			tr::lng_auction_availability_label(),
			std::move(availabilityText));

		const auto tooltip = std::make_shared<TableRowTooltipData>(
			TableRowTooltipData{ .parent = container });
		state->value.value(
		) | rpl::map([](const Data::GiftAuctionState &state) {
			return state.averagePrice;
		}) | rpl::filter(
			rpl::mappers::_1 != 0
		) | rpl::take(
			1
		) | rpl::on_next([=](int64 price) {
			delete round;

			raw->insertRow(
				2,
				object_ptr<FlatLabel>(
					raw,
					tr::lng_auction_average_label(),
					raw->st().defaultLabel),
				MakeAveragePriceValue(raw, tooltip, name, price),
				st::giveawayGiftCodeLabelMargin,
				st::giveawayGiftCodeValueMargin);
			raw->resizeToWidth(raw->widthNoMargins());
		}, raw->lifetime());
	}
	return result;
}

void AuctionGotGiftsBox(
		not_null<GenericBox*> box,
		std::shared_ptr<ChatHelpers::Show> show,
		const Data::StarGift &gift,
		std::vector<Data::GiftAcquired> list) {
	Expects(!list.empty());

	const auto count = int(list.size());
	box->setTitle(
		tr::lng_auction_bought_title(lt_count, rpl::single(count * 1.)));
	box->setWidth(st::boxWideWidth);
	box->setMaxHeight(st::boxWideWidth * 2);
	box->addButton(tr::lng_box_ok(), [=] { box->closeBox(); });
	box->addTopButton(st::boxTitleClose, [=] { box->closeBox(); });

	auto helper = Text::CustomEmojiHelper(Core::TextContext({
		.session = &show->session(),
	}));
	const auto emoji = Data::SingleCustomEmoji(gift.document);
	const auto container = box->verticalLayout();
	ranges::sort(list, ranges::less(), &Data::GiftAcquired::round);
	for (const auto &entry : list) {
		const auto table = container->add(
			object_ptr<Ui::TableLayout>(
				container,
				st::giveawayGiftCodeTable),
			st::giveawayGiftCodeTableMargin);
		const auto addFullWidth = [&](rpl::producer<TextWithEntities> text) {
			table->addRow(
				object_ptr<Ui::FlatLabel>(
					table,
					std::move(text),
					st::giveawayGiftMessage,
					st::defaultPopupMenu,
					helper.context()),
				nullptr,
				st::giveawayGiftCodeLabelMargin,
				st::giveawayGiftCodeValueMargin);
		};

		// Title "Gift #number in round #n"
		addFullWidth(tr::lng_auction_bought_in_round(
			lt_name,
			rpl::single(tr::marked(
				emoji
			).append(' ').append(
				Data::UniqueGiftName(gift.resellTitle, entry.number)
			)),
			lt_n,
			rpl::single(tr::marked(QString::number(entry.round))),
			tr::bold));

		// Recipient
		AddTableRow(
			table,
			tr::lng_credits_box_history_entry_peer(),
			show,
			entry.to->id);

		// Date
		AddTableRow(
			table,
			tr::lng_auction_bought_date(),
			rpl::single(tr::marked(
				langDateTime(base::unixtime::parse(entry.date)))));

		// Accepted Bid
		auto accepted = helper.paletteDependent(
			Ui::Earn::IconCreditsEmoji()
		).append(
			Lang::FormatCountDecimal(entry.bidAmount)
		).append(' ').append(
			helper.paletteDependent(
				Text::CustomEmojiTextBadge(
					'#' + QString::number(entry.position),
					st::defaultTableSmallButton)));
		AddTableRow(
			table,
			tr::lng_auction_bought_bid(),
			rpl::single(accepted),
			helper.context());

		// Message
		if (!entry.message.empty()) {
			addFullWidth(rpl::single(entry.message));
		}
	}
}

[[nodiscard]] rpl::producer<UniqueGiftCover> MakePreviewAuctionStream(
		const Data::StarGift &info,
		rpl::producer<Data::UniqueGiftAttributes> attributes) {
	Expects(attributes);

	const auto cover = [](Data::UniqueGift gift) {
		return UniqueGiftCover{ std::move(gift) };
	};
	auto initial = Data::UniqueGift{
		.title = info.resellTitle,
		.model = Data::UniqueGiftModel{
			.document = info.document,
		},
		.pattern = Data::UniqueGiftPattern{
			.document = info.document,
		},
		.backdrop = (info.background
			? info.background->backdrop()
			: Data::UniqueGiftBackdrop()),
	};
	return rpl::single(cover(initial)) | rpl::then(std::move(
		attributes
	) | rpl::map([=](const Data::UniqueGiftAttributes &values)
	-> rpl::producer<UniqueGiftCover> {
		if (values.backdrops.empty()
			|| values.models.empty()
			|| values.patterns.empty()) {
			return rpl::never<UniqueGiftCover>();
		}
		return [=](auto consumer) {
			auto lifetime = rpl::lifetime();

			struct State {
				Data::UniqueGiftAttributes data;
				std::vector<int> modelIndices;
				std::vector<int> patternIndices;
				std::vector<int> backdropIndices;
			};
			const auto state = lifetime.make_state<State>(State{
				.data = values,
			});

			const auto put = [=] {
				const auto index = [](
						std::vector<int> &indices,
						const auto &v) {
					const auto fill = [&] {
						if (!indices.empty()) {
							return;
						}
						indices = ranges::views::ints(
							0
						) | ranges::views::take(
							v.size()
						) | ranges::to_vector;
						ranges::shuffle(indices);
					};
					fill();
					const auto result = indices.back();
					indices.pop_back();
					fill();
					if (indices.back() == result) {
						std::swap(indices.front(), indices.back());
					}
					return result;
				};
				auto &models = state->data.models;
				auto &patterns = state->data.patterns;
				auto &backdrops = state->data.backdrops;
				consumer.put_next(cover({
					.title = info.resellTitle,
					.model = models[index(state->modelIndices, models)],
					.pattern = patterns[index(state->patternIndices, patterns)],
					.backdrop = backdrops[index(state->backdropIndices, backdrops)],
				}));
			};

			put();
			base::timer_each(
				kSwitchPreviewCoverInterval / 3
			) | rpl::on_next(put, lifetime);

			return lifetime;
		};
	}) | rpl::flatten_latest());
}

void AuctionInfoBox(
		not_null<GenericBox*> box,
		not_null<Window::SessionController*> window,
		not_null<PeerData*> peer,
		rpl::producer<Data::GiftAuctionState> value) {
	using namespace Info::PeerGifts;

	struct State {
		explicit State(not_null<Main::Session*> session)
			: delegate(session, GiftButtonMode::Minimal) {
		}

		Delegate delegate;
		rpl::variable<Data::GiftAuctionState> value;
		rpl::variable<int> minutesTillEnd;
		rpl::variable<int> secondsTillStart;
		rpl::variable<Data::UniqueGiftAttributes> attributes;

		std::vector<Data::GiftAcquired> acquired;
		bool acquiredRequested = false;

		base::unique_qptr<PopupMenu> menu;

		rpl::lifetime previewLifetime;
		bool previewRequested = false;
	};
	const auto show = window->uiShow();
	const auto state = box->lifetime().make_state<State>(&show->session());
	state->value = std::move(value);
	const auto &now = state->value.current();
	const auto auctions = &show->session().giftAuctions();
	const auto giftId = now.gift->id;
	if (auto attributes = auctions->attributes(giftId)) {
		state->attributes = std::move(*attributes);
	}  else {
		auctions->requestAttributes(giftId, crl::guard(box, [=] {
			state->attributes.force_assign(*auctions->attributes(giftId));
		}));
	}
	state->minutesTillEnd = MinutesLeftTillValue(now.endDate);
	state->secondsTillStart = SecondsLeftTillValue(now.startDate);
	const auto started = !state->secondsTillStart.current();

	box->setStyle(st::giftBox);
	box->setNoContentMargin(true);

	const auto container = box->verticalLayout();
	auto gift = MakePreviewAuctionStream(
		*now.gift,
		state->attributes.value());
	AddUniqueGiftCover(container, std::move(gift), {
		.pretitle = started ? nullptr : tr::lng_auction_preview_name(),
		.subtitle = tr::lng_auction_preview_learn_gifts(
			lt_arrow,
			rpl::single(Text::IconEmoji(&st::textMoreIconEmoji)),
			tr::link),
		.subtitleClick = [=] {
			ShowPremiumPreviewBox(window, PremiumFeature::Gifts);
		},
		.subtitleLinkColored = true,
	});
	AddSkip(container, st::defaultVerticalListSkip * 2);

	AddUniqueCloseButton(
		box,
		{},
		now.finished() ? nullptr : MakeAuctionFillMenuCallback(show, now));

	box->addRow(
		AuctionInfoTable(box, box->verticalLayout(), state->value.value()),
		st::boxRowPadding + st::auctionInfoTableMargin);

	if (const auto got = now.my.gotCount) {
		box->addRow(
			object_ptr<FlatLabel>(
				box,
				tr::lng_auction_bought(
					lt_count_decimal,
					rpl::single(1. * got),
					lt_emoji,
					rpl::single(Data::SingleCustomEmoji(
						state->value.current().gift->document)),
					lt_arrow,
					rpl::single(Text::IconEmoji(&st::textMoreIconEmoji)),
					tr::link),
				st::uniqueGiftValueAvailableLink,
				st::defaultPopupMenu,
				Core::TextContext({ .session = &show->session() })),
			st::boxRowPadding + st::uniqueGiftValueAvailableMargin,
			style::al_top
		)->setClickHandlerFilter([=](const auto &...) {
			const auto &value = state->value.current();
			const auto &gift = *value.gift;
			if (!value.my.gotCount) {
				return false;
			} else if (state->acquired.size() == value.my.gotCount) {
				show->show(Box(
					AuctionGotGiftsBox,
					show,
					gift,
					state->acquired));
			} else if (!state->acquiredRequested) {
				state->acquiredRequested = true;
				auctions->requestAcquired(
					value.gift->id,
					crl::guard(box, [=](
							std::vector<Data::GiftAcquired> result) {
						state->acquiredRequested = false;
						state->acquired = std::move(result);
						if (!state->acquired.empty()) {
							show->show(Box(
								AuctionGotGiftsBox,
								show,
								gift,
								state->acquired));
						}
					}));
			}
			return false;
		});
	} else if (const auto variants = now.gift->upgradeVariants) {
		using namespace Data;
		state->attributes.value(
		) | rpl::filter([](const UniqueGiftAttributes &list) {
			return !list.models.empty();
		}) | rpl::take(
			1
		) | rpl::on_next([=](const UniqueGiftAttributes &list) {
			auto emoji = tr::marked();
			const auto indices = RandomIndicesSubset(list.models.size(), 3);
			for (const auto index : indices) {
				emoji.append(Data::SingleCustomEmoji(
					list.models[index].document));
			}
			box->addRow(
				object_ptr<FlatLabel>(
					box,
					tr::lng_auction_preview_variants(
						lt_count_decimal,
						rpl::single(1. * variants),
						lt_emoji,
						rpl::single(emoji),
						lt_arrow,
						rpl::single(Text::IconEmoji(&st::textMoreIconEmoji)),
						tr::link),
					st::uniqueGiftValueAvailableLink,
					st::defaultPopupMenu,
					Core::TextContext({ .session = &show->session() })),
				st::boxRowPadding + st::uniqueGiftValueAvailableMargin,
				style::al_top
			)->setClickHandlerFilter([=](const auto &...) {
				const auto title = now.gift->resellTitle;
				const auto type = Data::GiftAttributeIdType::Model;
				const auto null = nullptr;
				show->show(Box(StarGiftPreviewBox, title, list, type, null));
				return false;
			});
		}, box->lifetime());
	}
	const auto button = box->addButton(rpl::single(QString()), [=] {
		if (state->value.current().finished()
			|| !state->minutesTillEnd.current()) {
			box->closeBox();
			return;
		}
		const auto sendBox = show->show(Box(
			SendGiftBox,
			window,
			peer,
			nullptr,
			GiftTypeStars{ .info = *state->value.current().gift },
			state->value.value()));
		sendBox->boxClosing(
		) | rpl::on_next([=] {
			box->closeBox();
		}, box->lifetime());
	});

	SetAuctionButtonCountdownText(
		button,
		AuctionButtonCountdownType::Join,
		state->value.value());
}

base::weak_qptr<BoxContent> ChooseAndShowAuctionBox(
		not_null<Window::SessionController*> window,
		not_null<PeerData*> peer,
		std::shared_ptr<rpl::variable<Data::GiftAuctionState>> state,
		Fn<void()> boxClosed) {
	const auto local = &peer->session().local();
	const auto &current = state->current();
	const auto now = base::unixtime::now();
	const auto started = (current.startDate <= now);
	const auto finished = current.finished() || (current.endDate <= now);
	const auto showBidBox = current.my.bid
		&& !finished
		&& (!current.my.to || current.my.to == peer);
	const auto showChangeRecipient = !showBidBox
		&& current.my.bid
		&& !finished;
	const auto showInfoBox = !showBidBox
		&& !showChangeRecipient
		&& (!started
			|| finished
			|| local->readPref<bool>(kAuctionAboutShownPref));
	auto box = base::weak_qptr<BoxContent>();
	if (showBidBox) {
		box = window->show(MakeAuctionBidBox({
			.peer = peer,
			.show = window->uiShow(),
			.state = state->value(),
		}));
	} else if (showChangeRecipient) {
		const auto change = [=](Fn<void()> close) {
			const auto sendBox = window->show(Box(
				SendGiftBox,
				window,
				peer,
				nullptr,
				Info::PeerGifts::GiftTypeStars{
					.info = *current.gift,
				},
				state->value()));
			sendBox->boxClosing(
			) | rpl::on_next(close, sendBox->lifetime());
		};
		const auto from = current.my.to;
		const auto text = (from->isSelf()
			? tr::lng_auction_change_already_me(tr::now, tr::rich)
			: tr::lng_auction_change_already(
				tr::now,
				lt_name,
				tr::bold(from->name()),
				tr::rich)).append(' ').append(peer->isSelf()
					? tr::lng_auction_change_to_me(tr::now, tr::rich)
					: tr::lng_auction_change_to(
						tr::now,
						lt_name,
						tr::bold(peer->name()),
						tr::rich));
		box = window->show(Box([=](not_null<GenericBox*> box) {
			box->addRow(
				CreateUserpicsTransfer(
					box,
					rpl::single(std::vector{ not_null<PeerData*>(from) }),
					peer,
					UserpicsTransferType::AuctionRecipient),
				st::boxRowPadding + st::auctionChangeRecipientPadding
			)->setAttribute(Qt::WA_TransparentForMouseEvents);

			ConfirmBox(box, {
				.text = text,
				.confirmed = change,
				.confirmText = tr::lng_auction_change_button(),
				.title = tr::lng_auction_change_title(),
			});
		}));
	} else if (showInfoBox) {
		box = window->show(Box(
			AuctionInfoBox,
			window,
			peer,
			state->value()));
	} else {
		local->writePref<bool>(kAuctionAboutShownPref, true);
		const auto understood = [=](Fn<void()> close) {
			ChooseAndShowAuctionBox(window, peer, state, close);
		};
		box = window->show(Box(
			AuctionAboutBox,
			current.totalRounds,
			current.gift->auctionGiftsPerRound,
			understood));
	}
	if (const auto strong = box.get()) {
		strong->boxClosing(
		) | rpl::on_next(boxClosed, strong->lifetime());
	} else {
		boxClosed();
	}
	return box;
}

} // namespace

rpl::lifetime ShowStarGiftAuction(
		not_null<Window::SessionController*> controller,
		PeerData *peer,
		QString slug,
		Fn<void()> finishRequesting,
		Fn<void()> boxClosed) {
	const auto weak = base::make_weak(controller);
	const auto session = &controller->session();
	struct State {
		rpl::variable<Data::GiftAuctionState> value;
		base::weak_qptr<BoxContent> box;
	};
	const auto state = std::make_shared<State>();
	auto result = session->giftAuctions().state(
		slug
	) | rpl::on_next([=](Data::GiftAuctionState &&value) {
		if (const auto onstack = finishRequesting) {
			onstack();
		}
		const auto initial = !state->value.current().gift.has_value();
		const auto already = value.my.to;
		state->value = std::move(value);
		if (initial) {
			if (const auto strong = weak.get()) {
				const auto to = peer
					? peer
					: already
					? already
					: strong->session().user();
				state->box = ChooseAndShowAuctionBox(
					strong,
					to,
					std::shared_ptr<rpl::variable<Data::GiftAuctionState>>(
						state,
						&state->value),
					boxClosed);
			} else {
				boxClosed();
			}
		}
	});
	result.add([=] {
		if (const auto strong = state->box.get()) {
			strong->closeBox();
		}
	});
	return result;
}

object_ptr<BoxContent> MakeAuctionBidBox(AuctionBidBoxArgs &&args) {
	return Box(AuctionBidBox, std::move(args));
}

void SetAuctionButtonCountdownText(
		not_null<RoundButton*> button,
		AuctionButtonCountdownType type,
		rpl::producer<Data::GiftAuctionState> value) {
	struct State {
		rpl::variable<Data::GiftAuctionState> value;
		rpl::variable<int> minutesTillEnd;
		rpl::variable<int> secondsTillStart;
	};
	const auto state = button->lifetime().make_state<State>();
	state->value = std::move(value);

	const auto &now = state->value.current();
	const auto preview = (now.startDate > base::unixtime::now());
	if (preview) {
		state->secondsTillStart = SecondsLeftTillValue(now.startDate);
	} else {
		state->minutesTillEnd = MinutesLeftTillValue(now.endDate);
	}

	auto buttonTitle = rpl::combine(
		state->value.value(),
		(preview
			? state->secondsTillStart.value()
			: state->minutesTillEnd.value())
	) | rpl::map([=](const Data::GiftAuctionState &state, int leftTill) {
		return (state.finished() || (!preview && leftTill <= 0))
			? tr::lng_box_ok(tr::marked)
			: preview
			? tr::lng_auction_join_early_bid(tr::marked)
			: (type != AuctionButtonCountdownType::Place)
			? tr::lng_auction_join_button(tr::marked)
			: tr::lng_auction_join_bid(tr::marked);
	}) | rpl::flatten_latest();

	auto buttonSubtitle = rpl::combine(
		state->value.value(),
		(preview
			? state->secondsTillStart.value()
			: state->minutesTillEnd.value())
	) | rpl::map([=](
		const Data::GiftAuctionState &state,
		int leftTill
	) -> rpl::producer<TextWithEntities> {
		if (state.finished() || leftTill <= 0) {
			return rpl::single(TextWithEntities());
		} else if (preview) {
			const auto hours = (leftTill / 3600);
			const auto minutes = (leftTill % 3600) / 60;
			const auto seconds = (leftTill % 60);
			const auto time = hours
				? u"%1:%2:%3"_q
				.arg(hours).arg(minutes, 2, 10, QChar('0'))
				.arg(seconds, 2, 10, QChar('0'))
				: u"%1:%2"_q.arg(minutes).arg(seconds, 2, 10, QChar('0'));
			return tr::lng_auction_join_starts_in(
				lt_time,
				rpl::single(tr::marked(time)),
				tr::marked);
		}
		const auto hours = (leftTill / 60);
		const auto minutes = leftTill % 60;

		auto value = [](int count) {
			return rpl::single(tr::marked(QString::number(count)));
		};
		return tr::lng_auction_join_time_left(
			lt_time,
			(hours
				? tr::lng_auction_join_time_medium(
					lt_hours,
					value(hours),
					lt_minutes,
					value(minutes),
					tr::marked)
				: tr::lng_auction_join_time_small(
					lt_minutes,
					value(minutes),
					tr::marked)),
			tr::marked);
	}) | rpl::flatten_latest();

	SetButtonTwoLabels(
		button,
		std::move(buttonTitle),
		std::move(buttonSubtitle),
		st::resaleButtonTitle,
		st::resaleButtonSubtitle);
}

void AuctionAboutBox(
		not_null<GenericBox*> box,
		int rounds,
		int giftsPerRound,
		Fn<void(Fn<void()> close)> understood) {
	box->setStyle(st::confcallJoinBox);
	box->setWidth(st::boxWideWidth);
	box->setNoContentMargin(true);
	box->addTopButton(st::boxTitleClose, [=] {
		box->closeBox();
	});
	box->addRow(
		Calls::Group::MakeRoundActiveLogo(
			box,
			st::auctionAboutLogo,
			st::auctionAboutLogoPadding),
		st::boxRowPadding + st::confcallLinkHeaderIconPadding);

	box->addRow(
		object_ptr<FlatLabel>(
			box,
			tr::lng_auction_about_title(),
			st::boxTitle),
		st::boxRowPadding,
		style::al_top);
	box->addRow(
		object_ptr<FlatLabel>(
			box,
			tr::lng_auction_about_subtitle(tr::rich),
			st::confcallLinkCenteredText),
		st::boxRowPadding + st::auctionAboutTextPadding,
		style::al_top
	)->setTryMakeSimilarLines(true);

	const auto features = std::vector<FeatureListEntry>{
		{
			st::menuIconAuctionDrop,
			tr::lng_auction_about_top_title(
				tr::now,
				lt_count,
				giftsPerRound),
			tr::lng_auction_about_top_about(
				tr::now,
				lt_count,
				giftsPerRound,
				lt_rounds,
				tr::lng_auction_about_top_rounds(
					tr::now,
					lt_count,
					rounds,
					tr::rich),
				lt_bidders,
				tr::lng_auction_about_top_bidders(
					tr::now,
					lt_count,
					giftsPerRound,
					tr::rich),
				tr::rich),
		},
		{
			st::menuIconStarsCarryover,
			tr::lng_auction_about_bid_title(tr::now),
			tr::lng_auction_about_bid_about(
				tr::now,
				lt_count,
				giftsPerRound,
				tr::rich),
		},
		{
			st::menuIconStarsRefund,
			tr::lng_auction_about_missed_title(tr::now),
			tr::lng_auction_about_missed_about(tr::now, tr::rich),
		},
	};
	for (const auto &feature : features) {
		box->addRow(MakeFeatureListEntry(box, feature));
	}

	const auto close = Fn<void()>([weak = base::make_weak(box)] {
		if (const auto strong = weak.get()) {
			strong->closeBox();
		}
	});
	box->addButton(
		rpl::single(QString()),
		understood ? [=] { understood(close); } : close
	)->setText(rpl::single(Text::IconEmoji(
		&st::infoStarsUnderstood
	).append(' ').append(tr::lng_auction_about_understood(tr::now))));
}

TextWithEntities ActiveAuctionsTitle(const Data::ActiveAuctions &auctions) {
	const auto &list = auctions.list;
	if (list.size() == 1) {
		const auto auction = list.front();
		return Data::SingleCustomEmoji(
			auction->gift->document
		).append(' ').append(tr::lng_auction_bar_active(tr::now));
	}
	auto result = tr::marked();
	for (const auto auction : list | ranges::views::take(3)) {
		result.append(Data::SingleCustomEmoji(auction->gift->document));
	}
	return result.append(' ').append(
		tr::lng_auction_bar_active_many(tr::now, lt_count, list.size()));
}

ManyAuctionsState ActiveAuctionsState(const Data::ActiveAuctions &auctions) {
	const auto &list = auctions.list;
	const auto winning = [](not_null<Data::GiftAuctionState*> auction) {
		const auto position = MyAuctionPosition(*auction);
		return (position <= auction->gift->auctionGiftsPerRound)
			? position
			: 0;
	};
	if (list.size() == 1) {
		const auto auction = list.front();
		const auto position = winning(auction);
		auto text = position
			? tr::lng_auction_bar_winning(
				tr::now,
				lt_count,
				position,
				tr::marked)
			: tr::lng_auction_bar_outbid(tr::now, tr::marked);
		return { std::move(text), !position };
	}
	auto outbid = 0;
	for (const auto auction : list) {
		if (!winning(auction)) {
			++outbid;
		}
	}
	auto text = (outbid == list.size())
		? tr::lng_auction_bar_outbid_all(tr::now, tr::marked)
		: outbid
		? tr::lng_auction_bar_outbid_some(
			tr::now,
			lt_count,
			outbid,
			tr::marked)
		: tr::lng_auction_bar_winning_all(tr::now, tr::marked);
	return { std::move(text), outbid != 0 };
}

rpl::producer<TextWithEntities> ActiveAuctionsButton(
		const Data::ActiveAuctions &auctions) {
	const auto &list = auctions.list;
	const auto withIcon = [](const QString &text) {
		using namespace Ui::Text;
		return IconEmoji(&st::auctionBidEmoji).append(' ').append(text);
	};
	if (list.size() == 1) {
		const auto auction = auctions.list.front();
		const auto end = auction->nextRoundAt
			? auction->nextRoundAt
			: auction->endDate;
		return SecondsLeftTillValue(end)
			| rpl::map(NiceCountdownText)
			| rpl::map(withIcon);
	}
	return tr::lng_auction_bar_view() | rpl::map(withIcon);
}

struct Single {
	QString slug;
	not_null<DocumentData*> document;
	int round = 0;
	int total = 0;
	int bid = 0;
	int position = 0;
	int winning = 0;
	TimeId ends = 0;
};

object_ptr<Ui::RpWidget> MakeActiveAuctionRow(
		not_null<QWidget*> parent,
		not_null<Window::SessionController*> window,
		not_null<DocumentData*> document,
		const QString &slug,
		rpl::producer<Single> value) {
	auto result = object_ptr<Ui::VerticalLayout>(parent);
	const auto raw = result.data();

	raw->add(object_ptr<Ui::RpWidget>(raw));

	auto title = rpl::duplicate(value) | rpl::map([=](const Single &fields) {
		return tr::lng_auction_bar_round(
			tr::now,
			lt_n,
			QString::number(fields.round + 1),
			lt_amount,
			QString::number(fields.total));
	});
	raw->add(
		object_ptr<Ui::FlatLabel>(
			raw,
			std::move(title),
			st::auctionListTitle),
		st::auctionListTitlePadding);

	const auto tag = Data::CustomEmojiSizeTag::Isolated;
	const auto sticker = std::shared_ptr<Ui::Text::CustomEmoji>(
		document->owner().customEmojiManager().create(
			document,
			[=] { raw->update(); },
			tag));

	raw->paintRequest(
	) | rpl::on_next([=] {
		auto q = QPainter(raw);
		sticker->paint(q, {
			.textColor = st::windowFg->c,
			.now = crl::now(),
			.position = QPoint(),
		});
	}, raw->lifetime());

	auto helper = Ui::Text::CustomEmojiHelper();
	const auto star = helper.paletteDependent(Ui::Earn::IconCreditsEmoji());
	auto text = rpl::duplicate(value) | rpl::map([=](const Single &fields) {
		const auto stars = tr::marked(star).append(' ').append(
			Lang::FormatCountDecimal(fields.bid));
		const auto outbid = (fields.position > fields.winning);
		return outbid
			? tr::lng_auction_bar_bid_outbid(
				tr::now,
				lt_stars,
				stars,
				tr::rich)
			: tr::lng_auction_bar_bid_ranked(
				tr::now,
				lt_stars,
				stars,
				lt_n,
				tr::marked(QString::number(fields.position)),
				tr::rich);
	});
	const auto subtitle = raw->add(
		object_ptr<Ui::FlatLabel>(
			raw,
			std::move(text),
			st::auctionListText,
			st::defaultPopupMenu,
			helper.context()),
		st::auctionListTextPadding);
	rpl::duplicate(value) | rpl::on_next([=](const Single &fields) {
		const auto outbid = (fields.position > fields.winning);
		subtitle->setTextColorOverride(outbid
			? st::attentionButtonFg->c
			: std::optional<QColor>());
	}, subtitle->lifetime());

	const auto button = raw->add(
		object_ptr<Ui::RoundButton>(
			raw,
			rpl::single(QString()),
			st::auctionListRaise),
		st::auctionListRaisePadding);
	button->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);

	auto secondsLeft = rpl::duplicate(
		value
	) | rpl::map([=](const Single &fields) {
		return SecondsLeftTillValue(fields.ends);
	}) | rpl::flatten_latest();
	button->setText(rpl::combine(
		std::move(secondsLeft),
		tr::lng_auction_bar_raise_bid()
	) | rpl::map([=](int seconds, const QString &text) {
		return Ui::Text::IconEmoji(
			&st::auctionBidEmoji
		).append(' ').append(text).append(' ').append(
			Ui::Text::Colorized(NiceCountdownText(seconds)));
	}));
	button->setClickedCallback([=] {
		window->showStarGiftAuction(slug);
	});
	button->setFullRadius(true);
	raw->widthValue() | rpl::on_next([=](int width) {
		button->setFullWidth(width);
	}, button->lifetime());

	return result;
}

Fn<void()> ActiveAuctionsCallback(
		not_null<Window::SessionController*> window,
		const Data::ActiveAuctions &auctions) {
	const auto &list = auctions.list;
	const auto count = int(list.size());
	if (count == 1) {
		const auto slug = list.front()->gift->auctionSlug;
		return [=] {
			window->showStarGiftAuction(slug);
		};
	}
	struct Auctions {
		std::vector<rpl::variable<Single>> list;
	};
	const auto state = std::make_shared<Auctions>();
	const auto singleFrom = [](const Data::GiftAuctionState &state) {
		return Single{
			.slug = state.gift->auctionSlug,
			.document = state.gift->document,
			.round = state.currentRound,
			.total = state.totalRounds,
			.bid = int(state.my.bid),
			.position = MyAuctionPosition(state),
			.winning = state.gift->auctionGiftsPerRound,
			.ends = state.nextRoundAt ? state.nextRoundAt : state.endDate,
		};
	};
	for (const auto auction : list) {
		state->list.push_back(singleFrom(*auction));
	}
	return [=] {
		window->show(Box([=](not_null<Ui::GenericBox*> box) {
			const auto rows = box->lifetime().make_state<
				rpl::variable<int>
			>(count);

			box->setWidth(st::boxWideWidth);
			box->setTitle(tr::lng_auction_bar_active_many(
				lt_count,
				rows->value() | tr::to_count()));

			const auto auctions = &window->session().giftAuctions();
			for (auto &entry : state->list) {
				using Data::GiftAuctionState;

				const auto &now = entry.current();
				entry = auctions->state(
					now.slug
				) | rpl::filter([=](const GiftAuctionState &state) {
					return state.my.bid != 0;
				}) | rpl::map(singleFrom);

				const auto skip = st::auctionListEntrySkip;
				const auto row = box->addRow(
					MakeActiveAuctionRow(
						box,
						window,
						now.document,
						now.slug,
						entry.value()),
					st::boxRowPadding + QMargins(0, skip, 0, skip));

				auctions->state(
					now.slug
				) | rpl::on_next([=](const GiftAuctionState &state) {
					if (!state.my.bid) {
						delete row;
						if (const auto now = rows->current(); now > 1) {
							*rows = (now - 1);
						} else {
							box->closeBox();
						}
					}
				}, row->lifetime());
			}

			box->addTopButton(st::boxTitleClose, [=] { box->closeBox(); });
			box->addButton(tr::lng_box_ok(), [=] { box->closeBox(); });
		}));
	};
}

} // namespace Ui
