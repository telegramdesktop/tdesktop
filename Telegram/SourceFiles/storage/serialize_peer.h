/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Serialize {

int storageImageLocationSize(const StorageImageLocation &location);
void writeStorageImageLocation(
	QDataStream &stream,
	const StorageImageLocation &location);

// NB! This method can return StorageFileLocation with Type::Generic!
// The reader should discard it or convert to one of the valid modern types.
std::optional<StorageImageLocation> readStorageImageLocation(
	int streamAppVersion,
	QDataStream &stream);

int imageLocationSize(const ImageLocation &location);
void writeImageLocation(QDataStream &stream, const ImageLocation &location);

// NB! This method can return StorageFileLocation with Type::Generic!
// The reader should discard it or convert to one of the valid modern types.
std::optional<ImageLocation> readImageLocation(
	int streamAppVersion,
	QDataStream &stream);

uint32 peerSize(not_null<PeerData*> peer);
void writePeer(QDataStream &stream, not_null<PeerData*> peer);
PeerData *readPeer(
	not_null<Main::Session*> session,
	int streamAppVersion,
	QDataStream &stream);
QString peekUserPhone(int streamAppVersion, QDataStream &stream);

} // namespace Serialize
