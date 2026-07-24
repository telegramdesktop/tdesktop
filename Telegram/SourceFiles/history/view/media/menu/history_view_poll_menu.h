/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"

namespace Data {
class LocationPoint;
} // namespace Data

namespace PollMediaUpload {
struct PollMediaState;
} // namespace PollMediaUpload

struct PollData;
class DocumentData;
class PhotoData;

namespace ChatHelpers {
class TabbedPanel;
} // namespace ChatHelpers

namespace Ui {
class DropdownMenu;
struct PreparedList;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace HistoryView {

struct PollMediaActions {
	Fn<void()> choosePhotoOrVideo;
	Fn<void()> chooseDocument;
	Fn<void()> chooseSticker;
	Fn<void(Ui::PreparedList)> editPhoto;
	Fn<void()> remove;
};

void FillPollAnswerMenu(
	not_null<Ui::DropdownMenu*> menu,
	not_null<PollData*> poll,
	const QByteArray &option,
	not_null<DocumentData*> document,
	FullMsgId itemId,
	not_null<Window::SessionController*> controller);

void ShowPollStatsBox(
	not_null<Window::SessionController*> controller,
	FullMsgId itemId);

void ShowPollStickerPreview(
	not_null<Window::SessionController*> controller,
	not_null<DocumentData*> document,
	Fn<void()> replace,
	Fn<void()> remove);

void ShowPollPhotoPreview(
	not_null<Window::SessionController*> controller,
	not_null<PhotoData*> photo,
	Fn<void()> replace,
	Fn<void()> edit,
	Fn<void()> remove);

void ShowPollDocumentPreview(
	not_null<Window::SessionController*> controller,
	not_null<DocumentData*> document,
	Fn<void()> replace,
	Fn<void()> remove);

void ShowPollGeoPreview(
	not_null<Window::SessionController*> controller,
	Data::LocationPoint point,
	Fn<void()> replace = nullptr,
	Fn<void()> remove = nullptr);

void EditPollPhoto(
	not_null<Window::SessionController*> controller,
	not_null<PhotoData*> photo,
	Fn<void(Ui::PreparedList)> done);

[[nodiscard]] bool ShowPollMediaPreview(
	not_null<Window::SessionController*> controller,
	const std::shared_ptr<PollMediaUpload::PollMediaState> &media,
	PollMediaActions actions);

[[nodiscard]] base::unique_qptr<ChatHelpers::TabbedPanel>
CreatePollStickerPanel(
	not_null<QWidget*> parent,
	not_null<Window::SessionController*> controller);

} // namespace HistoryView
