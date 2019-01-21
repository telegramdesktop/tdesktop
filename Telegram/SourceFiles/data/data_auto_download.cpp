/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_auto_download.h"

#include "data/data_peer.h"
#include "data/data_photo.h"
#include "data/data_document.h"
#include "ui/image/image_source.h"
#include "ui/image/image.h"

namespace Data {
namespace AutoDownload {
namespace {

constexpr auto kDefaultMaxSize = 8 * 1024 * 1024;
constexpr auto kVersion = char(1);

template <typename Enum>
auto enums_view(int from, int till) {
	using namespace ranges::view;
	return ints(from, till) | transform([](int index) {
		return static_cast<Enum>(index);
	});
}

template <typename Enum>
auto enums_view(int till) {
	return enums_view<Enum>(0, till);
}

void SetDefaultsForSource(Full &data, Source source) {
	data.setBytesLimit(source, Type::Photo, kDefaultMaxSize);
	data.setBytesLimit(source, Type::VoiceMessage, kDefaultMaxSize);
	data.setBytesLimit(source, Type::VideoMessage, kDefaultMaxSize);
	data.setBytesLimit(source, Type::GIF, kDefaultMaxSize);
	const auto channelsFileLimit = (source == Source::Channel)
		? 0
		: kDefaultMaxSize;
	data.setBytesLimit(source, Type::File, channelsFileLimit);
	data.setBytesLimit(source, Type::Video, channelsFileLimit);
	data.setBytesLimit(source, Type::Music, channelsFileLimit);
}

const Full &Defaults() {
	static auto Result = [] {
		auto result = Full::FullDisabled();
		for (const auto source : enums_view<Source>(kSourcesCount)) {
			SetDefaultsForSource(result, source);
		}
		return result;
	}();
	return Result;
}

Source SourceFromPeer(not_null<PeerData*> peer) {
	if (peer->isUser()) {
		return Source::User;
	} else if (peer->isChat() || peer->isMegagroup()) {
		return Source::Group;
	} else {
		return Source::Channel;
	}
}

Type TypeFromDocument(not_null<DocumentData*> document) {
	if (document->isSong()) {
		return Type::Music;
	} else if (document->isVoiceMessage()) {
		return Type::VoiceMessage;
	} else if (document->isVideoMessage()) {
		return Type::VideoMessage;
	} else if (document->isAnimation()) {
		return Type::GIF;
	} else if (document->isVideoFile()) {
		return Type::Video;
	}
	return Type::File;
}

} // namespace

void Single::setBytesLimit(int bytesLimit) {
	Expects(bytesLimit >= 0 && bytesLimit <= kMaxBytesLimit);

	_limit = bytesLimit;
}

bool Single::hasValue() const {
	return (_limit >= 0);
}

bool Single::shouldDownload(int fileSize) const {
	Expects(hasValue());

	return (_limit > 0) && (fileSize <= _limit);
}

int Single::bytesLimit() const {
	Expects(hasValue());

	return _limit;
}

qint32 Single::serialize() const {
	return _limit;
}

bool Single::setFromSerialized(qint32 serialized) {
	if (serialized < -1 || serialized > kMaxBytesLimit) {
		return false;
	}
	_limit = serialized;
	return true;
}

const Single &Set::single(Type type) const {
	Expects(static_cast<int>(type) >= 0
		&& static_cast<int>(type) < kTypesCount);

	return _data[static_cast<int>(type)];
}

Single &Set::single(Type type) {
	return const_cast<Single&>(static_cast<const Set*>(this)->single(type));
}

void Set::setBytesLimit(Type type, int bytesLimit) {
	single(type).setBytesLimit(bytesLimit);
}

bool Set::hasValue(Type type) const {
	return single(type).hasValue();
}

bool Set::shouldDownload(Type type, int fileSize) const {
	return single(type).shouldDownload(fileSize);
}

int Set::bytesLimit(Type type) const {
	return single(type).bytesLimit();
}

qint32 Set::serialize(Type type) const {
	return single(type).serialize();
}

bool Set::setFromSerialized(Type type, qint32 serialized) {
	if (static_cast<int>(type) < 0
		|| static_cast<int>(type) >= kTypesCount) {
		return false;
	}
	return single(type).setFromSerialized(serialized);
}

const Set &Full::set(Source source) const {
	Expects(static_cast<int>(source) >= 0
		&& static_cast<int>(source) < kSourcesCount);

	return _data[static_cast<int>(source)];
}

Set &Full::set(Source source) {
	return const_cast<Set&>(static_cast<const Full*>(this)->set(source));
}

const Set &Full::setOrDefault(Source source, Type type) const {
	const auto &my = set(source);
	const auto &result = my.hasValue(type) ? my : Defaults().set(source);

	Ensures(result.hasValue(type));
	return result;
}

void Full::setBytesLimit(Source source, Type type, int bytesLimit) {
	set(source).setBytesLimit(type, bytesLimit);
}

bool Full::shouldDownload(Source source, Type type, int fileSize) const {
	return setOrDefault(source, type).shouldDownload(type, fileSize);
}

int Full::bytesLimit(Source source, Type type) const {
	return setOrDefault(source, type).bytesLimit(type);
}

QByteArray Full::serialize() const {
	auto result = QByteArray();
	auto size = sizeof(qint8);
	size += kSourcesCount * kTypesCount * sizeof(qint32);
	result.reserve(size);
	{
		auto buffer = QBuffer(&result);
		buffer.open(QIODevice::WriteOnly);
		auto stream = QDataStream(&buffer);
		stream << qint8(kVersion);
		for (const auto source : enums_view<Source>(kSourcesCount)) {
			for (const auto type : enums_view<Type>(kTypesCount)) {
				stream << set(source).serialize(type);
			}
		}
	}
	return result;
}

bool Full::setFromSerialized(const QByteArray &serialized) {
	if (serialized.isEmpty()) {
		return false;
	}

	auto stream = QDataStream(serialized);
	auto version = qint8();
	stream >> version;
	if (stream.status() != QDataStream::Ok) {
		return false;
	} else if (version != kVersion) {
		return false;
	}
	auto temp = Full();
	for (const auto source : enums_view<Source>(kSourcesCount)) {
		for (const auto type : enums_view<Type>(kTypesCount)) {
			auto value = qint32();
			stream >> value;
			if (!temp.set(source).setFromSerialized(type, value)) {
				return false;
			}
		}
	}
	_data = temp._data;
	return true;
}

Full Full::FullDisabled() {
	auto result = Full();
	for (const auto source : enums_view<Source>(kSourcesCount)) {
		for (const auto type : enums_view<Type>(kTypesCount)) {
			result.setBytesLimit(source, type, 0);
		}
	}
	return result;
}

bool Should(
		const Full &data,
		not_null<PeerData*> peer,
		not_null<DocumentData*> document) {
	if (document->sticker()) {
		return true;
	}
	return data.shouldDownload(
		SourceFromPeer(peer),
		TypeFromDocument(document),
		document->size);
}

bool Should(
		const Full &data,
		not_null<DocumentData*> document) {
	if (document->sticker()) {
		return true;
	}
	const auto type = TypeFromDocument(document);
	const auto size = document->size;
	return data.shouldDownload(Source::User, type, size)
		|| data.shouldDownload(Source::Group, type, size)
		|| data.shouldDownload(Source::Channel, type, size);
}

bool Should(
		const Full &data,
		not_null<PeerData*> peer,
		not_null<Images::Source*> image) {
	return data.shouldDownload(
		SourceFromPeer(peer),
		Type::Photo,
		image->bytesSize());
}

} // namespace AutoDownload
} // namespace Data
