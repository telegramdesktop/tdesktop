/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class VerticalLayout;
} // namespace Ui

namespace Data {

enum class DefaultNotify : uint8_t;
class Thread;

struct VolumeController {
	Fn<ushort()> volume = nullptr;
	Fn<void(ushort)> saveVolume = nullptr;
};

[[nodiscard]] VolumeController DefaultRingtonesVolumeController(
	not_null<Main::Session*> session,
	Data::DefaultNotify defaultNotify);

[[nodiscard]] VolumeController ThreadRingtonesVolumeController(
	not_null<Data::Thread*> thread);

} // namespace Data

namespace Ui {

void AddRingtonesVolumeSlider(
	not_null<Ui::VerticalLayout*> container,
	rpl::producer<bool> toggleOn,
	rpl::producer<QString> subtitle,
	Data::VolumeController volumeController);

} // namespace Ui
