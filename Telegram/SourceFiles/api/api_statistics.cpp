/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_statistics.h"

#include "apiwrap.h"
#include "data/data_channel.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "main/main_session.h"
#include "statistics/statistics_data_deserialize.h"

namespace Api {
namespace {

[[nodiscard]] Data::StatisticalGraph StatisticalGraphFromTL(
		const MTPStatsGraph &tl) {
	return tl.match([&](const MTPDstatsGraph &d) {
		using namespace Statistic;
		const auto zoomToken = d.vzoom_token().has_value()
			? qs(*d.vzoom_token()).toUtf8()
			: QByteArray();
		return Data::StatisticalGraph{
			StatisticalChartFromJSON(qs(d.vjson().data().vdata()).toUtf8()),
			zoomToken,
		};
	}, [&](const MTPDstatsGraphAsync &data) {
		return Data::StatisticalGraph{
			.zoomToken = qs(data.vtoken()).toUtf8(),
		};
	}, [&](const MTPDstatsGraphError &data) {
		return Data::StatisticalGraph{ .error = qs(data.verror()) };
	});
}

[[nodiscard]] Data::StatisticalValue StatisticalValueFromTL(
		const MTPStatsAbsValueAndPrev &tl) {
	const auto current = tl.data().vcurrent().v;
	const auto previous = tl.data().vprevious().v;
	return Data::StatisticalValue{
		.value = current,
		.previousValue = previous,
		.growthRatePercentage = previous
			? std::abs((current - previous) / float64(previous) * 100.)
			: 0,
	};
}

[[nodiscard]] Data::ChannelStatistics ChannelStatisticsFromTL(
		const MTPDstats_broadcastStats &data) {
	const auto &tlUnmuted = data.venabled_notifications().data();
	const auto unmuted = (!tlUnmuted.vtotal().v)
		? 0.
		: std::clamp(
			tlUnmuted.vpart().v / tlUnmuted.vtotal().v * 100.,
			0.,
			100.);
	using Recent = MTPMessageInteractionCounters;
	auto recentMessages = ranges::views::all(
		data.vrecent_message_interactions().v
	) | ranges::views::transform([&](const Recent &tl) {
		return Data::StatisticsMessageInteractionInfo{
			.messageId = tl.data().vmsg_id().v,
			.viewsCount = tl.data().vviews().v,
			.forwardsCount = tl.data().vforwards().v,
		};
	}) | ranges::to_vector;

	return {
		.startDate = data.vperiod().data().vmin_date().v,
		.endDate = data.vperiod().data().vmax_date().v,

		.memberCount = StatisticalValueFromTL(data.vfollowers()),
		.meanViewCount = StatisticalValueFromTL(data.vviews_per_post()),
		.meanShareCount = StatisticalValueFromTL(data.vshares_per_post()),

		.enabledNotificationsPercentage = unmuted,

		.memberCountGraph = StatisticalGraphFromTL(
			data.vgrowth_graph()),

		.joinGraph = StatisticalGraphFromTL(
			data.vfollowers_graph()),

		.muteGraph = StatisticalGraphFromTL(
			data.vmute_graph()),

		.viewCountByHourGraph = StatisticalGraphFromTL(
			data.vtop_hours_graph()),

		.viewCountBySourceGraph = StatisticalGraphFromTL(
			data.vviews_by_source_graph()),

		.joinBySourceGraph = StatisticalGraphFromTL(
			data.vnew_followers_by_source_graph()),

		.languageGraph = StatisticalGraphFromTL(
			data.vlanguages_graph()),

		.messageInteractionGraph = StatisticalGraphFromTL(
			data.vinteractions_graph()),

		.instantViewInteractionGraph = StatisticalGraphFromTL(
			data.viv_interactions_graph()),

		.recentMessageInteractions = std::move(recentMessages),
	};
}

[[nodiscard]] Data::SupergroupStatistics SupergroupStatisticsFromTL(
		const MTPDstats_megagroupStats &data) {
	using Senders = MTPStatsGroupTopPoster;
	using Administrators = MTPStatsGroupTopAdmin;
	using Inviters = MTPStatsGroupTopInviter;

	auto topSenders = ranges::views::all(
		data.vtop_posters().v
	) | ranges::views::transform([&](const Senders &tl) {
		return Data::StatisticsMessageSenderInfo{
			.userId = UserId(tl.data().vuser_id().v),
			.sentMessageCount = tl.data().vmessages().v,
			.averageCharacterCount = tl.data().vavg_chars().v,
		};
	}) | ranges::to_vector;
	auto topAdministrators = ranges::views::all(
		data.vtop_admins().v
	) | ranges::views::transform([&](const Administrators &tl) {
		return Data::StatisticsAdministratorActionsInfo{
			.userId = UserId(tl.data().vuser_id().v),
			.deletedMessageCount = tl.data().vdeleted().v,
			.bannedUserCount = tl.data().vkicked().v,
			.restrictedUserCount = tl.data().vbanned().v,
		};
	}) | ranges::to_vector;
	auto topInviters = ranges::views::all(
		data.vtop_inviters().v
	) | ranges::views::transform([&](const Inviters &tl) {
		return Data::StatisticsInviterInfo{
			.userId = UserId(tl.data().vuser_id().v),
			.addedMemberCount = tl.data().vinvitations().v,
		};
	}) | ranges::to_vector;

	return {
		.startDate = data.vperiod().data().vmin_date().v,
		.endDate = data.vperiod().data().vmax_date().v,

		.memberCount = StatisticalValueFromTL(data.vmembers()),
		.messageCount = StatisticalValueFromTL(data.vmessages()),
		.viewerCount = StatisticalValueFromTL(data.vviewers()),
		.senderCount = StatisticalValueFromTL(data.vposters()),

		.memberCountGraph = StatisticalGraphFromTL(
			data.vgrowth_graph()),

		.joinGraph = StatisticalGraphFromTL(
			data.vmembers_graph()),

		.joinBySourceGraph = StatisticalGraphFromTL(
			data.vnew_members_by_source_graph()),

		.languageGraph = StatisticalGraphFromTL(
			data.vlanguages_graph()),

		.messageContentGraph = StatisticalGraphFromTL(
			data.vmessages_graph()),

		.actionGraph = StatisticalGraphFromTL(
			data.vactions_graph()),

		.dayGraph = StatisticalGraphFromTL(
			data.vtop_hours_graph()),

		.weekGraph = StatisticalGraphFromTL(
			data.vweekdays_graph()),

		.topSenders = std::move(topSenders),
		.topAdministrators = std::move(topAdministrators),
		.topInviters = std::move(topInviters),
	};
}

} // namespace

Statistics::Statistics(not_null<ApiWrap*> api)
: _api(&api->instance()) {
}

rpl::producer<rpl::no_value, QString> Statistics::request(
		not_null<PeerData*> peer) {
	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();
		const auto channel = peer->asChannel();
		if (!channel) {
			return lifetime;
		}

		if (!channel->isMegagroup()) {
			_api.request(MTPstats_GetBroadcastStats(
				MTP_flags(MTPstats_GetBroadcastStats::Flags(0)),
				channel->inputChannel
			)).done([=](const MTPstats_BroadcastStats &result) {
				_channelStats = ChannelStatisticsFromTL(result.data());
				consumer.put_done();
			}).fail([=](const MTP::Error &error) {
				consumer.put_error_copy(error.type());
			}).send();
		} else {
			_api.request(MTPstats_GetMegagroupStats(
				MTP_flags(MTPstats_GetMegagroupStats::Flags(0)),
				channel->inputChannel
			)).done([=](const MTPstats_MegagroupStats &result) {
				_supergroupStats = SupergroupStatisticsFromTL(result.data());
				peer->owner().processUsers(result.data().vusers());
				consumer.put_done();
			}).fail([=](const MTP::Error &error) {
				consumer.put_error_copy(error.type());
			}).send();
		}

		return lifetime;
	};
}

