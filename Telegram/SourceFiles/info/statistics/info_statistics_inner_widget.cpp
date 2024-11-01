/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/statistics/info_statistics_inner_widget.h"

#include "api/api_statistics.h"
#include "apiwrap.h"
#include "base/call_delayed.h"
#include "base/event_filter.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_stories.h"
#include "data/data_story.h"
#include "history/history_item.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "info/statistics/info_statistics_list_controllers.h"
#include "info/statistics/info_statistics_recent_message.h"
#include "info/statistics/info_statistics_widget.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "main/main_session.h"
#include "settings/settings_common.h" // CreateLottieIcon.
#include "statistics/chart_widget.h"
#include "statistics/statistics_common.h"
#include "statistics/statistics_format_values.h"
#include "statistics/widgets/chart_header_widget.h"
#include "ui/layers/generic_box.h"
#include "ui/rect.h"
#include "ui/vertical_list.h"
#include "ui/toast/toast.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/slide_wrap.h"
#include "styles/style_boxes.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_statistics.h"

namespace Info::Statistics {
namespace {

struct Descriptor final {
	not_null<PeerData*> peer;
	not_null<Api::Statistics*> api;
	not_null<QWidget*> toastParent;
};

void AddContextMenu(
		not_null<Ui::RpWidget*> button,
		not_null<Controller*> controller,
		not_null<HistoryItem*> item) {
	const auto fullId = item->fullId();
	const auto contextMenu = button->lifetime()
		.make_state<base::unique_qptr<Ui::PopupMenu>>();
	const auto showMenu = [=] {
		*contextMenu = base::make_unique_q<Ui::PopupMenu>(
			button,
			st::popupMenuWithIcons);
		const auto go = [=] {
			const auto &session = controller->parentController();
			if (const auto item = session->session().data().message(fullId)) {
				session->showMessage(item);
			}
		};
		contextMenu->get()->addAction(
			tr::lng_context_to_msg(tr::now),
			crl::guard(controller, go),
			&st::menuIconShowInChat);
		contextMenu->get()->popup(QCursor::pos());
	};

	base::install_event_filter(button, [=](not_null<QEvent*> e) {
		if (e->type() == QEvent::ContextMenu) {
			showMenu();
			return base::EventFilterResult::Cancel;
		}
		return base::EventFilterResult::Continue;
	});
}

void ProcessZoom(
		const Descriptor &d,
		not_null<Statistic::ChartWidget*> widget,
		const QString &zoomToken,
		Statistic::ChartViewType type) {
	if (zoomToken.isEmpty()) {
		return;
	}
	widget->zoomRequests(
	) | rpl::start_with_next([=](float64 x) {
		d.api->requestZoom(
			zoomToken,
			x
		) | rpl::start_with_next_error_done([=](
				const Data::StatisticalGraph &graph) {
			if (graph.chart) {
				widget->setZoomedChartData(graph.chart, x, type);
			} else if (!graph.error.isEmpty()) {
				Ui::Toast::Show(d.toastParent, graph.error);
			}
		}, [=](const QString &error) {
		}, [=] {
		}, widget->lifetime());
	}, widget->lifetime());
}

void FillStatistic(
		not_null<Ui::VerticalLayout*> content,
		const Descriptor &descriptor,
		Data::AnyStatistics &stats,
		Fn<void()> done) {
	using Type = Statistic::ChartViewType;
	const auto &padding = st::statisticsChartEntryPadding;
	const auto &m = st::statisticsLayerMargins;
	const auto addSkip = [&](not_null<Ui::VerticalLayout*> c) {
		Ui::AddSkip(c, padding.bottom());
		Ui::AddDivider(c);
		Ui::AddSkip(c, padding.top());
	};
	struct State final {
		Fn<void()> done;
		int pendingCount = 0;
	};
	const auto state = content->lifetime().make_state<State>(
		State{ std::move(done) });

	const auto singlePendingDone = [=] {
		state->pendingCount--;
		if (!state->pendingCount && state->done) {
			base::take(state->done)();
		}
	};

	const auto addChart = [&](
			Data::StatisticalGraph &graphData,
			rpl::producer<QString> &&title,
			Statistic::ChartViewType type) {
		if (graphData.chart) {
			const auto widget = content->add(
				object_ptr<Statistic::ChartWidget>(content),
				m);

			widget->setChartData(graphData.chart, type);
			ProcessZoom(descriptor, widget, graphData.zoomToken, type);
			widget->setTitle(std::move(title));

			addSkip(content);
		} else if (!graphData.zoomToken.isEmpty()) {
			state->pendingCount++;
			const auto wrap = content->add(
				object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
					content,
					object_ptr<Ui::VerticalLayout>(content)));
			wrap->toggle(false, anim::type::instant);
			const auto widget = wrap->entity()->add(
				object_ptr<Statistic::ChartWidget>(content),
				m);

			descriptor.api->requestZoom(
				graphData.zoomToken,
				0
			) | rpl::start_with_next_error_done([=, graphPtr = &graphData](
					const Data::StatisticalGraph &graph) mutable {
				{
					// Save the loaded async data to cache.
					// Guarded by content->lifetime().
					*graphPtr = graph;
				}

				if (graph.chart) {
					widget->setChartData(graph.chart, type);
					wrap->toggle(true, anim::type::instant);
					ProcessZoom(descriptor, widget, graph.zoomToken, type);
					widget->setTitle(rpl::duplicate(title));
				} else if (!graph.error.isEmpty()) {
				}
			}, [=](const QString &error) {
				singlePendingDone();
			}, [=] {
				singlePendingDone();
			}, content->lifetime());

			addSkip(wrap->entity());
		}
	};
	addSkip(content);
	if (stats.channel) {
		addChart(
			stats.channel.memberCountGraph,
			tr::lng_chart_title_member_count(),
			Type::Linear);
		addChart(
			stats.channel.joinGraph,
			tr::lng_chart_title_join(),
			Type::Linear);
		addChart(
			stats.channel.muteGraph,
			tr::lng_chart_title_mute(),
			Type::Linear);
		addChart(
			stats.channel.viewCountByHourGraph,
			tr::lng_chart_title_view_count_by_hour(),
			Type::Linear);
		addChart(
			stats.channel.viewCountBySourceGraph,
			tr::lng_chart_title_view_count_by_source(),
			Type::StackBar);
		addChart(
			stats.channel.joinBySourceGraph,
			tr::lng_chart_title_join_by_source(),
			Type::StackBar);
		addChart(
			stats.channel.languageGraph,
			tr::lng_chart_title_language(),
			Type::StackLinear);
		addChart(
			stats.channel.messageInteractionGraph,
			tr::lng_chart_title_message_interaction(),
			Type::DoubleLinear);
		addChart(
			stats.channel.instantViewInteractionGraph,
			tr::lng_chart_title_instant_view_interaction(),
			Type::DoubleLinear);
		addChart(
			stats.channel.reactionsByEmotionGraph,
			tr::lng_chart_title_reactions_by_emotion(),
			Type::Bar);
		addChart(
			stats.channel.storyInteractionsGraph,
			tr::lng_chart_title_story_interactions(),
			Type::DoubleLinear);
		addChart(
			stats.channel.storyReactionsByEmotionGraph,
			tr::lng_chart_title_story_reactions_by_emotion(),
			Type::Bar);
	} else if (stats.supergroup) {
		addChart(
			stats.supergroup.memberCountGraph,
			tr::lng_chart_title_member_count(),
			Type::Linear);
		addChart(
			stats.supergroup.joinGraph,
			tr::lng_chart_title_group_join(),
			Type::Linear);
		addChart(
			stats.supergroup.joinBySourceGraph,
			tr::lng_chart_title_group_join_by_source(),
			Type::StackBar);
		addChart(
			stats.supergroup.languageGraph,
			tr::lng_chart_title_group_language(),
			Type::StackLinear);
		addChart(
			stats.supergroup.messageContentGraph,
			tr::lng_chart_title_group_message_content(),
			Type::StackBar);
		addChart(
			stats.supergroup.actionGraph,
			tr::lng_chart_title_group_action(),
			Type::DoubleLinear);
		addChart(
			stats.supergroup.dayGraph,
			tr::lng_chart_title_group_day(),
			Type::Linear);
		addChart(
			stats.supergroup.weekGraph,
			tr::lng_chart_title_group_week(),
			Type::StackLinear);
	} else {
		auto &messageOrStory = stats.message
			? stats.message
			: stats.story;
		if (messageOrStory) {
			addChart(
				messageOrStory.messageInteractionGraph,
				tr::lng_chart_title_message_interaction(),
				Type::DoubleLinear);
			addChart(
				messageOrStory.reactionsByEmotionGraph,
				tr::lng_chart_title_reactions_by_emotion(),
				Type::Bar);
		}
	}
	if (!state->pendingCount) {
		++state->pendingCount;
		singlePendingDone();
	}
}

void AddHeader(
		not_null<Ui::VerticalLayout*> content,
		tr::phrase<> text,
		const Data::AnyStatistics &stats) {
	const auto startDate = stats.channel
		? stats.channel.startDate
		: stats.supergroup.startDate;
	const auto endDate = stats.channel
		? stats.channel.endDate
		: stats.supergroup.endDate;
	const auto header = content->add(
		object_ptr<Statistic::Header>(content),
		st::statisticsLayerMargins + st::statisticsChartHeaderPadding);
	header->resizeToWidth(header->width());
	header->setTitle(text(tr::now));
	if (!endDate || !startDate) {
		header->setSubTitle({});
		return;
	}
	header->setSubTitle(Statistic::LangDayMonthYear(startDate)
		+ ' '
		+ QChar(8212)
		+ ' '
		+ Statistic::LangDayMonthYear(endDate));
}

void FillOverview(
		not_null<Ui::VerticalLayout*> content,
		const Data::AnyStatistics &stats,
		bool isChannelStoryStats) {
	using Value = Data::StatisticalValue;

	const auto &channel = stats.channel;
	const auto &supergroup = stats.supergroup;

	if (!isChannelStoryStats) {
		Ui::AddSkip(content, st::statisticsLayerOverviewMargins.top());
		AddHeader(content, tr::lng_stats_overview_title, stats);
		Ui::AddSkip(content);
	}

	struct Second final {
		QColor color;
		QString text;
	};

	const auto parseSecond = [&](const Value &v) -> Second {
		const auto diff = v.value - v.previousValue;
		if (!diff || !v.previousValue) {
			return {};
		}
		constexpr auto kTooMuchDiff = int(1'000'000);
		const auto diffAbs = std::abs(diff);
		const auto diffText = diffAbs > kTooMuchDiff
			? Lang::FormatCountToShort(std::abs(diff)).string
			: QString::number(diffAbs);
		const auto percentage = std::abs(v.growthRatePercentage);
		const auto precision = (percentage == int(percentage)) ? 0 : 1;
		return {
			(diff < 0 ? st::menuIconAttentionColor : st::settingsIconBg2)->c,
			QString("%1%2 (%3%)")
				.arg((diff < 0) ? QChar(0x2212) : QChar(0x002B))
				.arg(diffText)
				.arg(QString::number(percentage, 'f', precision))
		};
	};

	const auto diffBetweenHeaders = 0
		+ st::statisticsOverviewValue.style.font->height
		- st::statisticsHeaderTitleTextStyle.font->height;

	const auto container = content->add(
		object_ptr<Ui::RpWidget>(content),
		st::statisticsLayerMargins);

	const auto addPrimary = [&](const Value &v) {
		return Ui::CreateChild<Ui::FlatLabel>(
			container,
			(v.value >= 0)
				? Lang::FormatCountToShort(v.value).string
				: QString(),
			st::statisticsOverviewValue);
	};
	const auto addSub = [&](
			not_null<Ui::RpWidget*> primary,
			const Value &v,
			tr::phrase<> text) {
		const auto data = parseSecond(v);
		const auto second = Ui::CreateChild<Ui::FlatLabel>(
			container,
			data.text,
			st::statisticsOverviewSecondValue);
		second->setTextColorOverride(data.color);
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
			if (container->height() < rect::bottom(sub)) {
				container->resize(container->width(), rect::bottom(sub));
			}
		}, primary->lifetime());
	};

	const auto isChannel = (!!channel);
	const auto &messageOrStory = stats.message ? stats.message : stats.story;
	const auto isMessage = (!!messageOrStory);

	const auto hasPostReactions = isChannel
		&& (channel.meanReactionCount.value
			|| channel.meanReactionCount.previousValue);

	const auto topLeftLabel = (isChannelStoryStats && isChannel)
		? addPrimary(channel.meanShareCount)
		: isChannel
		? addPrimary(channel.memberCount)
		: isMessage
		? addPrimary({ .value = float64(messageOrStory.views) })
		: addPrimary(supergroup.memberCount);
	const auto topRightLabel = (isChannelStoryStats && isChannel)
		? addPrimary(channel.meanStoryShareCount)
		: isChannel
		? Ui::CreateChild<Ui::FlatLabel>(
			container,
			QString("%1%").arg(0.01
				* std::round(channel.enabledNotificationsPercentage * 100.)),
			st::statisticsOverviewValue)
		: isMessage
		? addPrimary({ .value = float64(messageOrStory.publicForwards) })
		: addPrimary(supergroup.messageCount);
	const auto bottomLeftLabel = (isChannelStoryStats && isChannel)
		? addPrimary(hasPostReactions
			? channel.meanReactionCount
			: channel.meanStoryReactionCount)
		: isChannel
		? addPrimary(channel.meanViewCount)
		: isMessage
		? addPrimary({ .value = float64(messageOrStory.reactions) })
		: addPrimary(supergroup.viewerCount);
	const auto bottomRightLabel = (isChannelStoryStats && isChannel)
		? addPrimary(!hasPostReactions
			? Value{ .value = -1 }
			: channel.meanStoryReactionCount)
		: isChannel
		? addPrimary(channel.meanStoryViewCount)
		: isMessage
		? addPrimary({ .value = float64(messageOrStory.privateForwards) })
		: addPrimary(supergroup.senderCount);
	if (isChannelStoryStats && isChannel) {
		addSub(
			topLeftLabel,
			channel.meanShareCount,
			tr::lng_stats_overview_mean_share_count);
		addSub(
			topRightLabel,
			channel.meanStoryShareCount,
			tr::lng_stats_overview_mean_story_share_count);
		addSub(
			bottomLeftLabel,
			hasPostReactions
				? channel.meanReactionCount
				: channel.meanStoryReactionCount,
			hasPostReactions
				? tr::lng_stats_overview_mean_reactions_count
				: tr::lng_stats_overview_mean_story_reactions_count);
		if (hasPostReactions) {
			addSub(
				bottomRightLabel,
				channel.meanStoryReactionCount,
				tr::lng_stats_overview_mean_story_reactions_count);
		}
	} else if (const auto &s = channel) {
		addSub(
			topLeftLabel,
			s.memberCount,
			tr::lng_stats_overview_member_count);
		addSub(
			topRightLabel,
			{},
			tr::lng_stats_overview_enabled_notifications);
		addSub(
			bottomLeftLabel,
			s.meanViewCount,
			tr::lng_stats_overview_mean_view_count);
		addSub(
			bottomRightLabel,
			s.meanStoryViewCount,
			tr::lng_stats_overview_mean_story_view_count);
	} else if (const auto &s = supergroup) {
		addSub(
			topLeftLabel,
			s.memberCount,
			tr::lng_manage_peer_members);
		addSub(
			topRightLabel,
			s.messageCount,
			tr::lng_stats_overview_messages);
		addSub(
			bottomLeftLabel,
			s.viewerCount,
			tr::lng_stats_overview_group_mean_view_count);
		addSub(
			bottomRightLabel,
			s.senderCount,
			tr::lng_stats_overview_group_mean_post_count);
	} else if (const auto &s = messageOrStory) {
		if (s.views >= 0) {
			addSub(
				topLeftLabel,
				{},
				tr::lng_stats_overview_message_views);
		}
		if (s.publicForwards >= 0) {
			addSub(
				topRightLabel,
				{},
				tr::lng_stats_overview_message_public_shares);
		}
		if (s.reactions >= 0) {
			addSub(
				bottomLeftLabel,
				{},
				tr::lng_manage_peer_reactions);
		}
		if (s.privateForwards >= 0) {
			addSub(
				bottomRightLabel,
				{},
				tr::lng_stats_overview_message_private_shares);
		}
	}
	container->showChildren();
	container->sizeValue() | rpl::distinct_until_changed(
	) | rpl::start_with_next([=](const QSize &s) {
		const auto halfWidth = s.width() / 2;
		{
			const auto &p = st::statisticsOverviewValuePadding;
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
	Ui::AddSkip(content, st::statisticsLayerOverviewMargins.bottom());
}

} // namespace

void FillLoading(
		not_null<Ui::VerticalLayout*> container,
		LoadingType type,
		rpl::producer<bool> toggleOn,
		rpl::producer<> showFinished) {
	const auto emptyWrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	emptyWrap->toggleOn(std::move(toggleOn), anim::type::instant);

	const auto content = emptyWrap->entity();
	const auto iconName = (type == LoadingType::Boosts)
		? u"stats_boosts"_q
		: (type == LoadingType::Earn)
		? u"stats_earn"_q
		: u"stats"_q;
	auto icon = ::Settings::CreateLottieIcon(
		content,
		{ .name = iconName, .sizeOverride = Size(st::changePhoneIconSize) },
		st::settingsBlockedListIconPadding);

	(
		std::move(showFinished) | rpl::take(1)
	) | rpl::start_with_next([animate = std::move(icon.animate)] {
		animate(anim::repeat::loop);
	}, icon.widget->lifetime());
	content->add(std::move(icon.widget));

	content->add(
		object_ptr<Ui::CenterWrap<>>(
			content,
			object_ptr<Ui::FlatLabel>(
				content,
				(type == LoadingType::Boosts)
					? tr::lng_stats_boosts_loading()
					: (type == LoadingType::Earn)
					? tr::lng_stats_earn_loading()
					: tr::lng_stats_loading(),
				st::changePhoneTitle)),
		st::changePhoneTitlePadding + st::boxRowPadding);

	content->add(
		object_ptr<Ui::CenterWrap<>>(
			content,
			object_ptr<Ui::FlatLabel>(
				content,
				(type == LoadingType::Boosts)
					? tr::lng_stats_boosts_loading_subtext()
					: (type == LoadingType::Earn)
					? tr::lng_stats_earn_loading_subtext()
					: tr::lng_stats_loading_subtext(),
				st::statisticsLoadingSubtext)),
		st::changePhoneDescriptionPadding + st::boxRowPadding);

	Ui::AddSkip(content, st::settingsBlockedListIconPadding.top());
}

InnerWidget::InnerWidget(
	QWidget *parent,
	not_null<Controller*> controller,
	not_null<PeerData*> peer,
	FullMsgId contextId,
	FullStoryId storyId)
: VerticalLayout(parent)
, _controller(controller)
, _peer(peer)
, _contextId(contextId)
, _storyId(storyId) {
}

void InnerWidget::load() {
	const auto inner = this;

	const auto descriptor = Descriptor{
		_peer,
		lifetime().make_state<Api::Statistics>(_peer->asChannel()),
		_controller->uiShow()->toastParent(),
	};

	FillLoading(
		inner,
		Info::Statistics::LoadingType::Statistic,
		_loaded.events_starting_with(false) | rpl::map(!rpl::mappers::_1),
		_showFinished.events());

	_showFinished.events(
	) | rpl::take(1) | rpl::start_with_next([=] {
		if (!_contextId && !_storyId) {
			descriptor.api->request(
			) | rpl::start_with_done([=] {
				_state.stats = Data::AnyStatistics{
					descriptor.api->channelStats(),
					descriptor.api->supergroupStats(),
				};
				fill();

			}, lifetime());
		} else {
			const auto lifetimeApi = lifetime().make_state<rpl::lifetime>();
			const auto api = _storyId
				? lifetimeApi->make_state<Api::MessageStatistics>(
					descriptor.peer->asChannel(),
					_storyId)
				: lifetimeApi->make_state<Api::MessageStatistics>(
					descriptor.peer->asChannel(),
					_contextId);

			api->request([=](const Data::StoryStatistics &data) {
				_state.stats = Data::AnyStatistics{
					.message = _contextId ? data : Data::StoryStatistics(),
					.story = _storyId ? data : Data::StoryStatistics(),
				};
				if (_contextId || _storyId) {
					_state.publicForwardsFirstSlice = api->firstSlice();
				}
				fill();

				lifetimeApi->destroy();
			});
		}
	}, lifetime());
}

void InnerWidget::fill() {
	const auto wrap = this->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			this,
			object_ptr<Ui::VerticalLayout>(this)));
	wrap->toggle(false, anim::type::instant);
	const auto inner = wrap->entity();
	const auto descriptor = Descriptor{
		_peer,
		lifetime().make_state<Api::Statistics>(_peer->asChannel()),
		_controller->uiShow()->toastParent(),
	};
	const auto finishLoading = [=] {
		_loaded.fire(true);
		wrap->toggle(true, anim::type::instant);
		this->resizeToWidth(width());
		this->showChildren();
	};
	if (_state.stats.message) {
		if (const auto i = _peer->owner().message(_contextId)) {
			Ui::AddSkip(inner);
			const auto preview = inner->add(
				object_ptr<MessagePreview>(inner, i, QImage()));
			AddContextMenu(preview, _controller, i);
			Ui::AddSkip(inner);
			Ui::AddDivider(inner);
		}
	} else if (_state.stats.story) {
		if (const auto story = _peer->owner().stories().lookup(_storyId)) {
			Ui::AddSkip(inner);
			const auto preview = inner->add(
				object_ptr<MessagePreview>(inner, *story, QImage()));
			preview->setAttribute(Qt::WA_TransparentForMouseEvents);
			Ui::AddSkip(inner);
			Ui::AddDivider(inner);
		}
	}
	FillOverview(inner, _state.stats, false);
	if (_state.stats.channel) {
		FillOverview(inner, _state.stats, true);
	}
	FillStatistic(inner, descriptor, _state.stats, finishLoading);
	const auto &channel = _state.stats.channel;
	const auto &supergroup = _state.stats.supergroup;
	if (channel) {
		fillRecentPosts(inner);
	} else if (supergroup) {
		const auto showPeerInfo = [=](not_null<PeerData*> peer) {
			_showRequests.fire({ .info = peer->id });
		};
		const auto addSkip = [&](not_null<Ui::VerticalLayout*> c) {
			Ui::AddSkip(c);
			Ui::AddDivider(c);
			Ui::AddSkip(c);
			Ui::AddSkip(c);
		};
		if (!supergroup.topSenders.empty()) {
			AddMembersList(
				{ .topSenders = supergroup.topSenders },
				inner,
				showPeerInfo,
				descriptor.peer,
				tr::lng_stats_members_title());
		}
		if (!supergroup.topAdministrators.empty()) {
			addSkip(inner);
			AddMembersList(
				{ .topAdministrators
					= supergroup.topAdministrators },
				inner,
				showPeerInfo,
				descriptor.peer,
				tr::lng_stats_admins_title());
		}
		if (!supergroup.topInviters.empty()) {
			addSkip(inner);
			AddMembersList(
				{ .topInviters = supergroup.topInviters },
				inner,
				showPeerInfo,
				descriptor.peer,
				tr::lng_stats_inviters_title());
		}
	} else if (_state.stats.message || _state.stats.story) {
		using namespace Data;
		AddPublicForwards(
			_state.publicForwardsFirstSlice,
			inner,
			[=](RecentPostId id) {
				_showRequests.fire({
					.info = (!id.messageId && !id.storyId)
						? id.messageId.peer
						: PeerId(0),
					.history = id.messageId,
					.story = id.storyId,
				});
			},
			descriptor.peer,
			RecentPostId{ .messageId = _contextId, .storyId = _storyId });
	}
}

