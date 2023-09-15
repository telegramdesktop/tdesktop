/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Data {
class Thread;
struct NotifySound;
enum class DefaultNotify;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class PopupMenu;
class RpWidget;
class Show;
} // namespace Ui

namespace MuteMenu {

struct Descriptor {
	not_null<Main::Session*> session;
	Fn<rpl::producer<bool>()> isMutedValue;
	Fn<std::optional<Data::NotifySound>()> currentSound;
	Fn<void(Data::NotifySound)> updateSound;
	Fn<void(TimeId)> updateMutePeriod;
};

[[nodiscard]] Descriptor ThreadDescriptor(not_null<Data::Thread*> thread);
[[nodiscard]] Descriptor DefaultDescriptor(
	not_null<Main::Session*> session,
	Data::DefaultNotify type);

void FillMuteMenu(
	not_null<Ui::PopupMenu*> menu,
	Descriptor descriptor,
	std::shared_ptr<Ui::Show> show);

void SetupMuteMenu(
	not_null<Ui::RpWidget*> parent,
	rpl::producer<> triggers,
	Fn<std::optional<Descriptor>()> makeDescriptor,
	std::shared_ptr<Ui::Show> show);

inline void FillMuteMenu(
		not_null<Ui::PopupMenu*> menu,
		not_null<Data::Thread*> thread,
		std::shared_ptr<Ui::Show> show) {
	FillMuteMenu(menu, ThreadDescriptor(thread), std::move(show));
}

inline void SetupMuteMenu(
		not_null<Ui::RpWidget*> parent,
		rpl::producer<> triggers,
		Fn<Data::Thread*()> makeThread,
		std::shared_ptr<Ui::Show> show) {
	SetupMuteMenu(parent, std::move(triggers), [=] {
		const auto thread = makeThread();
		return thread
			? ThreadDescriptor(thread)
			: std::optional<Descriptor>();
	}, std::move(show));
}

} // namespace MuteMenu