Statistics::GraphResult Statistics::requestZoom(
		not_null<PeerData*> peer,
		const QString &token,
		float64 x) {
	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();
		const auto channel = peer->asChannel();
		if (!channel) {
			return lifetime;
		}
		const auto wasEmpty = _zoomDeque.empty();
		_zoomDeque.push_back([=] {
			_api.request(MTPstats_LoadAsyncGraph(
				MTP_flags(x
					? MTPstats_LoadAsyncGraph::Flag::f_x
					: MTPstats_LoadAsyncGraph::Flag(0)),
				MTP_string(token),
				MTP_long(x)
			)).done([=](const MTPStatsGraph &result) {
				consumer.put_next(StatisticalGraphFromTL(result));
				consumer.put_done();
				if (!_zoomDeque.empty()) {
					_zoomDeque.pop_front();
					if (!_zoomDeque.empty()) {
						_zoomDeque.front()();
					}
				}
			}).fail([=](const MTP::Error &error) {
				consumer.put_error_copy(error.type());
			}).send();
		});
		if (wasEmpty) {
			_zoomDeque.front()();
		}

		return lifetime;
	};
}

Statistics::GraphResult Statistics::requestMessage(
		not_null<PeerData*> peer,
		MsgId msgId) {
	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();
		const auto channel = peer->asChannel();
		if (!channel) {
			return lifetime;
		}

		_api.request(MTPstats_GetMessageStats(
			MTP_flags(MTPstats_GetMessageStats::Flags(0)),
			channel->inputChannel,
			MTP_int(msgId.bare)
		)).done([=](const MTPstats_MessageStats &result) {
			consumer.put_next(
				StatisticalGraphFromTL(result.data().vviews_graph()));
			consumer.put_done();
		}).fail([=](const MTP::Error &error) {
			consumer.put_error_copy(error.type());
		}).send();

		return lifetime;
	};
}

