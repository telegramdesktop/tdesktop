/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/boosts/info_boosts_inner_widget.h"

#include "api/api_premium.h"
#include "api/api_statistics.h"
#include "boxes/gift_premium_box.h"
#include "boxes/peers/edit_peer_invite_link.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "info/boosts/create_giveaway_box.h"
#include "info/boosts/giveaway/boost_badge.h"
#include "info/boosts/giveaway/giveaway_type_row.h"
#include "info/boosts/info_boosts_widget.h"
#include "info/info_controller.h"
#include "info/profile/info_profile_icon.h"
#include "info/statistics/info_statistics_inner_widget.h" // FillLoading.
#include "info/statistics/info_statistics_list_controllers.h"
#include "lang/lang_keys.h"
#include "settings/settings_common.h"
#include "statistics/widgets/chart_header_widget.h"
#include "ui/boxes/boost_box.h"
#include "ui/controls/invite_link_label.h"
#include "ui/effects/ripple_animation.h"
#include "ui/empty_userpic.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/discrete_sliders.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "styles/style_giveaway.h"
#include "styles/style_info.h"
#include "styles/style_statistics.h"

#include <QtGui/QGuiApplication>

namespace Info::Boosts {
namespace {

void AddHeader(
		not_null<Ui::VerticalLayout*> content,
		tr::phrase<> text) {
	const auto header = content->add(
		object_ptr<Statistic::Header>(content),
		st::statisticsLayerMargins + st::boostsChartHeaderPadding);
	header->resizeToWidth(header->width());
	header->setTitle(text(tr::now));
	header->setSubTitle({});
}

void FillOverview(
		not_null<Ui::VerticalLayout*> content,
		const Data::BoostStatus &status) {
	const auto &stats = status.overview;

	::Settings::AddSkip(content, st::boostsLayerOverviewMargins.top());
	AddHeader(content, tr::lng_stats_overview_title);
	::Settings::AddSkip(content);

	const auto diffBetweenHeaders = 0
		+ st::statisticsOverviewValue.style.font->height
		- st::statisticsHeaderTitleTextStyle.font->height;

	const auto container = content->add(
		object_ptr<Ui::RpWidget>(content),
		st::statisticsLayerMargins);

	const auto addPrimary = [&](float64 v, bool approximately = false) {
		return Ui::CreateChild<Ui::FlatLabel>(
			container,
			(v >= 0)
				? (approximately && v ? QChar(0x2248) : QChar())
					+ Lang::FormatCountToShort(v).string
				: QString(),
			st::statisticsOverviewValue);
	};
	const auto addSub = [&](
			not_null<Ui::RpWidget*> primary,
			float64 percentage,
			tr::phrase<> text) {
		const auto second = Ui::CreateChild<Ui::FlatLabel>(
			container,
			percentage
				? u"%1%"_q.arg(std::abs(std::round(percentage * 10.) / 10.))
				: QString(),
			st::statisticsOverviewSecondValue);
		second->setTextColorOverride(st::windowSubTextFg->c);
		const auto sub = Ui::CreateChild<Ui::FlatLabel>(
			container,
			text(),
			st::statisticsOverviewSubtext);
		sub->setTextColorOverride(st::windowSubTextFg->c);

		primary->geometryValue(
		) | rpl::start_with_next([=](const QRect &g) {
			const auto &padding = st::statisticsOverviewSecondValuePadding;
			second->moveToLeft(
				rect::right(g) + padding.left(),
				g.y() + padding.top());
			sub->moveToLeft(
				g.x(),
				st::statisticsChartHeaderHeight
					- st::statisticsOverviewSubtext.style.font->height
					+ g.y()
					+ diffBetweenHeaders);
		}, primary->lifetime());
	};


	const auto topLeftLabel = addPrimary(stats.level);
	const auto topRightLabel = addPrimary(stats.premiumMemberCount, true);
	const auto bottomLeftLabel = addPrimary(stats.boostCount);
	const auto bottomRightLabel = addPrimary(std::max(
		stats.nextLevelBoostCount - stats.boostCount,
		0));

	addSub(
		topLeftLabel,
		0,
		tr::lng_boosts_level);
	addSub(
		topRightLabel,
		stats.premiumMemberPercentage,
		tr::lng_boosts_premium_audience);
	addSub(
		bottomLeftLabel,
		0,
		tr::lng_boosts_existing);
	addSub(
		bottomRightLabel,
		0,
		tr::lng_boosts_next_level);

	container->showChildren();
	container->resize(container->width(), topLeftLabel->height() * 5);
	container->sizeValue(
	) | rpl::start_with_next([=](const QSize &s) {
		const auto halfWidth = s.width() / 2;
		{
			const auto &p = st::boostsOverviewValuePadding;
			topLeftLabel->moveToLeft(p.left(), p.top());
		}
		topRightLabel->moveToLeft(
			topLeftLabel->x() + halfWidth + st::statisticsOverviewRightSkip,
			topLeftLabel->y());
		bottomLeftLabel->moveToLeft(
			topLeftLabel->x(),
			topLeftLabel->y() + st::statisticsOverviewMidSkip);
		bottomRightLabel->moveToLeft(
			topRightLabel->x(),
			bottomLeftLabel->y());
	}, container->lifetime());
	::Settings::AddSkip(content, st::boostsLayerOverviewMargins.bottom());
}

void FillShareLink(
		not_null<Ui::VerticalLayout*> content,
		std::shared_ptr<Ui::Show> show,
		const QString &link,
		not_null<PeerData*> peer) {
	const auto weak = Ui::MakeWeak(content);
	const auto copyLink = crl::guard(weak, [=] {
		QGuiApplication::clipboard()->setText(link);
		show->showToast(tr::lng_channel_public_link_copied(tr::now));
	});
	const auto shareLink = crl::guard(weak, [=] {
		show->showBox(ShareInviteLinkBox(peer, link));
	});

	const auto label = content->lifetime().make_state<Ui::InviteLinkLabel>(
		content,
		rpl::single(link),
		nullptr);
	content->add(
		label->take(),
		st::boostsLinkFieldPadding);

	label->clicks(
	) | rpl::start_with_next(copyLink, label->lifetime());
	const auto copyShareWrap = content->add(
		object_ptr<Ui::VerticalLayout>(content));
	{
		const auto wrap = content->add(
			object_ptr<Ui::FixedHeightWidget>(
				content,
				st::inviteLinkButton.height),
			st::inviteLinkButtonsPadding);
		const auto copy = CreateChild<Ui::RoundButton>(
			wrap,
			tr::lng_group_invite_context_copy(),
			st::inviteLinkCopy);
		copy->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
		copy->setClickedCallback(copyLink);
		const auto share = CreateChild<Ui::RoundButton>(
			wrap,
			tr::lng_group_invite_context_share(),
			st::inviteLinkShare);
		share->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
		share->setClickedCallback(shareLink);

		wrap->widthValue(
		) | rpl::start_with_next([=](int width) {
			const auto buttonWidth = (width - st::inviteLinkButtonsSkip) / 2;
			copy->setFullWidth(buttonWidth);
			share->setFullWidth(buttonWidth);
			copy->moveToLeft(0, 0, width);
			share->moveToRight(0, 0, width);
		}, wrap->lifetime());
		wrap->showChildren();
	}
	::Settings::AddSkip(content, st::boostsLinkFieldPadding.bottom());
}

void FillGetBoostsButton(
		not_null<Ui::VerticalLayout*> content,
		not_null<Controller*> controller,
		std::shared_ptr<Ui::Show> show,
		not_null<PeerData*> peer,
		Fn<void()> reloadOnDone) {
	if (!Api::PremiumGiftCodeOptions(peer).giveawayGiftsPurchaseAvailable()) {
		return;
	}
	::Settings::AddSkip(content);
	const auto &st = st::getBoostsButton;
	const auto &icon = st::getBoostsButtonIcon;
	const auto button = content->add(
		::Settings::CreateButton(
			content.get(),
			tr::lng_boosts_get_boosts(),
			st));
	button->setClickedCallback([=] {
		show->showBox(Box(
			CreateGiveawayBox,
			controller,
			peer,
			reloadOnDone,
			std::nullopt));
	});
	Ui::CreateChild<Info::Profile::FloatingIcon>(
		button,
		icon,
		QPoint{
			st::infoSharedMediaButtonIconPosition.x(),
			(st.height + rect::m::sum::v(st.padding) - icon.height()) / 2,
		})->show();
	::Settings::AddSkip(content);
	::Settings::AddDividerText(content, tr::lng_boosts_get_boosts_subtext());
}

} // namespace

InnerWidget::InnerWidget(
	QWidget *parent,
	not_null<Controller*> controller,
	not_null<PeerData*> peer)
: VerticalLayout(parent)
, _controller(controller)
, _peer(peer)
, _show(controller->uiShow()) {
}

void InnerWidget::load() {
	const auto api = lifetime().make_state<Api::Boosts>(_peer);

	Info::Statistics::FillLoading(
		this,
		_loaded.events_starting_with(false) | rpl::map(!rpl::mappers::_1),
		_showFinished.events());

	_showFinished.events(
	) | rpl::take(1) | rpl::start_with_next([=] {
		api->request(
		) | rpl::start_with_error_done([](const QString &error) {
		}, [=] {
			_state = api->boostStatus();
			_loaded.fire(true);
			fill();
		}, lifetime());
	}, lifetime());
}

void InnerWidget::fill() {
	const auto fakeShowed = lifetime().make_state<rpl::event_stream<>>();
	const auto &status = _state;
	const auto inner = this;

	const auto reloadOnDone = crl::guard(this, [=] {
		while (Ui::VerticalLayout::count()) {
			delete Ui::VerticalLayout::widgetAt(0);
		}
		load();
	});

	{
		auto dividerContent = object_ptr<Ui::VerticalLayout>(inner);
		Ui::FillBoostLimit(
			fakeShowed->events(),
			dividerContent.data(),
			rpl::single(Ui::BoostCounters{
				.level = status.overview.level,
				.boosts = status.overview.boostCount,
				.thisLevelBoosts
					= status.overview.currentLevelBoostCount,
				.nextLevelBoosts
					= status.overview.nextLevelBoostCount,
				.mine = status.overview.mine,
			}),
			st::statisticsLimitsLinePadding);
		inner->add(object_ptr<Ui::DividerLabel>(
			inner,
			std::move(dividerContent),
			st::statisticsLimitsDividerPadding));
	}

	FillOverview(inner, status);

	::Settings::AddSkip(inner);
	::Settings::AddDivider(inner);
	::Settings::AddSkip(inner);

	if (!status.prepaidGiveaway.empty()) {
		const auto multiplier = Api::PremiumGiftCodeOptions(_peer)
			.giveawayBoostsPerPremium();
		::Settings::AddSkip(inner);
		AddHeader(inner, tr::lng_boosts_prepaid_giveaway_title);
		::Settings::AddSkip(inner);
		for (const auto &g : status.prepaidGiveaway) {
			using namespace Giveaway;
			const auto button = inner->add(object_ptr<GiveawayTypeRow>(
				inner,
				GiveawayTypeRow::Type::Prepaid,
				g.id,
				tr::lng_boosts_prepaid_giveaway_quantity(
					lt_count,
					rpl::single(g.quantity) | tr::to_count()),
				tr::lng_boosts_prepaid_giveaway_moths(
					lt_count,
					rpl::single(g.months) | tr::to_count()),
				Info::Statistics::CreateBadge(
					st::statisticsDetailsBottomCaptionStyle,
					QString::number(g.quantity * multiplier),
					st::boostsListBadgeHeight,
					st::boostsListBadgeTextPadding,
					st::premiumButtonBg2,
					st::premiumButtonFg,
					1.,
					st::boostsListMiniIconPadding,
					st::boostsListMiniIcon)));
			button->setClickedCallback([=] {
				_controller->uiShow()->showBox(Box(
					CreateGiveawayBox,
					_controller,
					_peer,
					reloadOnDone,
					g));
			});
		}

		::Settings::AddSkip(inner);
		::Settings::AddDivider(inner);
		::Settings::AddSkip(inner);
	}

	const auto hasBoosts = (status.firstSliceBoosts.multipliedTotal > 0);
	const auto hasGifts = (status.firstSliceGifts.multipliedTotal > 0);
	if (hasBoosts || hasGifts) {
		auto boostClicked = [=](const Data::Boost &boost) {
			if (!boost.giftCodeLink.slug.isEmpty()) {
				ResolveGiftCode(_controller, boost.giftCodeLink.slug);
			} else if (boost.userId) {
				const auto user = _peer->owner().user(boost.userId);
				if (boost.isGift || boost.isGiveaway) {
					const auto d = Api::GiftCode{
						.from = _peer->id,
						.to = user->id,
						.date = TimeId(boost.date.toSecsSinceEpoch()),
						.months = boost.expiresAfterMonths,
					};
					_show->showBox(Box(GiftCodePendingBox, _controller, d));
				} else {
					crl::on_main(this, [=] {
						_controller->showPeerInfo(user);
					});
				}
			} else if (!boost.isUnclaimed) {
				_show->showToast(tr::lng_boosts_list_pending_about(tr::now));
			}
		};

#ifdef _DEBUG
		const auto hasOneTab = false;
#else
		const auto hasOneTab = (hasBoosts != hasGifts);
#endif
		const auto boostsTabText = tr::lng_boosts_list_title(
			tr::now,
			lt_count,
			status.firstSliceBoosts.multipliedTotal);
		const auto giftsTabText = tr::lng_boosts_list_tab_gifts(
			tr::now,
			lt_count,
			status.firstSliceGifts.multipliedTotal);
		if (hasOneTab) {
			::Settings::AddSkip(inner);
			const auto header = inner->add(
				object_ptr<Statistic::Header>(inner),
				st::statisticsLayerMargins
					+ st::boostsChartHeaderPadding);
			header->resizeToWidth(header->width());
			header->setTitle(hasBoosts ? boostsTabText : giftsTabText);
			header->setSubTitle({});
		}

		class Slider final : public Ui::SettingsSlider {
		public:
			using Ui::SettingsSlider::SettingsSlider;
			void setNaturalWidth(int w) {
				_naturalWidth = w;
			}
			int naturalWidth() const override {
				return _naturalWidth;
			}

		private:
			int _naturalWidth = 0;

		};

		const auto slider = inner->add(
			object_ptr<Ui::SlideWrap<Slider>>(
				inner,
				object_ptr<Slider>(inner, st::defaultTabsSlider)),
			st::boxRowPadding);
		slider->toggle(!hasOneTab, anim::type::instant);

		slider->entity()->addSection(boostsTabText);
		slider->entity()->addSection(giftsTabText);

		{
			const auto &st = st::defaultTabsSlider;
			slider->entity()->setNaturalWidth(0
				+ st.labelStyle.font->width(boostsTabText)
				+ st.labelStyle.font->width(giftsTabText)
				+ rect::m::sum::h(st::boxRowPadding));
		}

		const auto boostsWrap = inner->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				inner,
				object_ptr<Ui::VerticalLayout>(inner)));
		const auto giftsWrap = inner->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				inner,
				object_ptr<Ui::VerticalLayout>(inner)));

		rpl::single(hasOneTab ? (hasGifts ? 1 : 0) : 0) | rpl::then(
			slider->entity()->sectionActivated()
		) | rpl::start_with_next([=](int index) {
			boostsWrap->toggle(!index, anim::type::instant);
			giftsWrap->toggle(index, anim::type::instant);
		}, inner->lifetime());

		Statistics::AddBoostsList(
			status.firstSliceBoosts,
			boostsWrap->entity(),
			boostClicked,
			_peer,
			tr::lng_boosts_title());
		Statistics::AddBoostsList(
			status.firstSliceGifts,
			giftsWrap->entity(),
			std::move(boostClicked),
			_peer,
			tr::lng_boosts_title());

		::Settings::AddSkip(inner);
		::Settings::AddSkip(inner);
		::Settings::AddDividerText(inner, tr::lng_boosts_list_subtext());
	}

	::Settings::AddSkip(inner);
	::Settings::AddSkip(inner);
	AddHeader(inner, tr::lng_boosts_link_title);
	::Settings::AddSkip(inner, st::boostsLinkSkip);
	FillShareLink(inner, _show, status.link, _peer);
	::Settings::AddSkip(inner);
	::Settings::AddDividerText(inner, tr::lng_boosts_link_subtext());

	FillGetBoostsButton(inner, _controller, _show, _peer, reloadOnDone);

	resizeToWidth(width());
	crl::on_main(this, [=]{ fakeShowed->fire({}); });
}

void InnerWidget::saveState(not_null<Memento*> memento) {
	memento->setState(base::take(_state));
}

void InnerWidget::restoreState(not_null<Memento*> memento) {
	_state = memento->state();
	if (!_state.link.isEmpty()) {
		fill();
	} else {
		load();
	}
	Ui::RpWidget::resizeToWidth(width());
}

rpl::producer<Ui::ScrollToRequest> InnerWidget::scrollToRequests() const {
	return _scrollToRequests.events();
}

auto InnerWidget::showRequests() const -> rpl::producer<ShowRequest> {
	return _showRequests.events();
}

void InnerWidget::showFinished() {
	_showFinished.fire({});
}

not_null<PeerData*> InnerWidget::peer() const {
	return _peer;
}

} // namespace Info::Boosts

