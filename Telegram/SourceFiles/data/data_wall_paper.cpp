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
#include "ui/chat/chat_theme.h"
#include "ui/color_int_conversion.h"
#include "core/application.h"
#include "main/main_session.h"

namespace Ui {

QColor ColorFromSerialized(MTPint serialized) {
	return ColorFromSerialized(serialized.v);
}

std::optional<QColor> MaybeColorFromSerialized(
		const tl::conditional<MTPint> &mtp) {
	return mtp ? ColorFromSerialized(*mtp) : std::optional<QColor>();
}

} // namespace Ui

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
constexpr auto kLegacy2DefaultBackground = 5947530738516623361;
constexpr auto kLegacy3DefaultBackground = 5778236420632084488;
constexpr auto kLegacy4DefaultBackground = 5945087215657811969;
constexpr auto kDefaultBackground = 5933856211186221059;
constexpr auto kIncorrectDefaultBackground = FromLegacyBackgroundId(105);

constexpr auto kVersionTag = qint32(0x7FFFFFFF);
constexpr auto kVersion = 1;

using Ui::MaybeColorFromSerialized;

[[nodiscard]] quint32 SerializeColor(const QColor &color) {
	return (quint32(std::clamp(color.red(), 0, 255)) << 16)
		| (quint32(std::clamp(color.green(), 0, 255)) << 8)
		| quint32(std::clamp(color.blue(), 0, 255));
}

[[nodiscard]] quint32 SerializeMaybeColor(std::optional<QColor> color) {
	return color ? SerializeColor(*color) : quint32(-1);
}

[[nodiscard]] std::vector<QColor> ColorsFromMTP(
		const MTPDwallPaperSettings &data) {
	auto result = std::vector<QColor>();
	const auto c1 = MaybeColorFromSerialized(data.vbackground_color());
	if (!c1) {
		return result;
	}
	result.reserve(4);
	result.push_back(*c1);
	const auto c2 = MaybeColorFromSerialized(
		data.vsecond_background_color());
	if (!c2) {
		return result;
	}
	result.push_back(*c2);
	const auto c3 = MaybeColorFromSerialized(data.vthird_background_color());
	if (!c3) {
		return result;
	}
	result.push_back(*c3);
	const auto c4 = MaybeColorFromSerialized(
		data.vfourth_background_color());
	if (!c4) {
		return result;
	}
	result.push_back(*c4);
	return result;
}

