/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class History;
class HistoryItem;

namespace Data {
class SavedMessages;
} // namespace Data

namespace Main {
class Account;
class Domain;
class Session;
} // namespace Main

namespace twidget {
struct WidgetStorage;
} // namespace twidget

namespace Tgw {

struct StorageHost {
	uint64 id = 0;
	Fn<void(
		const std::string &area,
		const std::map<std::string, std::string> &values)> commit;
};

struct HeightRecord {
	double height = 0.;
	bool bottom = false;

	friend inline bool operator==(HeightRecord, HeightRecord) = default;
};

void StartStorage(not_null<Main::Session*> session);

[[nodiscard]] twidget::WidgetStorage StoredAreas(
	not_null<HistoryItem*> item);
[[nodiscard]] std::optional<HeightRecord> StoredHeight(
	not_null<HistoryItem*> item);
void StoreHeight(not_null<HistoryItem*> item, HeightRecord height);
[[nodiscard]] bool StorageRetiring(not_null<HistoryItem*> item);
[[nodiscard]] StorageHost OpenStorageHost(not_null<HistoryItem*> item);
[[nodiscard]] bool StorageHostStale(uint64 id);
void StorageHostVisibility(uint64 id, bool visible);
void StorageHostRetired(uint64 id);
void StorageHostReleased(uint64 id);

void EraseStorage(not_null<Main::Account*> account);
void EraseDomainStorage(
	not_null<Main::Domain*> domain,
	const QString &dataName);
void ClearChatStorage(not_null<Main::Session*> session, PeerId peer);
void DeleteChatStorage(not_null<Main::Session*> session, PeerId peer);
void ClearMessagesStorage(
	not_null<Main::Session*> session,
	PeerId peer,
	const QVector<MTPint> &ids);
void ClearNonChannelStorage(
	not_null<Main::Session*> session,
	const QVector<MTPint> &ids);
void ClearHistoryStorage(
	not_null<Main::Session*> session,
	PeerId peer,
	MsgId till);
void ClearHistoryStorage(not_null<History*> history, MsgId till);
void DeleteMessageStorage(not_null<HistoryItem*> item);
void ClearDatesStorage(
	not_null<History*> history,
	TimeId minDate,
	TimeId maxDate);
void ClearTopicStorage(not_null<History*> history, MsgId rootId);
void ClearSublistStorage(
	not_null<Data::SavedMessages*> saved,
	PeerId sublist);
void ClearParticipantStorage(
	not_null<Main::Session*> session,
	PeerId peer,
	PeerId from);
void MoveStorage(not_null<HistoryItem*> from, not_null<HistoryItem*> to);

} // namespace Tgw
