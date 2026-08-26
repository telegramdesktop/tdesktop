/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class DocumentData;
class PhotoData;

namespace Ui {
class DropdownMenu;
class RpWidget;
} // namespace Ui

namespace Data {
struct ReactionId;
} // namespace Data

namespace Window {
class SessionController;
} // namespace Window

namespace HistoryView {

bool ShowStickerPreview(
	not_null<Window::SessionController*> controller,
	FullMsgId origin,
	not_null<DocumentData*> document,
	Fn<void(not_null<Ui::DropdownMenu*>)> fillMenu = nullptr);

bool ShowPhotoPreview(
	not_null<Window::SessionController*> controller,
	FullMsgId origin,
	not_null<PhotoData*> photo,
	Fn<void(not_null<Ui::DropdownMenu*>)> fillMenu = nullptr);

void ShowWidgetPreview(
	not_null<Window::SessionController*> controller,
	Fn<void(not_null<Ui::RpWidget*>)> setupContent,
	Fn<void(not_null<Ui::DropdownMenu*>)> fillMenu);

bool ShowReactionPreview(
	not_null<Window::SessionController*> controller,
	FullMsgId origin,
	Data::ReactionId reactionId,
	bool emojiPreview = false);

} // namespace HistoryView