[[nodiscard]] std::optional<QColor> ColorFromString(QStringView string) {
	if (string.size() != 6) {
		return {};
	} else if (ranges::any_of(string, [](QChar ch) {
		return (ch < 'a' || ch > 'f')
			&& (ch < 'A' || ch > 'F')
			&& (ch < '0' || ch > '9');
	})) {
		return {};
	}
	const auto component = [](QStringView text, int index) {
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

[[nodiscard]] std::vector<QColor> ColorsFromString(const QString &string) {
	constexpr auto kMaxColors = 4;
	const auto view = QStringView(string);
	const auto count = int(view.size() / 6);
	if (!count || count > kMaxColors || view.size() != count * 7 - 1) {
		return {};
	}
	auto result = std::vector<QColor>();
	result.reserve(count);
	for (auto i = 0; i != count; ++i) {
		if (i + 1 < count
			&& view[i * 7 + 6] != '~'
			&& (count > 2 || view[i * 7 + 6] != '-')) {
			return {};
		} else if (const auto parsed = ColorFromString(view.mid(i * 7, 6))) {
			result.push_back(*parsed);
		} else {
			return {};
		}
	}
	return result;
}

[[nodiscard]] QString StringFromColor(QColor color) {
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

[[nodiscard]] QString StringFromColors(const std::vector<QColor> &colors) {
	Expects(!colors.empty());

	auto strings = QStringList();
	strings.reserve(colors.size());
	for (const auto &color : colors) {
		strings.push_back(StringFromColor(color));
	}
	const auto separator = (colors.size() > 2) ? '~' : '-';
	return strings.join(separator);
}

[[nodiscard]] qint32 RawFromLegacyFlags(qint32 legacyFlags) {
	using Flag = WallPaperFlag;
	return ((legacyFlags & (1 << 0)) ? qint32(Flag::Creator) : 0)
		| ((legacyFlags & (1 << 1)) ? qint32(Flag::Default) : 0)
		| ((legacyFlags & (1 << 3)) ? qint32(Flag::Pattern) : 0)
		| ((legacyFlags & (1 << 4)) ? qint32(Flag::Dark) : 0);
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

QString WallPaper::emojiId() const {
	return _emojiId;
}

bool WallPaper::equals(const WallPaper &paper) const {
	return (_flags == paper._flags)
		&& (_slug == paper._slug)
		&& (_emojiId == paper._emojiId)
		&& (_backgroundColors == paper._backgroundColors)
		&& (_rotation == paper._rotation)
		&& (_intensity == paper._intensity)
		&& (_blurred == paper._blurred)
		&& (_document == paper._document);
}

const std::vector<QColor> WallPaper::backgroundColors() const {
	return _backgroundColors;
}

DocumentData *WallPaper::document() const {
	return _document;
}

Image *WallPaper::localThumbnail() const {
	return _thumbnail.get();
}

bool WallPaper::isPattern() const {
	return _flags & WallPaperFlag::Pattern;
}

bool WallPaper::isDefault() const {
	return _flags & WallPaperFlag::Default;
}

bool WallPaper::isCreator() const {
	return _flags & WallPaperFlag::Creator;
}

bool WallPaper::isDark() const {
	return _flags & WallPaperFlag::Dark;
}

bool WallPaper::isLocal() const {
	return !document() && _thumbnail;
}

bool WallPaper::isBlurred() const {
	return _blurred;
}

int WallPaper::patternIntensity() const {
	return _intensity;
}

float64 WallPaper::patternOpacity() const {
	return _intensity / 100.;
}

int WallPaper::gradientRotation() const {
	// In case of complex gradients rotation value is dynamic.
	return (_backgroundColors.size() < 3) ? _rotation : 0;
}

bool WallPaper::hasShareUrl() const {
	return !_slug.isEmpty();
}

QStringList WallPaper::collectShareParams() const {
	auto result = QStringList();
	if (isPattern()) {
		if (!backgroundColors().empty()) {
			result.push_back(
				"bg_color=" + StringFromColors(backgroundColors()));
		}
		if (_intensity) {
			result.push_back("intensity=" + QString::number(_intensity));
		}
	}
	if (_rotation && backgroundColors().size() == 2) {
		result.push_back("rotation=" + QString::number(_rotation));
	}
	auto mode = QStringList();
	if (_blurred) {
		mode.push_back("blur");
	}
	if (!mode.isEmpty()) {
		result.push_back("mode=" + mode.join('+'));
	}
	return result;
}

bool WallPaper::isNull() const {
	return !_id && _slug.isEmpty() && _backgroundColors.empty();
}

QString WallPaper::key() const {
	if (isNull()) {
		return QString();
	}
	const auto base = _slug.isEmpty()
		? (_id
			? QString::number(_id)
			: StringFromColors(backgroundColors()))
		: ("bg/" + _slug);
	auto params = collectShareParams();
	if (_document && !isPattern()) {
		params += u"&intensity="_q + QString::number(_intensity);
	}
	return params.isEmpty() ? base : (base + '?' + params.join('&'));
}

QString WallPaper::shareUrl(not_null<Main::Session*> session) const {
	if (!hasShareUrl()) {
		return QString();
	}
	const auto base = session->createInternalLinkFull("bg/" + _slug);
	const auto params = collectShareParams();
	return params.isEmpty() ? base : (base + '?' + params.join('&'));
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
	const auto serializeForIndex = [&](int index) {
		return (_backgroundColors.size() > index)
			? MTP_int(SerializeColor(_backgroundColors[index]))
			: MTP_int(0);
	};
	using Flag = MTPDwallPaperSettings::Flag;
	const auto flagForIndex = [&](int index) {
		return (_backgroundColors.size() <= index)
			? Flag(0)
			: (index == 0)
			? Flag::f_background_color
			: (index == 1)
			? Flag::f_second_background_color
			: (index == 2)
			? Flag::f_third_background_color
			: Flag::f_fourth_background_color;
	};
	return MTP_wallPaperSettings(
		MTP_flags((_blurred ? Flag::f_blur : Flag(0))
			| Flag::f_intensity
			| Flag::f_rotation
			| (_emojiId.isEmpty() ? Flag() : Flag::f_emoticon)
			| flagForIndex(0)
			| flagForIndex(1)
			| flagForIndex(2)
			| flagForIndex(3)),
		serializeForIndex(0),
		serializeForIndex(1),
		serializeForIndex(2),
		serializeForIndex(3),
		MTP_int(_intensity),
		MTP_int(_rotation),
		MTP_string(_emojiId));
}

WallPaper WallPaper::withUrlParams(
		const QMap<QString, QString> &params) const {
	auto result = *this;
	result._blurred = false;
	result._backgroundColors = ColorsFromString(_slug);
	result._intensity = kDefaultIntensity;
	if (auto mode = params.value("mode"); !mode.isEmpty()) {
		const auto list = mode.replace('+', ' ').split(' ');
		for (const auto &change : list) {
			if (change == u"blur"_q) {
				result._blurred = true;
			}
		}
	}
	if (result._backgroundColors.empty()) {
		result._backgroundColors = ColorsFromString(params.value("bg_color"));
	}
	if (result._backgroundColors.empty()) {
		result._backgroundColors = ColorsFromString(params.value("gradient"));
	}
	if (result._backgroundColors.empty()) {
		result._backgroundColors = ColorsFromString(params.value("color"));
	}
	if (result._backgroundColors.empty()) {
		result._backgroundColors = ColorsFromString(params.value("slug"));
	}
	if (const auto string = params.value("intensity"); !string.isEmpty()) {
		auto ok = false;
		const auto intensity = string.toInt(&ok);
		if (ok && base::in_range(intensity, -100, 101)) {
			result._intensity = intensity;
		}
	}
	result._rotation = params.value("rotation").toInt();
	result._rotation = (std::clamp(result._rotation, 0, 315) / 45) * 45;

	return result;
}

WallPaper WallPaper::withBlurred(bool blurred) const {
	auto result = *this;
	result._blurred = blurred;
	return result;
}

WallPaper WallPaper::withPatternIntensity(int intensity) const {
	auto result = *this;
	result._intensity = intensity;
	return result;
}

WallPaper WallPaper::withGradientRotation(int rotation) const {
	auto result = *this;
	result._rotation = rotation;
	return result;
}

WallPaper WallPaper::withBackgroundColors(std::vector<QColor> colors) const {
	auto result = *this;
	result._backgroundColors = std::move(colors);
	if (!ColorsFromString(_slug).empty()) {
		result._slug = StringFromColors(result._backgroundColors);
	}
	return result;
}

WallPaper WallPaper::withParamsFrom(const WallPaper &other) const {
	auto result = *this;
	result._blurred = other._blurred;
	if (!other._backgroundColors.empty()) {
		result._backgroundColors = other._backgroundColors;
		if (!ColorsFromString(_slug).empty()) {
			result._slug = StringFromColors(result._backgroundColors);
		}
	}
	result._intensity = other._intensity;
	if (other.isPattern()) {
		result._flags |= WallPaperFlag::Pattern;
	}
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
		return Create(data);
	});
}

std::optional<WallPaper> WallPaper::Create(
		not_null<Main::Session*> session,
		const MTPDwallPaper &data) {
	const auto document = session->data().processDocument(
		data.vdocument());
	if (!document->checkWallPaperProperties()) {
		return std::nullopt;
	}
	auto result = WallPaper(data.vid().v);
	result._accessHash = data.vaccess_hash().v;
	result._ownerId = session->userId();
	result._flags = (data.is_dark() ? WallPaperFlag::Dark : WallPaperFlag(0))
		| (data.is_pattern() ? WallPaperFlag::Pattern : WallPaperFlag(0))
		| (data.is_default() ? WallPaperFlag::Default : WallPaperFlag(0))
		| (data.is_creator() ? WallPaperFlag::Creator : WallPaperFlag(0));
	result._slug = qs(data.vslug());
	result._document = document;
	if (const auto settings = data.vsettings()) {
		settings->match([&](const MTPDwallPaperSettings &data) {
			result._blurred = data.is_blur();
			if (const auto intensity = data.vintensity()) {
				result._intensity = intensity->v;
			}
			if (result.isPattern()) {
				result._backgroundColors = ColorsFromMTP(data);
				if (const auto rotation = data.vrotation()) {
					result._rotation = rotation->v;
				}
			}
		});
	}
	return result;
}

std::optional<WallPaper> WallPaper::Create(const MTPDwallPaperNoFile &data) {
	auto result = WallPaper(data.vid().v);
	result._flags = (data.is_dark() ? WallPaperFlag::Dark : WallPaperFlag(0))
		| (data.is_default() ? WallPaperFlag::Default : WallPaperFlag(0));
	result._blurred = false;
	result._backgroundColors.clear();
	if (const auto settings = data.vsettings()) {
		settings->match([&](const MTPDwallPaperSettings &data) {
			result._blurred = data.is_blur();
			result._backgroundColors = ColorsFromMTP(data);
			if (const auto rotation = data.vrotation()) {
				result._rotation = rotation->v;
			}
			result._emojiId = qs(data.vemoticon().value_or_empty());
		});
	}
	return result;
}

QByteArray WallPaper::serialize() const {
	auto size = sizeof(quint64) // _id
		+ sizeof(quint64) // _accessHash
		+ sizeof(qint32) // version tag
		+ sizeof(qint32) // version
		+ sizeof(qint32) // _flags
		+ Serialize::stringSize(_slug)
		+ sizeof(qint32) // _settings
		+ sizeof(qint32) // _backgroundColors.size()
		+ (_backgroundColors.size() * sizeof(quint32)) // _backgroundColors
		+ sizeof(qint32) // _intensity
		+ sizeof(qint32) // _rotation
		+ sizeof(quint64); // ownerId

	auto result = QByteArray();
	result.reserve(size);
	{
		auto stream = QDataStream(&result, QIODevice::WriteOnly);
		stream.setVersion(QDataStream::Qt_5_1);
		stream
			<< quint64(_id)
			<< quint64(_accessHash)
			<< qint32(kVersionTag)
			<< qint32(kVersion)
			<< qint32(_flags)
			<< _slug
			<< qint32(_blurred ? 1 : 0)
			<< qint32(_backgroundColors.size());
		for (const auto &color : _backgroundColors) {
			stream << SerializeMaybeColor(color);
		}
		stream
			<< qint32(_intensity)
			<< qint32(_rotation)
			<< quint64(_ownerId.bare);
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
	auto versionTag = qint32();
	auto version = qint32(0);

	auto stream = QDataStream(serialized);
	stream.setVersion(QDataStream::Qt_5_1);
	stream
		>> id
		>> accessHash
		>> versionTag;

	auto flags = qint32();
	auto ownerId = UserId();
	auto slug = QString();
	auto blurred = qint32();
	auto backgroundColors = std::vector<QColor>();
	auto intensity = qint32();
	auto rotation = qint32();
	if (versionTag == kVersionTag) {
		auto bareOwnerId = quint64();
		auto backgroundColorsCount = qint32();
		stream
			>> version
			>> flags
			>> slug
			>> blurred
			>> backgroundColorsCount;
		if (backgroundColorsCount < 0 || backgroundColorsCount > 4) {
			return std::nullopt;
		}
		backgroundColors.reserve(backgroundColorsCount);
		for (auto i = 0; i != backgroundColorsCount; ++i) {
			auto serialized = quint32();
			stream >> serialized;
			const auto color = MaybeColorFromSerialized(serialized);
			if (!color) {
				return std::nullopt;
			}
			backgroundColors.push_back(*color);
		}
		stream
			>> intensity
			>> rotation
			>> bareOwnerId;
		ownerId = UserId(BareId(bareOwnerId));
	} else {
		auto settings = qint32();
		auto backgroundColor = quint32();
		stream
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
		flags = RawFromLegacyFlags(versionTag);
		blurred = (settings & qint32(1U << 1)) ? 1 : 0;
		if (const auto color = MaybeColorFromSerialized(backgroundColor)) {
			backgroundColors.push_back(*color);
		}
	}
	if (stream.status() != QDataStream::Ok) {
		return std::nullopt;
	} else if (intensity < -100 || intensity > 100) {
		return std::nullopt;
	}
	auto result = WallPaper(id);
	result._accessHash = accessHash;
	result._ownerId = ownerId;
	result._flags = WallPaperFlags::from_raw(flags);
	result._slug = slug;
	result._blurred = (blurred == 1);
	result._backgroundColors = std::move(backgroundColors);
	result._intensity = intensity;
	result._rotation = rotation;
	return result;
}

std::optional<WallPaper> WallPaper::FromLegacySerialized(
		quint64 id,
		quint64 accessHash,
		quint32 flags,
		QString slug) {
	auto result = WallPaper(id);
	result._accessHash = accessHash;
	result._flags = WallPaperFlags::from_raw(RawFromLegacyFlags(flags));
	result._slug = slug;
	if (const auto color = ColorFromString(slug)) {
		result._backgroundColors.push_back(*color);
	}
	return result;
}

std::optional<WallPaper> WallPaper::FromLegacyId(qint32 legacyId) {
	auto result = WallPaper(FromLegacyBackgroundId(legacyId));
	if (!IsCustomWallPaper(result)) {
		result._flags = WallPaperFlag::Default;
	}
	return result;
}

std::optional<WallPaper> WallPaper::FromColorsSlug(const QString &slug) {
	auto colors = ColorsFromString(slug);
	if (colors.empty()) {
		return std::nullopt;
	}
	auto result = CustomWallPaper();
	result._slug = slug;
	result._backgroundColors = std::move(colors);
	return result;
}

WallPaper WallPaper::FromEmojiId(const QString &emojiId) {
	auto result = WallPaper(0);
	result._emojiId = emojiId;
	return result;
}

WallPaper WallPaper::ConstructDefault() {
	auto result = WallPaper(
		kDefaultBackground
	).withPatternIntensity(50).withBackgroundColors({
		QColor(219, 221, 187),
		QColor(107, 165, 135),
		QColor(213, 216, 141),
		QColor(136, 184, 132),
	});
	result._flags |= WallPaperFlag::Default | WallPaperFlag::Pattern;
	return result;
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

bool IsLegacy2DefaultWallPaper(const WallPaper &paper) {
	return (paper.id() == kLegacy2DefaultBackground)
		|| (paper.id() == kIncorrectDefaultBackground);
}

bool IsLegacy3DefaultWallPaper(const WallPaper &paper) {
	return (paper.id() == kLegacy3DefaultBackground);
}

bool IsLegacy4DefaultWallPaper(const WallPaper &paper) {
	return (paper.id() == kLegacy4DefaultBackground);
}

WallPaper DefaultWallPaper() {
	return WallPaper::ConstructDefault();
}

bool IsDefaultWallPaper(const WallPaper &paper) {
	return (paper.id() == kDefaultBackground);
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

QImage GenerateDitheredGradient(const Data::WallPaper &paper) {
	return Ui::GenerateDitheredGradient(
		paper.backgroundColors(),
		paper.gradientRotation());
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
	return WallPaper(
		kTestingDefaultBackground
	).withParamsFrom(DefaultWallPaper());
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
