/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"

class DocumentData;

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace Data {
struct ReactionId;
} // namespace Data

namespace Ui {
class BoxContent;
class GenericBox;
class GradientButton;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Main {
class Session;
} // namespace Main

void ShowStickerPreviewBox(
	std::shared_ptr<ChatHelpers::Show> show,
	not_null<DocumentData*> document);

void DoubledLimitsPreviewBox(
	not_null<Ui::GenericBox*> box,
	not_null<Main::Session*> session);

void UpgradedStoriesPreviewBox(
	not_null<Ui::GenericBox*> box,
	not_null<Main::Session*> session);

void TelegramBusinessPreviewBox(
	not_null<Ui::GenericBox*> box,
	not_null<Main::Session*> session);

enum class PremiumFeature {
	// Premium features.
	Stories,
	DoubleLimits,
	MoreUpload,
	FasterDownload,
	VoiceToText,
	NoAds,
	EmojiStatus,
	InfiniteReactions,
	Stickers,
	AnimatedEmoji,
	AdvancedChatManagement,
	ProfileBadge,
	AnimatedUserpics,
	RealTimeTranslation,
	Wallpapers,
	TagsForMessages,
	LastSeen,
	MessagePrivacy,
	Business,
	Effects,
	FilterTags,

	// Business features.
	BusinessLocation,
	BusinessHours,
	QuickReplies,
	GreetingMessage,
	AwayMessage,
	BusinessBots,
	ChatIntro,
	ChatLinks,

	kCount,
};

void ShowPremiumPreviewBox(
	not_null<Window::SessionController*> controller,
	PremiumFeature section,
	Fn<void(not_null<Ui::BoxContent*>)> shown = nullptr);

void ShowPremiumPreviewBox(
	std::shared_ptr<ChatHelpers::Show> show,
	PremiumFeature section,
	Fn<void(not_null<Ui::BoxContent*>)> shown = nullptr,
	bool hideSubscriptionButton = false);

void ShowPremiumPreviewToBuy(
	not_null<Window::SessionController*> controller,
	PremiumFeature section,
	Fn<void()> hiddenCallback = nullptr);

void PremiumUnavailableBox(not_null<Ui::GenericBox*> box);

[[nodiscard]] object_ptr<Ui::GradientButton> CreateUnlockButton(
	QWidget *parent,
	rpl::producer<QString> text);