Data::ChannelStatistics Statistics::channelStats() const {
	return _channelStats;
}

Data::SupergroupStatistics Statistics::supergroupStats() const {
	return _supergroupStats;
}

PublicForwards::PublicForwards(
	not_null<ChannelData*> channel,
	FullMsgId fullId)
: _channel(channel)
, _fullId(fullId)
, _api(&channel->session().api().instance()) {
}

void PublicForwards::request(
		const Data::PublicForwardsSlice::OffsetToken &token,
		Fn<void(Data::PublicForwardsSlice)> done) {
	if (_requestId) {
		return;
	}
	const auto offsetPeer = _channel->owner().peer(token.fullId.peer);
	const auto tlOffsetPeer = offsetPeer
		? offsetPeer->input
		: MTP_inputPeerEmpty();
	constexpr auto kLimit = tl::make_int(100);
	_requestId = _api.request(MTPstats_GetMessagePublicForwards(
		_channel->inputChannel,
		MTP_int(_fullId.msg),
		MTP_int(token.rate),
		tlOffsetPeer,
		MTP_int(token.fullId.msg),
		kLimit
	)).done([=, channel = _channel](const MTPmessages_Messages &result) {
		using Messages = QVector<FullMsgId>;
		_requestId = 0;

		auto nextToken = Data::PublicForwardsSlice::OffsetToken();
		const auto process = [&](const MTPVector<MTPMessage> &messages) {
			auto result = Messages();
			for (const auto &message : messages.v) {
				const auto msgId = IdFromMessage(message);
				const auto peerId = PeerFromMessage(message);
				const auto lastDate = DateFromMessage(message);
				if (const auto peer = channel->owner().peerLoaded(peerId)) {
					if (lastDate) {
						channel->owner().addNewMessage(
							message,
							MessageFlags(),
							NewMessageType::Existing);
						nextToken.fullId = { peerId, msgId };
						result.push_back(nextToken.fullId);
					}
				}
			}
			return result;
		};

		auto allLoaded = false;
		auto fullCount = 0;
		auto messages = result.match([&](const MTPDmessages_messages &data) {
			channel->owner().processUsers(data.vusers());
			channel->owner().processChats(data.vchats());
			auto list = process(data.vmessages());
			allLoaded = true;
			fullCount = list.size();
			return list;
		}, [&](const MTPDmessages_messagesSlice &data) {
			channel->owner().processUsers(data.vusers());
			channel->owner().processChats(data.vchats());
			auto list = process(data.vmessages());

			if (const auto nextRate = data.vnext_rate()) {
				const auto rateUpdated = (nextRate->v != token.rate);
				if (rateUpdated) {
					nextToken.rate = nextRate->v;
				} else {
					allLoaded = true;
				}
			}
			fullCount = data.vcount().v;
			return list;
		}, [&](const MTPDmessages_channelMessages &data) {
			channel->owner().processUsers(data.vusers());
			channel->owner().processChats(data.vchats());
			auto list = process(data.vmessages());
			allLoaded = true;
			fullCount = data.vcount().v;
			return list;
		}, [&](const MTPDmessages_messagesNotModified &) {
			allLoaded = true;
			return Messages();
		});

		_lastTotal = std::max(_lastTotal, fullCount);
		done({
			.list = std::move(messages),
			.total = _lastTotal,
			.allLoaded = allLoaded,
			.token = nextToken,
		});
	}).fail([=] {
		_requestId = 0;
	}).send();
}

