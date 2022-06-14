/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class DocumentData;

namespace Ui {
class GenericBox;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Main {
class Session;
} // namespace Main

void ShowStickerPreviewBox(
	not_null<Window::SessionController*> controller,
	not_null<DocumentData*> document);

void DoubledLimitsPreviewBox(
	not_null<Ui::GenericBox*> box,
	not_null<Main::Session*> session);

enum class PremiumPreview {
	MoreUpload,
	FasterDownload,
	VoiceToText,
	NoAds,
	Reactions,
	Stickers,
	AdvancedChatManagement,
	ProfileBadge,
	AnimatedUserpics,

	kCount,
};
enum class ReactionDisableType {
	None,
	Group,
	Channel,
};

void ShowPremiumPreviewBox(
	not_null<Window::SessionController*> controller,
	PremiumPreview section,
	const base::flat_map<QString, ReactionDisableType> &disabled = {});

void ShowPremiumPreviewToBuy(
	not_null<Window::SessionController*> controller,
	PremiumPreview section,
	Fn<void()> hiddenCallback);

void PremiumUnavailableBox(not_null<Ui::GenericBox*> box);
