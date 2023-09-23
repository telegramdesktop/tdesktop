/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/layers/generic_box.h"

namespace style {
struct PremiumLimits;
} // namespace style

namespace Data {
class Forum;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

namespace Window {
class SessionNavigation;
} // namespace Window

void ChannelsLimitBox(
	not_null<Ui::GenericBox*> box,
	not_null<Main::Session*> session);
void PublicLinksLimitBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionNavigation*> navigation,
	Fn<void()> retry);
void FilterChatsLimitBox(
	not_null<Ui::GenericBox*> box,
	not_null<Main::Session*> session,
	int currentCount,
	bool include);
void FilterLinksLimitBox(
	not_null<Ui::GenericBox*> box,
	not_null<Main::Session*> session);
void FiltersLimitBox(
	not_null<Ui::GenericBox*> box,
	not_null<Main::Session*> session,
	std::optional<int> filtersCountOverride);
void ShareableFiltersLimitBox(
	not_null<Ui::GenericBox*> box,
	not_null<Main::Session*> session);
void FilterPinsLimitBox(
	not_null<Ui::GenericBox*> box,
	not_null<Main::Session*> session,
	FilterId filterId);
void FolderPinsLimitBox(
	not_null<Ui::GenericBox*> box,
	not_null<Main::Session*> session);
void PinsLimitBox(
	not_null<Ui::GenericBox*> box,
	not_null<Main::Session*> session);
void ForumPinsLimitBox(
	not_null<Ui::GenericBox*> box,
	not_null<Data::Forum*> forum);
void CaptionLimitBox(
	not_null<Ui::GenericBox*> box,
	not_null<Main::Session*> session,
	int remove,
	const style::PremiumLimits *stOverride = nullptr);
void CaptionLimitReachedBox(
	not_null<Ui::GenericBox*> box,
	not_null<Main::Session*> session,
	int remove,
	const style::PremiumLimits *stOverride = nullptr);
void FileSizeLimitBox(
	not_null<Ui::GenericBox*> box,
	not_null<Main::Session*> session,
	uint64 fileSizeBytes,
	const style::PremiumLimits *stOverride = nullptr);
void AccountsLimitBox(
	not_null<Ui::GenericBox*> box,
	not_null<Main::Session*> session);

[[nodiscard]] QString LimitsPremiumRef(const QString &addition);
