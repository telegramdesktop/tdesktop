/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class PhotoData;
class DocumentData;

namespace Data {
struct CreditsHistoryEntry;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class MaskedInputField;
class RpWidget;
class VerticalLayout;
} // namespace Ui

namespace Ui {

[[nodiscard]] QImage GenerateStars(int height, int count);

[[nodiscard]] not_null<Ui::RpWidget*> CreateSingleStarWidget(
	not_null<Ui::RpWidget*> parent,
	int height);

[[nodiscard]] not_null<Ui::MaskedInputField*> AddInputFieldForCredits(
	not_null<Ui::VerticalLayout*> container,
	rpl::producer<uint64> value);

Fn<void(Painter &, int, int, int, int)> GenerateCreditsPaintUserpicCallback(
	const Data::CreditsHistoryEntry &entry);

Fn<void(Painter &, int, int, int, int)> GenerateCreditsPaintEntryCallback(
	not_null<PhotoData*> photo,
	Fn<void()> update);

Fn<void(Painter &, int, int, int, int)> GenerateCreditsPaintEntryCallback(
	not_null<DocumentData*> video,
	Fn<void()> update);

Fn<void(Painter &, int, int, int, int)> GeneratePaidMediaPaintCallback(
	not_null<PhotoData*> photo,
	Fn<void()> update);

Fn<Fn<void(Painter&, int, int, int, int)>(Fn<void()>)> PaintPreviewCallback(
	not_null<Main::Session*> session,
	const Data::CreditsHistoryEntry &entry);

[[nodiscard]] TextWithEntities GenerateEntryName(
	const Data::CreditsHistoryEntry &entry);

} // namespace Ui
