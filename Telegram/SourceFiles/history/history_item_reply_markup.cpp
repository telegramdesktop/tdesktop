/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history_item_reply_markup.h"

#include "data/data_session.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "inline_bots/bot_attach_web_view.h"

namespace {

[[nodiscard]] InlineBots::PeerTypes PeerTypesFromMTP(
		const MTPvector<MTPInlineQueryPeerType> &types) {
	using namespace InlineBots;
	auto result = PeerTypes(0);
	for (const auto &type : types.v) {
		result |= type.match([&](const MTPDinlineQueryPeerTypePM &data) {
			return PeerType::User;
		}, [&](const MTPDinlineQueryPeerTypeChat &data) {
			return PeerType::Group;
		}, [&](const MTPDinlineQueryPeerTypeMegagroup &data) {
			return PeerType::Group;
		}, [&](const MTPDinlineQueryPeerTypeBroadcast &data) {
			return PeerType::Broadcast;
		}, [&](const MTPDinlineQueryPeerTypeBotPM &data) {
			return PeerType::Bot;
		}, [&](const MTPDinlineQueryPeerTypeSameBotPM &data) {
			return PeerType();
		});
	}
	return result;
}

[[nodiscard]] RequestPeerQuery RequestPeerQueryFromTL(
		const MTPDkeyboardButtonRequestPeer &query) {
	using Type = RequestPeerQuery::Type;
	using Restriction = RequestPeerQuery::Restriction;
	auto result = RequestPeerQuery();
	result.maxQuantity = query.vmax_quantity().v;
	const auto restriction = [](const MTPBool *value) {
		return !value
			? Restriction::Any
			: mtpIsTrue(*value)
			? Restriction::Yes
			: Restriction::No;
	};
	const auto rights = [](const MTPChatAdminRights *value) {
		return value ? ChatAdminRightsInfo(*value).flags : ChatAdminRights();
	};
	query.vpeer_type().match([&](const MTPDrequestPeerTypeUser &data) {
		result.type = Type::User;
		result.userIsBot = restriction(data.vbot());
		result.userIsPremium = restriction(data.vpremium());
	}, [&](const MTPDrequestPeerTypeChat &data) {
		result.type = Type::Group;
		result.amCreator = data.is_creator();
		result.isBotParticipant = data.is_bot_participant();
		result.groupIsForum = restriction(data.vforum());
		result.hasUsername = restriction(data.vhas_username());
		result.myRights = rights(data.vuser_admin_rights());
		result.botRights = rights(data.vbot_admin_rights());
	}, [&](const MTPDrequestPeerTypeBroadcast &data) {
		result.type = Type::Broadcast;
		result.amCreator = data.is_creator();
		result.hasUsername = restriction(data.vhas_username());
		result.myRights = rights(data.vuser_admin_rights());
		result.botRights = rights(data.vbot_admin_rights());
	});
	return result;
}

} // namespace

HistoryMessageMarkupButton::HistoryMessageMarkupButton(
	Type type,
	const QString &text,
	const QByteArray &data,
	const QString &forwardText,
	int64 buttonId)
: type(type)
, text(text)
, forwardText(forwardText)
, data(data)
, buttonId(buttonId) {
}

HistoryMessageMarkupButton *HistoryMessageMarkupButton::Get(
		not_null<Data::Session*> owner,
		FullMsgId itemId,
		int row,
		int column) {
	if (const auto item = owner->message(itemId)) {
		if (const auto markup = item->Get<HistoryMessageReplyMarkup>()) {
			if (row < markup->data.rows.size()) {
				auto &buttons = markup->data.rows[row];
				if (column < buttons.size()) {
					return &buttons[column];
				}
			}
		}
	}
	return nullptr;
}

