/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <array>

namespace Images {
class Source;
} // namespace Images

namespace Data {
namespace AutoDownload {

constexpr auto kMaxBytesLimit = 3000 * 512 * 1024;

enum class Source {
	User    = 0x00,
	Group   = 0x01,
	Channel = 0x02,
};

constexpr auto kSourcesCount = 3;

enum class Type {
	Photo        = 0x00,
	Video        = 0x01,
	VoiceMessage = 0x02,
	VideoMessage = 0x03,
	Music        = 0x04,
	GIF          = 0x05,
	File         = 0x06,
};

constexpr auto kTypesCount = 7;

class Single {
public:
	void setBytesLimit(int bytesLimit);

	bool hasValue() const;
	bool shouldDownload(int fileSize) const;
	int bytesLimit() const;

	qint32 serialize() const;
	bool setFromSerialized(qint32 serialized);

private:
	int _limit = -1;

};

class Set {
public:
	void setBytesLimit(Type type, int bytesLimit);

	bool hasValue(Type type) const;
	bool shouldDownload(Type type, int fileSize) const;
	int bytesLimit(Type type) const;

	qint32 serialize(Type type) const;
	bool setFromSerialized(Type type, qint32 serialized);

private:
	const Single &single(Type type) const;
	Single &single(Type type);

	std::array<Single, kTypesCount> _data;

};

class Full {
public:
	void setBytesLimit(Source source, Type type, int bytesLimit);

	bool shouldDownload(Source source, Type type, int fileSize) const;
	int bytesLimit(Source source, Type type) const;

	QByteArray serialize() const;
	bool setFromSerialized(const QByteArray &serialized);

	static Full FullDisabled();

private:
	const Set &set(Source source) const;
	Set &set(Source source);
	const Set &setOrDefault(Source source, Type type) const;

	std::array<Set, kSourcesCount> _data;

};

bool Should(
	const Full &data,
	not_null<PeerData*> peer,
	not_null<DocumentData*> document);
bool Should(
	const Full &data,
	not_null<DocumentData*> document);
bool Should(
	const Full &data,
	not_null<PeerData*> peer,
	not_null<Images::Source*> image);

} // namespace AutoDownload
} // namespace Data