void InnerWidget::fillRecentPosts(not_null<Ui::VerticalLayout*> container) {
	const auto &stats = _state.stats.channel;
	if (!stats || stats.recentMessageInteractions.empty()) {
		return;
	}
	_messagePreviews.reserve(stats.recentMessageInteractions.size());

	const auto wrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	const auto content = wrap->entity();
	AddHeader(content, tr::lng_stats_recent_messages_title, { stats, {} });
	Ui::AddSkip(content);

	const auto addMessage = [=](
			not_null<Ui::VerticalLayout*> messageWrap,
			HistoryItem *maybeItem,
			Data::Story *maybeStory,
			const Data::StatisticsMessageInteractionInfo &info) {
		const auto button = messageWrap->add(
			object_ptr<Ui::SettingsButton>(
				messageWrap,
				rpl::never<QString>(),
				st::statisticsRecentPostButton));
		const auto fullRecentId = Data::RecentPostId{
			.messageId = maybeItem ? maybeItem->fullId() : FullMsgId(),
			.storyId = maybeStory ? maybeStory->fullId() : FullStoryId(),
		};
		auto it = _state.recentPostPreviews.find(fullRecentId);
		auto cachedPreview = (it != end(_state.recentPostPreviews))
			? base::take(it->second)
			: QImage();
		const auto raw = maybeItem
			? Ui::CreateChild<MessagePreview>(
				button,
				maybeItem,
				std::move(cachedPreview))
			: Ui::CreateChild<MessagePreview>(
				button,
				maybeStory,
				std::move(cachedPreview));
		raw->setInfo(
			info.viewsCount,
			info.forwardsCount,
			info.reactionsCount);

		if (maybeItem) {
			AddContextMenu(button, _controller, maybeItem);
		}

		_messagePreviews.push_back(raw);
		raw->show();
		button->sizeValue(
		) | rpl::start_with_next([=](const QSize &s) {
			if (!s.isNull()) {
				raw->setGeometry(Rect(s)
					- st::statisticsRecentPostButton.padding);
			}
		}, raw->lifetime());
		button->setClickedCallback([=] {
			_showRequests.fire({
				.messageStatistic = fullRecentId.messageId,
				.storyStatistic = fullRecentId.storyId,
			});
		});
		Ui::AddSkip(messageWrap);
		if (!wrap->toggled()) {
			wrap->toggle(true, anim::type::normal);
		}
	};

	const auto buttonWrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
			container,
			object_ptr<Ui::SettingsButton>(
				container,
				tr::lng_stories_show_more())));

	constexpr auto kFirstPage = int(10);
	constexpr auto kPerPage = int(30);
	const auto max = int(stats.recentMessageInteractions.size());
	if (_state.recentPostsExpanded) {
		_state.recentPostsExpanded = std::max(
			_state.recentPostsExpanded - kPerPage,
			0);
	}
	const auto showMore = [=] {
		const auto from = _state.recentPostsExpanded;
		_state.recentPostsExpanded = std::min(
			max,
			_state.recentPostsExpanded
				? (_state.recentPostsExpanded + kPerPage)
				: kFirstPage);
		if (_state.recentPostsExpanded == max) {
			buttonWrap->toggle(false, anim::type::instant);
		}
		for (auto i = from; i < _state.recentPostsExpanded; i++) {
			const auto &recent = stats.recentMessageInteractions[i];
			const auto messageWrap = content->add(
				object_ptr<Ui::VerticalLayout>(content));
			auto &data = _peer->owner();
			if (recent.messageId) {
				const auto fullId = FullMsgId(_peer->id, recent.messageId);
				if (const auto item = data.message(fullId)) {
					addMessage(messageWrap, item, nullptr, recent);
					continue;
				}
				const auto callback = crl::guard(content, [=] {
					if (const auto item = _peer->owner().message(fullId)) {
						addMessage(messageWrap, item, nullptr, recent);
						content->resizeToWidth(content->width());
					}
				});
				_peer->session().api().requestMessageData(
					_peer,
					fullId.msg,
					callback);
			} else if (recent.storyId) {
				const auto fullId = FullStoryId{ _peer->id, recent.storyId };
				if (const auto story = data.stories().lookup(fullId)) {
					addMessage(messageWrap, nullptr, *story, recent);
					continue;
				}
			}
		}
		container->resizeToWidth(container->width());
	};
	const auto delay = st::defaultRippleAnimation.hideDuration;
	buttonWrap->entity()->setClickedCallback([=] {
		base::call_delayed(delay, crl::guard(container, showMore));
	});
	showMore();
	if (_messagePreviews.empty()) {
		wrap->toggle(false, anim::type::instant);
	}
}

void InnerWidget::saveState(not_null<Memento*> memento) {
	for (const auto &message : _messagePreviews) {
		message->saveState(_state);
	}
	memento->setState(base::take(_state));
}

void InnerWidget::restoreState(not_null<Memento*> memento) {
	_state = memento->state();
	if (_state.stats.channel
		|| _state.stats.supergroup
		|| _state.stats.message
		|| _state.stats.story) {
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

} // namespace Info::Statistics