MessageStatistics::MessageStatistics(
	not_null<ChannelData*> channel,
	FullMsgId fullId)
: _publicForwards(channel, fullId)
, _channel(channel)
, _fullId(fullId)
, _api(&channel->session().api().instance()) {
}

Data::PublicForwardsSlice MessageStatistics::firstSlice() const {
	return _firstSlice;
}

void MessageStatistics::request(Fn<void(Data::MessageStatistics)> done) {
	if (_channel->isMegagroup()) {
		return;
	}

	const auto requestFirstPublicForwards = [=](
			const Data::StatisticalGraph &messageGraph,
			const Data::StatisticsMessageInteractionInfo &info) {
		_publicForwards.request({}, [=](Data::PublicForwardsSlice slice) {
			const auto total = slice.total;
			_firstSlice = std::move(slice);
			done({
				.messageInteractionGraph = messageGraph,
				.publicForwards = total,
				.privateForwards = info.forwardsCount - total,
				.views = info.viewsCount,
			});
		});
	};

	const auto requestPrivateForwards = [=](
			const Data::StatisticalGraph &messageGraph) {
		_api.request(MTPchannels_GetMessages(
			_channel->inputChannel,
			MTP_vector<MTPInputMessage>(
				1,
				MTP_inputMessageID(MTP_int(_fullId.msg))))
		).done([=](const MTPmessages_Messages &result) {
			const auto process = [&](const MTPVector<MTPMessage> &messages) {
				const auto &message = messages.v.front();
				return message.match([&](const MTPDmessage &data) {
					return Data::StatisticsMessageInteractionInfo{
						.messageId = IdFromMessage(message),
						.viewsCount = data.vviews()
							? data.vviews()->v
							: 0,
						.forwardsCount = data.vforwards()
							? data.vforwards()->v
							: 0,
					};
				}, [](const MTPDmessageEmpty &) {
					return Data::StatisticsMessageInteractionInfo();
				}, [](const MTPDmessageService &) {
					return Data::StatisticsMessageInteractionInfo();
				});
			};

			auto info = result.match([&](const MTPDmessages_messages &data) {
				return process(data.vmessages());
			}, [&](const MTPDmessages_messagesSlice &data) {
				return process(data.vmessages());
			}, [&](const MTPDmessages_channelMessages &data) {
				return process(data.vmessages());
			}, [](const MTPDmessages_messagesNotModified &) {
				return Data::StatisticsMessageInteractionInfo();
			});

			requestFirstPublicForwards(messageGraph, std::move(info));
		}).fail([=](const MTP::Error &error) {
			requestFirstPublicForwards(messageGraph, {});
		}).send();
	};

	_api.request(MTPstats_GetMessageStats(
		MTP_flags(MTPstats_GetMessageStats::Flags(0)),
		_channel->inputChannel,
		MTP_int(_fullId.msg.bare)
	)).done([=](const MTPstats_MessageStats &result) {
		requestPrivateForwards(
			StatisticalGraphFromTL(result.data().vviews_graph()));
	}).fail([=](const MTP::Error &error) {
		requestPrivateForwards({});
	}).send();

}

Boosts::Boosts(not_null<PeerData*> peer)
: _peer(peer)
, _api(&peer->session().api().instance()) {
}

