/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_wall_paper.h"

#include "data/data_document.h"
#include "data/data_file_origin.h"
#include "data/data_session.h"
#include "storage/serialize_common.h"
#include "core/application.h"
#include "main/main_session.h"

namespace Data {
namespace {

constexpr auto FromLegacyBackgroundId(int32 legacyId) -> WallPaperId {
	return uint64(0xFFFFFFFF00000000ULL) | uint64(uint32(legacyId));
}

constexpr auto kUninitializedBackground = FromLegacyBackgroundId(-999);
constexpr auto kTestingThemeBackground = FromLegacyBackgroundId(-666);
constexpr auto kTestingDefaultBackground = FromLegacyBackgroundId(-665);
constexpr auto kTestingEditorBackground = FromLegacyBackgroundId(-664);
constexpr auto kThemeBackground = FromLegacyBackgroundId(-2);
constexpr auto kCustomBackground = FromLegacyBackgroundId(-1);
constexpr auto kLegacy1DefaultBackground = FromLegacyBackgroundId(0);
constexpr auto kDefaultBackground = 5947530738516623361;
constexpr auto kIncorrectDefaultBackground = FromLegacyBackgroundId(105);

quint32 SerializeMaybeColor(std::optional<QColor> color) {
	return color
		? ((quint32(std::clamp(color->red(), 0, 255)) << 16)
			| (quint32(std::clamp(color->green(), 0, 255)) << 8)
			| quint32(std::clamp(color->blue(), 0, 255)))
		: quint32(-1);
}

std::optional<QColor> MaybeColorFromSerialized(quint32 serialized) {
	return (serialized == quint32(-1))
		? std::nullopt
		: std::make_optional(QColor(
			int((serialized >> 16) & 0xFFU),
			int((serialized >> 8) & 0xFFU),
			int(serialized & 0xFFU)));
}

std::optional<QColor> ColorFromString(const QString &string) {
	if (string.size() != 6) {
		return {};
	} else if (ranges::any_of(string, [](QChar ch) {
		return (ch < 'a' || ch > 'f')
			&& (ch < 'A' || ch > 'F')
			&& (ch < '0' || ch > '9');
	})) {
		return {};
	}
	const auto component = [](const QString &text, int index) {
		const auto decimal = [](QChar hex) {
			const auto code = hex.unicode();
			return (code >= '0' && code <= '9')
				? int(code - '0')
				: (code >= 'a' && code <= 'f')
				? int(code - 'a' + 0x0a)
				: int(code - 'A' + 0x0a);
		};
		index *= 2;
		return decimal(text[index]) * 0x10 + decimal(text[index + 1]);
	};
	return QColor(
		component(string, 0),
		component(string, 1),
		component(string, 2),
		255);
}

QString StringFromColor(QColor color) {
	const auto component = [](int value) {
		const auto hex = [](int value) {
			value = std::clamp(value, 0, 15);
			return (value > 9)
				? ('a' + (value - 10))
				: ('0' + value);
		};
		return QString() + hex(value / 16) + hex(value % 16);
	};
	return component(color.red())
		+ component(color.green())
		+ component(color.blue());
}

} // namespace

WallPaper::WallPaper(WallPaperId id) : _id(id) {
}

void WallPaper::setLocalImageAsThumbnail(std::shared_ptr<Image> image) {
	Expects(IsDefaultWallPaper(*this)
		|| IsLegacy1DefaultWallPaper(*this)
		|| IsCustomWallPaper(*this));
	Expects(_thumbnail == nullptr);

	_thumbnail = std::move(image);
}

WallPaperId WallPaper::id() const {
	return _id;
}

std::optional<QColor> WallPaper::backgroundColor() const {
	return _backgroundColor;
}

DocumentData *WallPaper::document() const {
	return _document;
}

Image *WallPaper::localThumbnail() const {
	return _thumbnail.get();
}

bool WallPaper::isPattern() const {
	return _flags & MTPDwallPaper::Flag::f_pattern;
}

bool WallPaper::isDefault() const {
	return _flags & MTPDwallPaper::Flag::f_default;
}

bool WallPaper::isCreator() const {
	return _flags & MTPDwallPaper::Flag::f_creator;
}

bool WallPaper::isDark() const {
	return _flags & MTPDwallPaper::Flag::f_dark;
}

bool WallPaper::isLocal() const {
	return !document() && _thumbnail;
}

bool WallPaper::isBlurred() const {
	return _settings & MTPDwallPaperSettings::Flag::f_blur;
}

int WallPaper::patternIntensity() const {
	return _intensity;
}

bool WallPaper::hasShareUrl() const {
	return !_slug.isEmpty();
}

QString WallPaper::shareUrl(not_null<Main::Session*> session) const {
	if (!hasShareUrl()) {
		return QString();
	}
	const auto base = session->createInternalLinkFull("bg/" + _slug);
	auto params = QStringList();
	if (isPattern()) {
		if (_backgroundColor) {
			params.push_back("bg_color=" + StringFromColor(*_backgroundColor));
		}
		if (_intensity) {
			params.push_back("intensity=" + QString::number(_intensity));
		}
	}
	auto mode = QStringList();
	if (_settings & MTPDwallPaperSettings::Flag::f_blur) {
		mode.push_back("blur");
	}
	if (_settings & MTPDwallPaperSettings::Flag::f_motion) {
		mode.push_back("motion");
	}
	if (!mode.isEmpty()) {
		params.push_back("mode=" + mode.join('+'));
	}
	return params.isEmpty()
		? base
		: base + '?' + params.join('&');
}

void WallPaper::loadDocumentThumbnail() const {
	if (_document) {
		_document->loadThumbnail(fileOrigin());
	}
}

void WallPaper::loadDocument() const {
	if (_document) {
		_document->save(fileOrigin(), QString());
	}
}

FileOrigin WallPaper::fileOrigin() const {
	return FileOriginWallpaper(_id, _accessHash, _ownerId, _slug);
}

UserId WallPaper::ownerId() const {
	return _ownerId;
}

MTPInputWallPaper WallPaper::mtpInput(not_null<Main::Session*> session) const {
	return (_ownerId && _ownerId != session->userId() && !_slug.isEmpty())
		? MTP_inputWallPaperSlug(MTP_string(_slug))
		: MTP_inputWallPaper(MTP_long(_id), MTP_long(_accessHash));
}

MTPWallPaperSettings WallPaper::mtpSettings() const {
	return MTP_wallPaperSettings(
		MTP_flags(_settings),
		(_backgroundColor
			? MTP_int(SerializeMaybeColor(_backgroundColor))
			: MTP_int(0)),
		MTP_int(0), // second_background_color
		MTP_int(_intensity),
		MTP_int(0) // rotation
	);
}

WallPaper WallPaper::withUrlParams(
		const QMap<QString, QString> &params) const {
	using Flag = MTPDwallPaperSettings::Flag;

	auto result = *this;
	result._settings = Flag(0);
	result._backgroundColor = ColorFromString(_slug);
	result._intensity = kDefaultIntensity;

	if (auto mode = params.value("mode"); !mode.isEmpty()) {
		const auto list = mode.replace('+', ' ').split(' ');
		for (const auto &change : list) {
			if (change == qstr("blur")) {
				result._settings |= Flag::f_blur;
			} else if (change == qstr("motion")) {
				result._settings |= Flag::f_motion;
			}
		}
	}
	if (const auto color = ColorFromString(params.value("bg_color"))) {
		result._settings |= Flag::f_background_color;
		result._backgroundColor = color;
	}
	if (const auto string = params.value("intensity"); !string.isEmpty()) {
		auto ok = false;
		const auto intensity = string.toInt(&ok);
		if (ok && base::in_range(intensity, 0, 101)) {
			result._settings |= Flag::f_intensity;
			result._intensity = intensity;
		}
	}

	return result;
}

WallPaper WallPaper::withBlurred(bool blurred) const {
	using Flag = MTPDwallPaperSettings::Flag;

	auto result = *this;
	if (blurred) {
		result._settings |= Flag::f_blur;
	} else {
		result._settings &= ~Flag::f_blur;
	}
	return result;
}

WallPaper WallPaper::withPatternIntensity(int intensity) const {
	using Flag = MTPDwallPaperSettings::Flag;

	auto result = *this;
	result._settings |= Flag::f_intensity;
	result._intensity = intensity;
	return result;
}

WallPaper WallPaper::withBackgroundColor(QColor color) const {
	using Flag = MTPDwallPaperSettings::Flag;

	auto result = *this;
	result._settings |= Flag::f_background_color;
	result._backgroundColor = color;
	if (ColorFromString(_slug)) {
		result._slug = StringFromColor(color);
	}
	return result;
}

WallPaper WallPaper::withParamsFrom(const WallPaper &other) const {
	auto result = *this;
	result._settings = other._settings;
	if (other._backgroundColor || !ColorFromString(_slug)) {
		result._backgroundColor = other._backgroundColor;
		if (ColorFromString(_slug)) {
			result._slug = StringFromColor(*result._backgroundColor);
		}
	}
	result._intensity = other._intensity;
	return result;
}

WallPaper WallPaper::withoutImageData() const {
	auto result = *this;
	result._thumbnail = nullptr;
	return result;
}

std::optional<WallPaper> WallPaper::Create(
		not_null<Main::Session*> session,
		const MTPWallPaper &data) {
	return data.match([&](const MTPDwallPaper &data) {
		return Create(session, data);
	}, [](const MTPDwallPaperNoFile &data) {
		return std::optional<WallPaper>(); // #TODO themes
	});
}

std::optional<WallPaper> WallPaper::Create(
		not_null<Main::Session*> session,
		const MTPDwallPaper &data) {
	using Flag = MTPDwallPaper::Flag;

	const auto document = session->data().processDocument(
		data.vdocument());
	if (!document->checkWallPaperProperties()) {
		return std::nullopt;
	}
	auto result = WallPaper(data.vid().v);
	result._accessHash = data.vaccess_hash().v;
	result._ownerId = session->userId();
	result._flags = data.vflags().v;
	result._slug = qs(data.vslug());
	result._document = document;
	if (const auto settings = data.vsettings()) {
		const auto isPattern = ((result._flags & Flag::f_pattern) != 0);
		settings->match([&](const MTPDwallPaperSettings &data) {
			using Flag = MTPDwallPaperSettings::Flag;

			result._settings = data.vflags().v;
			const auto backgroundColor = data.vbackground_color();
			if (isPattern && backgroundColor) {
				result._backgroundColor = MaybeColorFromSerialized(
					backgroundColor->v);
			} else {
				result._settings &= ~Flag::f_background_color;
			}
			const auto intensity = data.vintensity();
			if (isPattern && intensity) {
				result._intensity = intensity->v;
			} else {
				result._settings &= ~Flag::f_intensity;
			}
		});
	}
	return result;
}

QByteArray WallPaper::serialize() const {
	auto size = sizeof(quint64) // _id
		+ sizeof(quint64) // _accessHash
		+ sizeof(qint32) // _flags
		+ Serialize::stringSize(_slug)
		+ sizeof(qint32) // _settings
		+ sizeof(quint32) // _backgroundColor
		+ sizeof(qint32) // _intensity
		+ (2 * sizeof(qint32)); // ownerId

	auto result = QByteArray();
	result.reserve(size);
	{
		const auto field1 = qint32(uint32(_ownerId.bare & 0xFFFFFFFF));
		const auto field2 = qint32(uint32(_ownerId.bare >> 32));
		auto stream = QDataStream(&result, QIODevice::WriteOnly);
		stream.setVersion(QDataStream::Qt_5_1);
		stream
			<< quint64(_id)
			<< quint64(_accessHash)
			<< qint32(_flags)
			<< _slug
			<< qint32(_settings)
			<< SerializeMaybeColor(_backgroundColor)
			<< qint32(_intensity)
			<< field1
			<< field2;
	}
	return result;
}

std::optional<WallPaper> WallPaper::FromSerialized(
		const QByteArray &serialized) {
	if (serialized.isEmpty()) {
		return std::nullopt;
	}

	auto id = quint64();
	auto accessHash = quint64();
	auto ownerId = UserId();
	auto flags = qint32();
	auto slug = QString();
	auto settings = qint32();
	auto backgroundColor = quint32();
	auto intensity = qint32();

	auto stream = QDataStream(serialized);
	stream.setVersion(QDataStream::Qt_5_1);
	stream
		>> id
		>> accessHash
		>> flags
		>> slug
		>> settings
		>> backgroundColor
		>> intensity;
	if (!stream.atEnd()) {
		auto field1 = qint32();
		auto field2 = qint32();
		stream >> field1;
		if (!stream.atEnd()) {
			stream >> field2;
		}
		ownerId = UserId(
			BareId(uint32(field1)) | (BareId(uint32(field2)) << 32));
	}
	if (stream.status() != QDataStream::Ok) {
		return std::nullopt;
	} else if (intensity < 0 || intensity > 100) {
		return std::nullopt;
	}
	auto result = WallPaper(id);
	result._accessHash = accessHash;
	result._ownerId = ownerId;
	result._flags = MTPDwallPaper::Flags::from_raw(flags);
	result._slug = slug;
	result._settings = MTPDwallPaperSettings::Flags::from_raw(settings);
	result._backgroundColor = MaybeColorFromSerialized(backgroundColor);
	result._intensity = intensity;
	return result;
}

std::optional<WallPaper> WallPaper::FromLegacySerialized(
		quint64 id,
		quint64 accessHash,
		quint32 flags,
		QString slug) {
	auto result = WallPaper(id);
	result._accessHash = accessHash;
	result._flags = MTPDwallPaper::Flags::from_raw(flags);
	result._slug = slug;
	result._backgroundColor = ColorFromString(slug);
	return result;
}

std::optional<WallPaper> WallPaper::FromLegacyId(qint32 legacyId) {
	auto result = WallPaper(FromLegacyBackgroundId(legacyId));
	if (!IsCustomWallPaper(result)) {
		result._flags = MTPDwallPaper::Flag::f_default;
	}
	return result;
}

std::optional<WallPaper> WallPaper::FromColorSlug(const QString &slug) {
	if (const auto color = ColorFromString(slug)) {
		auto result = CustomWallPaper();
		result._slug = slug;
		result._backgroundColor = color;
		return result;
	}
	return std::nullopt;
}

WallPaper ThemeWallPaper() {
	return WallPaper(kThemeBackground);
}

bool IsThemeWallPaper(const WallPaper &paper) {
	return (paper.id() == kThemeBackground);
}

WallPaper CustomWallPaper() {
	return WallPaper(kCustomBackground);
}

bool IsCustomWallPaper(const WallPaper &paper) {
	return (paper.id() == kCustomBackground);
}

WallPaper Legacy1DefaultWallPaper() {
	return WallPaper(kLegacy1DefaultBackground);
}

bool IsLegacy1DefaultWallPaper(const WallPaper &paper) {
	return (paper.id() == kLegacy1DefaultBackground);
}

WallPaper DefaultWallPaper() {
	return WallPaper(kDefaultBackground);
}

bool IsDefaultWallPaper(const WallPaper &paper) {
	return (paper.id() == kDefaultBackground)
		|| (paper.id() == kIncorrectDefaultBackground);
}

bool IsCloudWallPaper(const WallPaper &paper) {
	return (paper.id() != kIncorrectDefaultBackground)
		&& !IsThemeWallPaper(paper)
		&& !IsCustomWallPaper(paper)
		&& !IsLegacy1DefaultWallPaper(paper)
		&& !details::IsUninitializedWallPaper(paper)
		&& !details::IsTestingThemeWallPaper(paper)
		&& !details::IsTestingDefaultWallPaper(paper)
		&& !details::IsTestingEditorWallPaper(paper);
}

QColor PatternColor(QColor background) {
	const auto hue = background.hueF();
	const auto saturation = background.saturationF();
	const auto value = background.valueF();
	return QColor::fromHsvF(
		hue,
		std::min(1.0, saturation + 0.05 + 0.1 * (1. - saturation)),
		(value > 0.5
			? std::max(0., value * 0.65)
			: std::max(0., std::min(1., 1. - value * 0.65))),
		0.4
	).toRgb();
}

QImage PreparePatternImage(
		QImage image,
		QColor bg,
		QColor fg,
		int intensity) {
	if (image.format() != QImage::Format_ARGB32_Premultiplied) {
		image = std::move(image).convertToFormat(
			QImage::Format_ARGB32_Premultiplied);
	}
	// Similar to ColorizePattern.
	// But here we set bg to all 'alpha=0' pixels and fg to opaque ones.

	const auto width = image.width();
	const auto height = image.height();
	const auto alpha = anim::interpolate(
		0,
		255,
		fg.alphaF() * std::clamp(intensity / 100., 0., 1.));
	if (!alpha) {
		image.fill(bg);
		return image;
	}
	fg.setAlpha(255);
	const auto patternBg = anim::shifted(bg);
	const auto patternFg = anim::shifted(fg);

	constexpr auto resultIntsPerPixel = 1;
	const auto resultIntsPerLine = (image.bytesPerLine() >> 2);
	const auto resultIntsAdded = resultIntsPerLine - width * resultIntsPerPixel;
	auto resultInts = reinterpret_cast<uint32*>(image.bits());
	Assert(resultIntsAdded >= 0);
	Assert(image.depth() == static_cast<int>((resultIntsPerPixel * sizeof(uint32)) << 3));
	Assert(image.bytesPerLine() == (resultIntsPerLine << 2));

	const auto maskBytesPerPixel = (image.depth() >> 3);
	const auto maskBytesPerLine = image.bytesPerLine();
	const auto maskBytesAdded = maskBytesPerLine - width * maskBytesPerPixel;

	// We want to read the last byte of four available.
	// This is the difference with style::colorizeImage.
	auto maskBytes = image.constBits() + (maskBytesPerPixel - 1);
	Assert(maskBytesAdded >= 0);
	Assert(image.depth() == (maskBytesPerPixel << 3));
	for (auto y = 0; y != height; ++y) {
		for (auto x = 0; x != width; ++x) {
			const auto maskOpacity = static_cast<anim::ShiftedMultiplier>(
				*maskBytes) + 1;
			const auto fgOpacity = (maskOpacity * alpha) >> 8;
			const auto bgOpacity = 256 - fgOpacity;
			*resultInts = anim::unshifted(
				patternBg * bgOpacity + patternFg * fgOpacity);
			maskBytes += maskBytesPerPixel;
			resultInts += resultIntsPerPixel;
		}
		maskBytes += maskBytesAdded;
		resultInts += resultIntsAdded;
	}
	return image;
}

QImage PrepareBlurredBackground(QImage image) {
	constexpr auto kSize = 900;
	constexpr auto kRadius = 24;
	if (image.width() > kSize || image.height() > kSize) {
		image = image.scaled(
			kSize,
			kSize,
			Qt::KeepAspectRatio,
			Qt::SmoothTransformation);
	}
	return Images::BlurLargeImage(image, kRadius);
}

namespace details {

WallPaper UninitializedWallPaper() {
	return WallPaper(kUninitializedBackground);
}

bool IsUninitializedWallPaper(const WallPaper &paper) {
	return (paper.id() == kUninitializedBackground);
}

WallPaper TestingThemeWallPaper() {
	return WallPaper(kTestingThemeBackground);
}

bool IsTestingThemeWallPaper(const WallPaper &paper) {
	return (paper.id() == kTestingThemeBackground);
}

WallPaper TestingDefaultWallPaper() {
	return WallPaper(kTestingDefaultBackground);
}

bool IsTestingDefaultWallPaper(const WallPaper &paper) {
	return (paper.id() == kTestingDefaultBackground);
}

WallPaper TestingEditorWallPaper() {
	return WallPaper(kTestingEditorBackground);
}

bool IsTestingEditorWallPaper(const WallPaper &paper) {
	return (paper.id() == kTestingEditorBackground);
}

} // namespace details
} // namespace Data
