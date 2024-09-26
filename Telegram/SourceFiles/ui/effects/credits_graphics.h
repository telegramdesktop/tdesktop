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
struct CreditsHistoryMedia;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

namespace Ui {

class MaskedInputField;
class RpWidget;
class VerticalLayout;

using PaintRoundImageCallback = Fn<void(
	Painter &p,
	int x,
	int y,
	int outerWidth,
	int size)>;

[[nodiscard]] QImage GenerateStars(int height, int count);

[[nodiscard]] not_null<Ui::RpWidget*> CreateSingleStarWidget(
	not_null<Ui::RpWidget*> parent,
	int height);

[[nodiscard]] not_null<Ui::MaskedInputField*> AddInputFieldForCredits(
	not_null<Ui::VerticalLayout*> container,
	rpl::producer<uint64> value);

PaintRoundImageCallback GenerateCreditsPaintUserpicCallback(
	const Data::CreditsHistoryEntry &entry);

PaintRoundImageCallback GenerateCreditsPaintEntryCallback(
	not_null<PhotoData*> photo,
	Fn<void()> update);

PaintRoundImageCallback GenerateCreditsPaintEntryCallback(
	not_null<DocumentData*> video,
	Fn<void()> update);

PaintRoundImageCallback GenerateCreditsPaintEntryCallback(
	not_null<Main::Session*> session,
	Data::CreditsHistoryMedia media,
	Fn<void()> update);

PaintRoundImageCallback GeneratePaidMediaPaintCallback(
	not_null<PhotoData*> photo,
	PhotoData *second,
	int totalCount,
	Fn<void()> update);

PaintRoundImageCallback GenerateGiftStickerUserpicCallback(
	not_null<Main::Session*> session,
	uint64 stickerId,
	Fn<void()> update);

Fn<PaintRoundImageCallback(Fn<void()>)> PaintPreviewCallback(
	not_null<Main::Session*> session,
	const Data::CreditsHistoryEntry &entry);

[[nodiscard]] TextWithEntities GenerateEntryName(
	const Data::CreditsHistoryEntry &entry);

Fn<void(QPainter &)> PaintOutlinedColoredCreditsIconCallback(
	int size,
	float64 outlineRatio);

[[nodiscard]] QImage CreditsWhiteDoubledIcon(int size, float64 outlineRatio);

} // namespace Ui
