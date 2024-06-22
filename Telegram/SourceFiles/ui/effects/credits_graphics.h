/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class PhotoData;

namespace Data {
struct CreditsHistoryEntry;
} // namespace Data

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Ui {

[[nodiscard]] QImage GenerateStars(int height, int count);

[[nodiscard]] not_null<Ui::RpWidget*> CreateSingleStarWidget(
	not_null<Ui::RpWidget*> parent,
	int height);

Fn<void(Painter &, int, int, int, int)> GenerateCreditsPaintUserpicCallback(
	const Data::CreditsHistoryEntry &entry);

Fn<void(Painter &, int, int, int, int)> GenerateCreditsPaintEntryCallback(
	not_null<PhotoData*> photo,
	Fn<void()> update);

Fn<void(Painter &, int, int, int, int)> GeneratePaidMediaPaintCallback(
	not_null<PhotoData*> photo,
	Fn<void()> update);

[[nodiscard]] TextWithEntities GenerateEntryName(
	const Data::CreditsHistoryEntry &entry);

} // namespace Ui