rpl::producer<rpl::no_value, QString> Boosts::request() {
	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();
		const auto channel = _peer->asChannel();
		if (!channel || channel->isMegagroup()) {
			return lifetime;
		}

		_api.request(MTPpremium_GetBoostsStatus(
			_peer->input
		)).done([=](const MTPpremium_BoostsStatus &result) {
			const auto &data = result.data();
			const auto hasPremium = !!data.vpremium_audience();
			const auto premiumMemberCount = hasPremium
				? std::max(0, int(data.vpremium_audience()->data().vpart().v))
				: 0;
			const auto participantCount = hasPremium
				? std::max(
					int(data.vpremium_audience()->data().vtotal().v),
					premiumMemberCount)
				: 0;
			const auto premiumMemberPercentage = (participantCount > 0)
				? (100. * premiumMemberCount / participantCount)
				: 0;

			_boostStatus.overview = Data::BoostsOverview{
				.isBoosted = data.is_my_boost(),
				.level = std::max(data.vlevel().v, 0),
				.boostCount = std::max(
					data.vboosts().v,
					data.vcurrent_level_boosts().v),
				.currentLevelBoostCount = data.vcurrent_level_boosts().v,
				.nextLevelBoostCount = data.vnext_level_boosts()
					? data.vnext_level_boosts()->v
					: 0,
				.premiumMemberCount = premiumMemberCount,
				.premiumMemberPercentage = premiumMemberPercentage,
			};
			_boostStatus.link = qs(data.vboost_url());

			using namespace Data;
			requestBoosts({ .gifts = false }, [=](BoostsListSlice &&slice) {
				_boostStatus.firstSliceBoosts = std::move(slice);
				requestBoosts({ .gifts = true }, [=](BoostsListSlice &&s) {
					_boostStatus.firstSliceGifts = std::move(s);
					consumer.put_done();
				});
			});
		}).fail([=](const MTP::Error &error) {
			consumer.put_error_copy(error.type());
		}).send();

		return lifetime;
	};
}

void Boosts::requestBoosts(
		const Data::BoostsListSlice::OffsetToken &token,
		Fn<void(Data::BoostsListSlice)> done) {
	if (_requestId) {
		return;
	}
	constexpr auto kTlFirstSlice = tl::make_int(kFirstSlice);
	constexpr auto kTlLimit = tl::make_int(kLimit);
	const auto gifts = token.gifts;
	_requestId = _api.request(MTPpremium_GetBoostsList(
		gifts
			? MTP_flags(MTPpremium_GetBoostsList::Flag::f_gifts)
			: MTP_flags(0),
		_peer->input,
		MTP_string(token.next),
		token.next.isEmpty() ? kTlFirstSlice : kTlLimit
	)).done([=](const MTPpremium_BoostsList &result) {
		_requestId = 0;

		const auto &data = result.data();
		_peer->owner().processUsers(data.vusers());

		auto list = std::vector<Data::Boost>();
		list.reserve(data.vboosts().v.size());
		for (const auto &boost : data.vboosts().v) {
			const auto &data = boost.data();
			const auto path = data.vused_gift_slug()
				? (u"giftcode/"_q + qs(data.vused_gift_slug()->v))
				: QString();
			auto giftCodeLink = !path.isEmpty()
				? Data::GiftCodeLink{
					_peer->session().createInternalLink(path),
					_peer->session().createInternalLinkFull(path),
					qs(data.vused_gift_slug()->v),
				}
				: Data::GiftCodeLink();
			list.push_back({
				data.is_gift(),
				data.is_giveaway(),
				data.is_unclaimed(),
				qs(data.vid()),
				data.vuser_id().value_or_empty(),
				data.vgiveaway_msg_id()
					? FullMsgId{ _peer->id, data.vgiveaway_msg_id()->v }
					: FullMsgId(),
				QDateTime::fromSecsSinceEpoch(data.vdate().v),
				data.vexpires().v,
				std::move(giftCodeLink),
				data.vmultiplier().value_or_empty(),
			});
		}
		done(Data::BoostsListSlice{
			.list = std::move(list),
			.multipliedTotal = data.vcount().v,
			.allLoaded = (data.vcount().v == data.vboosts().v.size()),
			.token = Data::BoostsListSlice::OffsetToken{
				.next = data.vnext_offset()
					? qs(*data.vnext_offset())
					: QString(),
				.gifts = gifts,
			},
		});
	}).fail([=] {
		_requestId = 0;
	}).send();
}

Data::BoostStatus Boosts::boostStatus() const {
	return _boostStatus;
}

} // namespace Api
