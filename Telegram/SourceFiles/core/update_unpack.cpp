/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/update_unpack.h"

#include "core/update_verify.h"

#include <QtCore/QDataStream>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QSet>
#include <QtCore/QStringList>

#include <cstring>
#include <memory>

#ifndef TDESKTOP_DISABLE_AUTOUPDATE
#if defined Q_OS_WIN && !defined TDESKTOP_USE_PACKAGED
#include <LzmaLib.h>
#else // Q_OS_WIN && !TDESKTOP_USE_PACKAGED
#include <lzma.h>
#endif // Q_OS_WIN && !TDESKTOP_USE_PACKAGED
#endif // !TDESKTOP_DISABLE_AUTOUPDATE

namespace Core::Updates {
namespace {

void SetError(QString *error, const char *text) {
	if (error) {
		*error = QString::fromLatin1(text);
	}
}

#ifndef TDESKTOP_DISABLE_AUTOUPDATE

constexpr auto kMaxUncompressedSize = 1024 * 1024 * 1024;

#if !defined Q_OS_WIN || defined TDESKTOP_USE_PACKAGED
struct LzmaDeleter {
	void operator()(lzma_stream *stream) const {
		lzma_end(stream);
	}
};
#endif // !Q_OS_WIN || TDESKTOP_USE_PACKAGED

[[nodiscard]] bool SafeName(const QString &name) {
	if (name.isEmpty() || QString::fromUtf8(name.toUtf8()) != name) {
		return false;
	}
	for (const auto ch : name) {
		if (ch.unicode() < 32
			|| QLatin1String("\\:<>\"|?*").contains(ch)) {
			return false;
		}
	}
	for (const auto &part : name.split('/')) {
		if (part.isEmpty()
			|| part == QLatin1String(".")
			|| part == QLatin1String("..")
			|| part.endsWith('.')
			|| part.endsWith(' ')) {
			return false;
		}
		const auto stem = part.section('.', 0, 0).trimmed().toUpper();
		const auto devices = { "CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$" };
		for (const auto device : devices) {
			if (stem == QLatin1String(device)) {
				return false;
			}
		}
		if (stem.size() == 4
			&& (stem.startsWith(QLatin1String("COM"))
				|| stem.startsWith(QLatin1String("LPT")))
			&& ((stem.back() >= QLatin1Char('1')
					&& stem.back() <= QLatin1Char('9'))
				|| stem.back() == QChar(0xB9)
				|| stem.back() == QChar(0xB2)
				|| stem.back() == QChar(0xB3))) {
			return false;
		}
	}
	return true;
}

[[nodiscard]] bool AddName(
		const QString &name,
		QSet<QString> &files,
		QSet<QString> &directories) {
	const auto key = name.normalized(QString::NormalizationForm_C).toCaseFolded();
	if (files.contains(key) || directories.contains(key)) {
		return false;
	}
	for (auto slash = key.indexOf('/'); slash >= 0
		; slash = key.indexOf('/', slash + 1)) {
		const auto parent = key.left(slash);
		if (files.contains(parent)) {
			return false;
		}
		directories.insert(parent);
	}
	files.insert(key);
	return true;
}

#endif // !TDESKTOP_DISABLE_AUTOUPDATE

} // namespace

// WHY: Callers authenticate bytes before this parser reads the payload:
// [lzma props on Windows,] original size, compressed bytes.
std::optional<QByteArray> DecompressUpdatePayload(
		const VerifiedUpdate &verified,
		QString *error) {
#ifndef TDESKTOP_DISABLE_AUTOUPDATE
#if defined Q_OS_WIN && !defined TDESKTOP_USE_PACKAGED
	constexpr auto kPropsSize = LZMA_PROPS_SIZE;
#else // Q_OS_WIN && !TDESKTOP_USE_PACKAGED
	constexpr auto kPropsSize = 0;
#endif // Q_OS_WIN && !TDESKTOP_USE_PACKAGED
	constexpr auto kHeaderSize = kPropsSize + sizeof(qint32);
	const auto &payload = verified.envelope.payload;
	if (payload.size() <= kHeaderSize || payload.size() > kMaxPayloadSize) {
		SetError(error, "Bad compressed payload size.");
		return std::nullopt;
	}
	const auto data = payload.constData();
	const auto compressedSize = size_t(payload.size() - kHeaderSize);
	auto originalSize = qint32(0);
	memcpy(&originalSize, data + kPropsSize, sizeof(originalSize));
	if (originalSize <= 0 || originalSize > kMaxUncompressedSize) {
		SetError(error, "Bad uncompressed payload size.");
		return std::nullopt;
	}
	auto result = QByteArray(originalSize, Qt::Uninitialized);
#if defined Q_OS_WIN && !defined TDESKTOP_USE_PACKAGED
	auto resultSize = SizeT(result.size());
	auto sourceSize = SizeT(compressedSize);
	const auto status = LzmaUncompress(
		reinterpret_cast<uchar*>(result.data()),
		&resultSize,
		reinterpret_cast<const uchar*>(data + kHeaderSize),
		&sourceSize,
		reinterpret_cast<const uchar*>(data),
		LZMA_PROPS_SIZE);
	if (status != SZ_OK
		|| sourceSize != compressedSize
		|| resultSize != size_t(originalSize)) {
		SetError(error, "Could not decompress LZMA payload.");
		return std::nullopt;
	}
#else // Q_OS_WIN && !TDESKTOP_USE_PACKAGED
	auto stream = lzma_stream LZMA_STREAM_INIT;
	const auto guard = std::unique_ptr<lzma_stream, LzmaDeleter>(&stream);
	if (lzma_stream_decoder(
			&stream,
			kMaxUncompressedSize,
			LZMA_CONCATENATED) != LZMA_OK) {
		SetError(error, "Could not initialize XZ decoder.");
		return std::nullopt;
	}
	stream.avail_in = compressedSize;
	stream.next_in = reinterpret_cast<const uint8_t*>(data + kHeaderSize);
	stream.avail_out = result.size();
	stream.next_out = reinterpret_cast<uint8_t*>(result.data());
	const auto status = lzma_code(&stream, LZMA_FINISH);
	if (status != LZMA_STREAM_END || stream.avail_in || stream.avail_out) {
		SetError(error, "Could not decompress complete XZ payload.");
		return std::nullopt;
	}
#endif // Q_OS_WIN && !TDESKTOP_USE_PACKAGED
	return result;
#else // !TDESKTOP_DISABLE_AUTOUPDATE
	SetError(error, "Automatic updates are disabled.");
	return std::nullopt;
#endif // TDESKTOP_DISABLE_AUTOUPDATE
}

bool ExtractUpdateFiles(
		const QByteArray &uncompressed,
		quint64 expectedVersion,
		const QString &directory,
		QString *error) {
#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	if (uncompressed.size() > kMaxUncompressedSize) {
		SetError(error, "Uncompressed payload is too large.");
		return false;
	}
	auto stream = QDataStream(uncompressed);
	stream.setVersion(QDataStream::Qt_5_1);
	auto version = quint32(0);
	auto count = quint32(0);
	stream >> version >> count;
	if (stream.status() != QDataStream::Ok
		|| version != UpdateVersionBase(expectedVersion)) {
		SetError(error, "Inner update version does not match envelope.");
		return false;
	}
#ifdef Q_OS_WIN
	constexpr auto kMinimumEntrySize = 14;
#else // Q_OS_WIN
	constexpr auto kMinimumEntrySize = 15;
#endif // Q_OS_WIN
	const auto device = stream.device();
	if (!count || count > device->bytesAvailable() / kMinimumEntrySize) {
		SetError(error, "Bad update file count.");
		return false;
	}
	auto files = QSet<QString>();
	auto directories = QSet<QString>();
	for (auto i = quint32(0); i != count; ++i) {
		const auto namePosition = device->pos();
		auto nameSize = quint32(0);
		stream >> nameSize;
		if (stream.status() != QDataStream::Ok
			|| !nameSize
			|| (nameSize % 2)
			|| nameSize > device->bytesAvailable()
			|| !device->seek(namePosition)) {
			SetError(error, "Bad update filename size.");
			return false;
		}
		auto name = QString();
		auto fileSize = quint32(0);
		auto dataSize = quint32(0);
		stream >> name >> fileSize >> dataSize;
		if (dataSize == quint32(0xFFFFFFFF)) {
			dataSize = 0;
		}
		if (stream.status() != QDataStream::Ok
			|| fileSize != dataSize
			|| dataSize > device->bytesAvailable()) {
			SetError(error, "Bad update file size.");
			return false;
		}
		const auto data = uncompressed.constData() + device->pos();
		if (!device->seek(device->pos() + dataSize)) {
			SetError(error, "Truncated update file.");
			return false;
		}
		auto executable = quint8(0);
#ifndef Q_OS_WIN
		stream >> executable;
#endif // !Q_OS_WIN
		if (stream.status() != QDataStream::Ok || executable > 1) {
			SetError(error, "Bad update executable flag.");
			return false;
		}
		if (!SafeName(name) || !AddName(name, files, directories)) {
			SetError(error, "Unsafe or conflicting update filename.");
			return false;
		}
		auto file = QFile(QDir(directory).filePath(name));
		if (!QDir().mkpath(QFileInfo(file).absolutePath())
			|| !file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
			SetError(error, "Could not create extracted update file.");
			return false;
		}
		if (file.write(data, dataSize) != dataSize || !file.flush()) {
			SetError(error, "Could not write extracted update file.");
			return false;
		}
		if (executable && !file.setPermissions(file.permissions()
				| QFileDevice::ExeOwner
				| QFileDevice::ExeUser
				| QFileDevice::ExeGroup
				| QFileDevice::ExeOther)) {
			SetError(error, "Could not set update executable permissions.");
			return false;
		}
		file.close();
		if (file.error() != QFileDevice::NoError) {
			SetError(error, "Could not close extracted update file.");
			return false;
		}
	}
	if (!stream.atEnd()) {
		SetError(error, "Unexpected trailing update data.");
		return false;
	}
	return true;
#else // !TDESKTOP_DISABLE_AUTOUPDATE
	SetError(error, "Automatic updates are disabled.");
	return false;
#endif // TDESKTOP_DISABLE_AUTOUPDATE
}

} // namespace Core::Updates