void HistoryMessageMarkupData::fillRows(
		const QVector<MTPKeyboardButtonRow> &list) {
	rows.clear();
	if (list.isEmpty()) {
		return;
	}

	using Type = Button::Type;
	rows.reserve(list.size());
	for (const auto &row : list) {
		row.match([&](const MTPDkeyboardButtonRow &data) {
			auto row = std::vector<Button>();
			row.reserve(data.vbuttons().v.size());
			for (const auto &button : data.vbuttons().v) {
				button.match([&](const MTPDkeyboardButton &data) {
					row.emplace_back(Type::Default, qs(data.vtext()));
				}, [&](const MTPDkeyboardButtonCallback &data) {
					row.emplace_back(
						(data.is_requires_password()
							? Type::CallbackWithPassword
							: Type::Callback),
						qs(data.vtext()),
						qba(data.vdata()));
				}, [&](const MTPDkeyboardButtonRequestGeoLocation &data) {
					row.emplace_back(Type::RequestLocation, qs(data.vtext()));
				}, [&](const MTPDkeyboardButtonRequestPhone &data) {
					row.emplace_back(Type::RequestPhone, qs(data.vtext()));
				}, [&](const MTPDkeyboardButtonRequestPeer &data) {
					const auto query = RequestPeerQueryFromTL(data);
					row.emplace_back(
						Type::RequestPeer,
						qs(data.vtext()),
						QByteArray(
							reinterpret_cast<const char*>(&query),
							sizeof(query)),
						QString(),
						int64(data.vbutton_id().v));
				}, [&](const MTPDkeyboardButtonUrl &data) {
					row.emplace_back(
						Type::Url,
						qs(data.vtext()),
						qba(data.vurl()));
				}, [&](const MTPDkeyboardButtonSwitchInline &data) {
					const auto type = data.is_same_peer()
						? Type::SwitchInlineSame
						: Type::SwitchInline;
					row.emplace_back(type, qs(data.vtext()), qba(data.vquery()));
					if (type == Type::SwitchInline) {
						// Optimization flag.
						// Fast check on all new messages if there is a switch button to auto-click it.
						flags |= ReplyMarkupFlag::HasSwitchInlineButton;
						if (const auto types = data.vpeer_types()) {
							row.back().peerTypes = PeerTypesFromMTP(*types);
						}
					}
				}, [&](const MTPDkeyboardButtonGame &data) {
					row.emplace_back(Type::Game, qs(data.vtext()));
				}, [&](const MTPDkeyboardButtonBuy &data) {
					row.emplace_back(Type::Buy, qs(data.vtext()));
				}, [&](const MTPDkeyboardButtonUrlAuth &data) {
					row.emplace_back(
						Type::Auth,
						qs(data.vtext()),
						qba(data.vurl()),
						qs(data.vfwd_text().value_or_empty()),
						data.vbutton_id().v);
				}, [&](const MTPDkeyboardButtonRequestPoll &data) {
					const auto quiz = [&] {
						if (!data.vquiz()) {
							return QByteArray();
						}
						return data.vquiz()->match([&](const MTPDboolTrue&) {
							return QByteArray(1, 1);
						}, [&](const MTPDboolFalse&) {
							return QByteArray(1, 0);
						});
					}();
					row.emplace_back(
						Type::RequestPoll,
						qs(data.vtext()),
						quiz);
				}, [&](const MTPDkeyboardButtonUserProfile &data) {
					row.emplace_back(
						Type::UserProfile,
						qs(data.vtext()),
						QByteArray::number(data.vuser_id().v));
				}, [&](const MTPDinputKeyboardButtonUrlAuth &data) {
					LOG(("API Error: inputKeyboardButtonUrlAuth."));
					// Should not get those for the users.
				}, [&](const MTPDinputKeyboardButtonUserProfile &data) {
					LOG(("API Error: inputKeyboardButtonUserProfile."));
					// Should not get those for the users.
				}, [&](const MTPDkeyboardButtonWebView &data) {
					row.emplace_back(
						Type::WebView,
						qs(data.vtext()),
						data.vurl().v);
				}, [&](const MTPDkeyboardButtonSimpleWebView &data) {
					row.emplace_back(
						Type::SimpleWebView,
						qs(data.vtext()),
						data.vurl().v);
				});
			}
			if (!row.empty()) {
				rows.push_back(std::move(row));
			}
		});
	}
	if (rows.size() == 1
		&& rows.front().size() == 1
		&& rows.front().front().type == Type::Buy) {
		flags |= ReplyMarkupFlag::OnlyBuyButton;
	}
}

HistoryMessageMarkupData::HistoryMessageMarkupData(
		const MTPReplyMarkup *data) {
	if (!data) {
		return;
	}
	using Flag = ReplyMarkupFlag;
	data->match([&](const MTPDreplyKeyboardMarkup &data) {
		flags = (data.is_resize() ? Flag::Resize : Flag())
			| (data.is_selective() ? Flag::Selective : Flag())
			| (data.is_single_use() ? Flag::SingleUse : Flag())
			| (data.is_persistent() ? Flag::Persistent : Flag());
		placeholder = qs(data.vplaceholder().value_or_empty());
		fillRows(data.vrows().v);
	}, [&](const MTPDreplyInlineMarkup &data) {
		flags = Flag::Inline;
		placeholder = QString();
		fillRows(data.vrows().v);
	}, [&](const MTPDreplyKeyboardHide &data) {
		flags = Flag::None
			| (data.is_selective() ? Flag::Selective : Flag());
		placeholder = QString();
	}, [&](const MTPDreplyKeyboardForceReply &data) {
		flags = Flag::ForceReply
			| (data.is_selective() ? Flag::Selective : Flag())
			| (data.is_single_use() ? Flag::SingleUse : Flag());
		placeholder = qs(data.vplaceholder().value_or_empty());
	});
}

void HistoryMessageMarkupData::fillForwardedData(
		const HistoryMessageMarkupData &original) {
	Expects(isNull());
	Expects(!original.isNull());

	flags = original.flags;
	placeholder = original.placeholder;

	rows.reserve(original.rows.size());
	using Type = HistoryMessageMarkupButton::Type;
	for (const auto &existing : original.rows) {
		auto row = std::vector<Button>();
		row.reserve(existing.size());
		for (const auto &button : existing) {
			const auto newType = (button.type != Type::SwitchInlineSame)
				? button.type
				: Type::SwitchInline;
			const auto text = button.forwardText.isEmpty()
				? button.text
				: button.forwardText;
			row.emplace_back(
				newType,
				text,
				button.data,
				QString(),
				button.buttonId);
		}
		if (!row.empty()) {
			rows.push_back(std::move(row));
		}
	}
}

bool HistoryMessageMarkupData::isNull() const {
	if (flags & ReplyMarkupFlag::IsNull) {
		Assert(isTrivial());
		return true;
	}
	return false;
}

bool HistoryMessageMarkupData::isTrivial() const {
	return rows.empty()
		&& placeholder.isEmpty()
		&& !(flags & ~ReplyMarkupFlag::IsNull);
}

HistoryMessageRepliesData::HistoryMessageRepliesData(
		const MTPMessageReplies *data) {
	if (!data) {
		return;
	}
	const auto &fields = data->c_messageReplies();
	if (const auto list = fields.vrecent_repliers()) {
		recentRepliers.reserve(list->v.size());
		for (const auto &id : list->v) {
			recentRepliers.push_back(peerFromMTP(id));
		}
	}
	repliesCount = fields.vreplies().v;
	channelId = ChannelId(fields.vchannel_id().value_or_empty());
	readMaxId = fields.vread_max_id().value_or_empty();
	maxId = fields.vmax_id().value_or_empty();
	isNull = false;
	pts = fields.vreplies_pts().v;
}
