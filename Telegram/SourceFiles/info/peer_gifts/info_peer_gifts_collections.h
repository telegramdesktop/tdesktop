/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Data {
class SavedStarGiftId;
} // namespace Data

namespace Ui {
class GenericBox;
} // namespace Ui

namespace Window {
class SessionNavigation;
} // namespace Window

namespace Info::PeerGifts {

void NewCollectionBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionNavigation*> navigation,
	not_null<PeerData*> peer,
	Data::SavedStarGiftId addId,
	Fn<void(MTPStarGiftCollection)> added);

void EditCollectionNameBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionNavigation*> navigation,
	not_null<PeerData*> peer,
	int id,
	QString current,
	Fn<void(QString)> done);

} // namespace Info::PeerGifts
