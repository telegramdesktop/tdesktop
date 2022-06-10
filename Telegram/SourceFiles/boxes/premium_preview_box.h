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

void ShowStickerPreviewBox(
	not_null<Window::SessionController*> controller,
	not_null<DocumentData*> document);

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

void PremiumUnavailableBox(not_null<Ui::GenericBox*> box);
