/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace InlineBots {
struct WebViewContext;
} // namespace InlineBots

namespace Window {

enum class ResolveType {
	Default,
	BotApp,
	BotStart,
	AddToGroup,
	AddToChannel,
	HashtagSearch,
	ShareGame,
	Mention,
	Boost,
	Profile,
};

struct CommentId {
	MsgId id = 0;
};
struct ThreadId {
	MsgId id = 0;
};
using RepliesByLinkInfo = std::variant<v::null_t, CommentId, ThreadId>;

struct PeerByLinkInfo {
	std::variant<QString, ChannelId> usernameOrId;
	QString phone;
	QString chatLinkSlug;
	MsgId messageId = ShowAtUnreadMsgId;
	StoryId storyId = 0;
	std::optional<TimeId> videoTimestamp;
	QString text;
	RepliesByLinkInfo repliesInfo;
	ResolveType resolveType = ResolveType::Default;
	QString referral;
	QString startToken;
	ChatAdminRights startAdminRights;
	bool startAutoSubmit = false;
	bool joinChannel = false;
	QString botAppName;
	bool botAppForceConfirmation = false;
	bool botAppFullScreen = false;
	QString attachBotUsername;
	std::optional<QString> attachBotToggleCommand;
	bool attachBotMainOpen = false;
	bool attachBotMainCompact = false;
	InlineBots::PeerTypes attachBotChooseTypes;
	std::optional<QString> voicechatHash;
	FullMsgId clickFromMessageId;
	std::shared_ptr<InlineBots::WebViewContext> clickFromBotWebviewContext;
};

} // namespace Window
