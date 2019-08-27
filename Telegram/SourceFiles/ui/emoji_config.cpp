/*
WARNING! All changes made in this file will be lost!
Created from 'empty' by 'codegen_emoji'

This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "emoji_config.h"

#include "chat_helpers/emoji_suggestions_helper.h"
#include "base/bytes.h"
#include "base/openssl_help.h"
#include "base/parse_helper.h"
#include "main/main_session.h"

namespace Ui {
namespace Emoji {
namespace {

constexpr auto kSaveRecentEmojiTimeout = 3000;
constexpr auto kUniversalSize = 72;
constexpr auto kImagesPerRow = 32;
constexpr auto kImageRowsPerSprite = 16;

constexpr auto kSetVersion = uint32(1);
constexpr auto kCacheVersion = uint32(3);
constexpr auto kMaxId = uint32(1 << 8);

constexpr auto kScaleForTouchBar = 150;

const auto kSets = {
	Set{ 0,   0,         0, "Mac",      ":/gui/emoji/set0_preview.webp" },
	Set{ 1, 246, 7'336'383, "Android",  ":/gui/emoji/set1_preview.webp" },
	Set{ 2, 206, 5'038'738, "Twemoji",  ":/gui/emoji/set2_preview.webp" },
	Set{ 3, 238, 6'992'260, "EmojiOne", ":/gui/emoji/set3_preview.webp" },
};

// Right now we can't allow users of Ui::Emoji to create custom sizes.
// Any Instance::Instance() can invalidate Universal.id() and sprites.
// So all Instance::Instance() should happen before async generations.
class Instance {
public:
	explicit Instance(int size);

	bool cached() const;
	void draw(QPainter &p, EmojiPtr emoji, int x, int y);

private:
	void readCache();
	void generateCache();
	void checkUniversalImages();
	void pushSprite(QImage &&data);

	int _id = 0;
	int _size = 0;
	std::vector<QPixmap> _sprites;
	base::binary_guard _generating;

};

auto SizeNormal = -1;
auto SizeLarge = -1;
auto SpritesCount = -1;

auto InstanceNormal = std::unique_ptr<Instance>();
auto InstanceLarge = std::unique_ptr<Instance>();
auto Universal = std::shared_ptr<UniversalImages>();
auto CanClearUniversal = false;
auto Updates = rpl::event_stream<>();
auto UpdatesRecent = rpl::event_stream<>();

#if defined Q_OS_MAC && !defined OS_MAC_OLD
auto TouchbarSize = -1;
auto TouchbarInstance = std::unique_ptr<Instance>();
auto TouchbarEmoji = (Instance*)nullptr;
#endif

auto MainEmojiMap = std::map<int, QPixmap>();
auto OtherEmojiMap = base::flat_map<int, std::map<int, QPixmap>>();

int RowsCount(int index) {
	if (index + 1 < SpritesCount) {
		return kImageRowsPerSprite;
	}
	const auto count = internal::FullCount()
		- (index * kImagesPerRow * kImageRowsPerSprite);
	return (count / kImagesPerRow)
		+ ((count % kImagesPerRow) ? 1 : 0);
}

QString CacheFileNameMask(int size) {
	return "cache_" + QString::number(size) + '_';
}

QString CacheFilePath(int size, int index) {
	return internal::CacheFileFolder()
		+ '/'
		+ CacheFileNameMask(size)
		+ QString::number(index);
}

QString CurrentSettingPath() {
	return internal::CacheFileFolder() + "/current";
}

bool IsValidSetId(int id) {
	return (id == 0) || (id > 0 && id < kMaxId);
}

uint32 ComputeVersion(int id) {
	Expects(IsValidSetId(id));

	static_assert(kCacheVersion > 0 && kCacheVersion < (1 << 16));
	static_assert(kSetVersion > 0 && kSetVersion < (1 << 8));

	auto result = uint32(kCacheVersion);
	if (!id) {
		return result;
	}
	result |= (uint32(id) << 24) | (uint32(kSetVersion) << 16);
	return result;
}

int ReadCurrentSetId() {
	const auto path = CurrentSettingPath();
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return 0;
	}
	auto stream = QDataStream(&file);
	stream.setVersion(QDataStream::Qt_5_1);
	auto id = qint32(0);
	stream >> id;
	return (stream.status() == QDataStream::Ok && IsValidSetId(id))
		? id
		: 0;
}

void SwitchToSetPrepared(int id, std::shared_ptr<UniversalImages> images) {
	auto setting = QFile(CurrentSettingPath());
	if (!id) {
		setting.remove();
	} else if (setting.open(QIODevice::WriteOnly)) {
		auto stream = QDataStream(&setting);
		stream.setVersion(QDataStream::Qt_5_1);
		stream << qint32(id);
	}
	Universal = std::move(images);
	CanClearUniversal = false;
	MainEmojiMap.clear();
	OtherEmojiMap.clear();
	Updates.fire({});
}

void ClearCurrentSetIdSync() {
	Expects(Universal != nullptr);

	const auto id = Universal->id();
	if (!id) {
		return;
	}
	QDir(internal::SetDataPath(id)).removeRecursively();

	const auto newId = 0;
	auto universal = std::make_shared<UniversalImages>(newId);
	universal->ensureLoaded();
	SwitchToSetPrepared(newId, std::move(universal));
}

void SaveToFile(int id, const QImage &image, int size, int index) {
	Expects(image.bytesPerLine() == image.width() * 4);

	QFile f(CacheFilePath(size, index));
	if (!f.open(QIODevice::WriteOnly)) {
		if (!QDir::current().mkpath(internal::CacheFileFolder())
			|| !f.open(QIODevice::WriteOnly)) {
			LOG(("App Error: Could not open emoji cache '%1' for size %2_%3"
				).arg(f.fileName()
				).arg(size
				).arg(index));
			return;
		}
	}
	const auto write = [&](bytes::const_span data) {
		return f.write(
			reinterpret_cast<const char*>(data.data()),
			data.size()
		) == data.size();
	};
	const uint32 header[] = {
		uint32(ComputeVersion(id)),
		uint32(size),
		uint32(image.width()),
		uint32(image.height()),
	};
	const auto data = bytes::const_span(
		reinterpret_cast<const bytes::type*>(image.bits()),
		image.width() * image.height() * 4);
	if (!write(bytes::make_span(header))
		|| !write(data)
		|| !write(openssl::Sha256(bytes::make_span(header), data))
		|| false) {
		LOG(("App Error: Could not write emoji cache '%1' for size %2"
			).arg(f.fileName()
			).arg(size));
	}
}

QImage LoadFromFile(int id, int size, int index) {
	const auto rows = RowsCount(index);
	const auto width = kImagesPerRow * size;
	const auto height = rows * size;
	const auto fileSize = 4 * sizeof(uint32)
		+ (width * height * 4)
		+ openssl::kSha256Size;
	QFile f(CacheFilePath(size, index));
	if (!f.exists()
		|| f.size() != fileSize
		|| !f.open(QIODevice::ReadOnly)) {
		return QImage();
	}
	const auto read = [&](bytes::span data) {
		return f.read(
			reinterpret_cast<char*>(data.data()),
			data.size()
		) == data.size();
	};
	uint32 header[4] = { 0 };
	if (!read(bytes::make_span(header))
		|| header[0] != ComputeVersion(id)
		|| header[1] != size
		|| header[2] != width
		|| header[3] != height) {
		return QImage();
	}
	auto result = QImage(
		width,
		height,
		QImage::Format_ARGB32_Premultiplied);
	Assert(result.bytesPerLine() == width * 4);
	const auto data = bytes::make_span(
		reinterpret_cast<bytes::type*>(result.bits()),
		width * height * 4);
	auto signature = bytes::vector(openssl::kSha256Size);
	if (!read(data)
		|| !read(signature)
		//|| (bytes::compare(
		//	signature,
		//	openssl::Sha256(bytes::make_span(header), data)) != 0)
		|| false) {
		return QImage();
	}
	crl::async([=, signature = std::move(signature)] {
		// This should not happen (invalid signature),
		// so we delay this check and fix only the next launch.
		const auto data = bytes::make_span(
			reinterpret_cast<const bytes::type*>(result.bits()),
			width * height * 4);
		const auto result = bytes::compare(
			signature,
			openssl::Sha256(bytes::make_span(header), data));
		if (result != 0) {
			QFile(CacheFilePath(size, index)).remove();
		}
	});
	return result;
}

std::vector<QImage> LoadSprites(int id) {
	Expects(IsValidSetId(id));
	Expects(SpritesCount > 0);

	auto result = std::vector<QImage>();
	const auto folder = (id != 0)
		? internal::SetDataPath(id) + '/'
		: qsl(":/gui/emoji/");
	const auto base = folder + "emoji_";
	return ranges::view::ints(
		0,
		SpritesCount
	) | ranges::view::transform([&](int index) {
		return base + QString::number(index + 1) + ".webp";
	}) | ranges::view::transform([](const QString &path) {
		return QImage(path, "WEBP").convertToFormat(
			QImage::Format_ARGB32_Premultiplied);
	}) | ranges::to_vector;
}

bool ValidateConfig(int id) {
	Expects(IsValidSetId(id));

	if (!id) {
		return true;
	}
	constexpr auto kSizeLimit = 65536;
	auto config = QFile(internal::SetDataPath(id) + "/config.json");
	if (!config.open(QIODevice::ReadOnly) || config.size() > kSizeLimit) {
		return false;
	}
	auto error = QJsonParseError{ 0, QJsonParseError::NoError };
	const auto document = QJsonDocument::fromJson(
		base::parse::stripComments(config.readAll()),
		&error);
	config.close();
	if (error.error != QJsonParseError::NoError) {
		return false;
	}
	if (document.object()["id"].toInt() != id
		|| document.object()["version"].toInt() != kSetVersion) {
		return false;
	}
	return true;
}

std::vector<QImage> LoadAndValidateSprites(int id) {
	Expects(IsValidSetId(id));
	Expects(SpritesCount > 0);

	if (!ValidateConfig(id)) {
		return {};
	}
	auto result = LoadSprites(id);
	const auto sizes = ranges::view::ints(
		0,
		SpritesCount
	) | ranges::view::transform([](int index) {
		return QSize(
			kImagesPerRow * kUniversalSize,
			RowsCount(index) * kUniversalSize);
	});
	const auto good = ranges::view::zip_with(
		[](const QImage &data, QSize size) { return data.size() == size; },
		result,
		sizes);
	if (ranges::find(good, false) != end(good)) {
		return {};
	}
	return result;
}

void AppendPartToResult(TextWithEntities &result, const QChar *start, const QChar *from, const QChar *to) {
	if (to <= from) {
		return;
	}
	for (auto &entity : result.entities) {
		if (entity.offset() >= to - start) break;
		if (entity.offset() + entity.length() < from - start) continue;
		if (entity.offset() >= from - start) {
			entity.extendToLeft(from - start - result.text.size());
		}
		if (entity.offset() + entity.length() <= to - start) {
			entity.shrinkFromRight(from - start - result.text.size());
		}
	}
	result.text.append(from, to - from);
}

bool IsReplacementPart(ushort ch) {
	return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || (ch == '-') || (ch == '+') || (ch == '_');
}

EmojiPtr FindReplacement(const QChar *start, const QChar *end, int *outLength) {
	if (start != end && *start == ':') {
		auto maxLength = GetSuggestionMaxLength();
		for (auto till = start + 1; till != end; ++till) {
			if (*till == ':') {
				auto text = QString::fromRawData(start, till + 1 - start);
				auto emoji = GetSuggestionEmoji(QStringToUTF16(text));
				auto result = Find(QStringFromUTF16(emoji));
				if (result) {
					if (outLength) *outLength = (till + 1 - start);
				}
				return result;
			} else if (!IsReplacementPart(till->unicode()) || (till - start) > maxLength) {
				break;
			}
		}
	}
	return internal::FindReplace(start, end, outLength);
}

void ClearUniversalChecked() {
	Expects(InstanceNormal != nullptr && InstanceLarge != nullptr);

	if (CanClearUniversal
		&& Universal
		&& InstanceNormal->cached()
		&& InstanceLarge->cached()) {
		Universal->clear();
	}
}

} // namespace

namespace internal {

QString CacheFileFolder() {
	return cWorkingDir() + "tdata/emoji";
}

QString SetDataPath(int id) {
	Expects(IsValidSetId(id) && id != 0);

	return CacheFileFolder() + "/set" + QString::number(id);
}

} // namespace internal

UniversalImages::UniversalImages(int id) : _id(id) {
	Expects(IsValidSetId(id));
}

int UniversalImages::id() const {
	return _id;
}

bool UniversalImages::ensureLoaded() {
	Expects(SpritesCount > 0);

	if (!_sprites.empty()) {
		return true;
	}
	_sprites = LoadAndValidateSprites(_id);
	return !_sprites.empty();
}

void UniversalImages::clear() {
	_sprites.clear();
}

void UniversalImages::draw(
		QPainter &p,
		EmojiPtr emoji,
		int size,
		int x,
		int y) const {
	Expects(emoji->sprite() < _sprites.size());

	const auto large = kUniversalSize;
	const auto &original = _sprites[emoji->sprite()];
	const auto data = original.bits();
	const auto stride = original.bytesPerLine();
	const auto format = original.format();
	const auto row = emoji->row();
	const auto column = emoji->column();
	auto single = QImage(
		data + (row * kImagesPerRow * large + column) * large * 4,
		large,
		large,
		stride,
		format
	).scaled(
		size,
		size,
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation);
	single.setDevicePixelRatio(p.device()->devicePixelRatio());
	p.drawImage(x, y, single);
}

QImage UniversalImages::generate(int size, int index) const {
	Expects(size > 0);
	Expects(index < _sprites.size());

	const auto rows = RowsCount(index);
	const auto large = kUniversalSize;
	const auto &original = _sprites[index];
	const auto data = original.bits();
	const auto stride = original.bytesPerLine();
	const auto format = original.format();
	auto result = QImage(
		size * kImagesPerRow,
		size * rows,
		QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::transparent);
	{
		QPainter p(&result);
		for (auto y = 0; y != rows; ++y) {
			for (auto x = 0; x != kImagesPerRow; ++x) {
				const auto single = QImage(
					data + (y * kImagesPerRow * large + x) * large * 4,
					large,
					large,
					stride,
					format
				).scaled(
					size,
					size,
					Qt::IgnoreAspectRatio,
					Qt::SmoothTransformation);
				p.drawImage(
					x * size,
					y * size,
					single);
			}
		}
	}
	SaveToFile(_id, result, size, index);
	return result;
}

void Init() {
	internal::Init();

	const auto count = internal::FullCount();
	const auto persprite = kImagesPerRow * kImageRowsPerSprite;
	SpritesCount = (count / persprite) + ((count % persprite) ? 1 : 0);

	SizeNormal = ConvertScale(18, cScale() * cIntRetinaFactor());
	SizeLarge = int(ConvertScale(18 * 4 / 3., cScale() * cIntRetinaFactor()));
	Universal = std::make_shared<UniversalImages>(ReadCurrentSetId());
	CanClearUniversal = false;

	InstanceNormal = std::make_unique<Instance>(SizeNormal);
	InstanceLarge = std::make_unique<Instance>(SizeLarge);

#if defined Q_OS_MAC && !defined OS_MAC_OLD
	if (cScale() != kScaleForTouchBar) {
		TouchbarSize = int(ConvertScale(18 * 4 / 3.,
			kScaleForTouchBar * cIntRetinaFactor()));
		TouchbarInstance = std::make_unique<Instance>(TouchbarSize);
		TouchbarEmoji = TouchbarInstance.get();
	} else {
		TouchbarEmoji = InstanceLarge.get();
	}
#endif
}

void Clear() {
	MainEmojiMap.clear();
	OtherEmojiMap.clear();

	InstanceNormal = nullptr;
	InstanceLarge = nullptr;
#if defined Q_OS_MAC && !defined OS_MAC_OLD
	TouchbarInstance = nullptr;
	TouchbarEmoji = nullptr;
#endif
}

void ClearIrrelevantCache() {
	Expects(SizeNormal > 0);
	Expects(SizeLarge > 0);

	crl::async([] {
		const auto folder = internal::CacheFileFolder();
		const auto list = QDir(folder).entryList(QDir::Files);
		const auto good1 = CacheFileNameMask(SizeNormal);
		const auto good2 = CacheFileNameMask(SizeLarge);
		const auto good3full = CurrentSettingPath();
		for (const auto &name : list) {
			if (!name.startsWith(good1) && !name.startsWith(good2)) {
				const auto full = folder + '/' + name;
				if (full != good3full) {
					QFile(full).remove();
				}
			}
		}
	});
}

tr::phrase<> CategoryTitle(int index) {
	switch (index) {
	case 1: return tr::lng_emoji_category1;
	case 2: return tr::lng_emoji_category2;
	case 3: return tr::lng_emoji_category3;
	case 4: return tr::lng_emoji_category4;
	case 5: return tr::lng_emoji_category5;
	case 6: return tr::lng_emoji_category6;
	case 7: return tr::lng_emoji_category7;
	}
	Unexpected("Index in CategoryTitle.");
}

std::vector<Set> Sets() {
	return kSets | ranges::to_vector;
}

int CurrentSetId() {
	Expects(Universal != nullptr);

	return Universal->id();
}

void SwitchToSet(int id, Fn<void(bool)> callback) {
	Expects(IsValidSetId(id));

	if (Universal && Universal->id() == id) {
		callback(true);
		return;
	}
	crl::async([=] {
		auto universal = std::make_shared<UniversalImages>(id);
		if (!universal->ensureLoaded()) {
			crl::on_main([=] {
				callback(false);
			});
		} else {
			crl::on_main([=, universal = std::move(universal)]() mutable {
				SwitchToSetPrepared(id, std::move(universal));
				callback(true);
			});
		}
	});
}

bool SetIsReady(int id) {
	Expects(IsValidSetId(id));

	if (!id) {
		return true;
	}
	const auto folder = internal::SetDataPath(id) + '/';
	auto names = ranges::view::ints(
		0,
		SpritesCount + 1
	) | ranges::view::transform([](int index) {
		return index
			? "emoji_" + QString::number(index) + ".webp"
			: QString("config.json");
	});
	const auto bad = ranges::find_if(names, [&](const QString &name) {
		return !QFile(folder + name).exists();
	});
	return (bad == names.end());
}

rpl::producer<> Updated() {
	return Updates.events();
}

int GetSizeNormal() {
	Expects(SizeNormal > 0);

	return SizeNormal;
}

int GetSizeLarge() {
	Expects(SizeLarge > 0);

	return SizeLarge;
}

#if defined Q_OS_MAC && !defined OS_MAC_OLD
int GetSizeTouchbar() {
	return (cScale() == kScaleForTouchBar)
		? GetSizeLarge()
		: TouchbarSize;
}
#endif

int One::variantsCount() const {
	return hasVariants() ? 5 : 0;
}

int One::variantIndex(EmojiPtr variant) const {
	return (variant - original());
}

EmojiPtr One::variant(int index) const {
	return (index >= 0 && index <= variantsCount()) ? (original() + index) : this;
}

QString IdFromOldKey(uint64 oldKey) {
	auto code = uint32(oldKey >> 32);
	auto code2 = uint32(oldKey & 0xFFFFFFFFLLU);
	if (!code && code2) {
		code = base::take(code2);
	}
	if ((code & 0xFFFF0000U) != 0xFFFF0000U) { // code and code2 contain the whole id
		auto result = QString();
		result.reserve(4);
		auto addCode = [&result](uint32 code) {
			if (auto high = (code >> 16)) {
				result.append(QChar(static_cast<ushort>(high & 0xFFFFU)));
			}
			result.append(QChar(static_cast<ushort>(code & 0xFFFFU)));
		};
		addCode(code);
		if (code2) addCode(code2);
		return result;
	}

	// old sequence
	auto sequenceIndex = int(code & 0xFFFFU);
	switch (sequenceIndex) {
	case 0: return QString::fromUtf8("\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa7");
	case 1: return QString::fromUtf8("\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa7\xe2\x80\x8d\xf0\x9f\x91\xa6");
	case 2: return QString::fromUtf8("\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa6\xe2\x80\x8d\xf0\x9f\x91\xa6");
	case 3: return QString::fromUtf8("\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa7\xe2\x80\x8d\xf0\x9f\x91\xa7");
	case 4: return QString::fromUtf8("\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa6");
	case 5: return QString::fromUtf8("\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa7");
	case 6: return QString::fromUtf8("\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa7\xe2\x80\x8d\xf0\x9f\x91\xa6");
	case 7: return QString::fromUtf8("\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa6\xe2\x80\x8d\xf0\x9f\x91\xa6");
	case 8: return QString::fromUtf8("\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa7\xe2\x80\x8d\xf0\x9f\x91\xa7");
	case 9: return QString::fromUtf8("\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa6");
	case 10: return QString::fromUtf8("\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa7");
	case 11: return QString::fromUtf8("\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa7\xe2\x80\x8d\xf0\x9f\x91\xa6");
	case 12: return QString::fromUtf8("\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa6\xe2\x80\x8d\xf0\x9f\x91\xa6");
	case 13: return QString::fromUtf8("\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa7\xe2\x80\x8d\xf0\x9f\x91\xa7");
	case 14: return QString::fromUtf8("\xf0\x9f\x91\xa9\xe2\x80\x8d\xe2\x9d\xa4\xef\xb8\x8f\xe2\x80\x8d\xf0\x9f\x91\xa9");
	case 15: return QString::fromUtf8("\xf0\x9f\x91\xa8\xe2\x80\x8d\xe2\x9d\xa4\xef\xb8\x8f\xe2\x80\x8d\xf0\x9f\x91\xa8");
	case 16: return QString::fromUtf8("\xf0\x9f\x91\xa9\xe2\x80\x8d\xe2\x9d\xa4\xef\xb8\x8f\xe2\x80\x8d\xf0\x9f\x92\x8b\xe2\x80\x8d\xf0\x9f\x91\xa9");
	case 17: return QString::fromUtf8("\xf0\x9f\x91\xa8\xe2\x80\x8d\xe2\x9d\xa4\xef\xb8\x8f\xe2\x80\x8d\xf0\x9f\x92\x8b\xe2\x80\x8d\xf0\x9f\x91\xa8");
	case 18: return QString::fromUtf8("\xf0\x9f\x91\x81\xe2\x80\x8d\xf0\x9f\x97\xa8");
	}
	return QString();
}

void ReplaceInText(TextWithEntities &result) {
	auto newText = TextWithEntities();
	newText.entities = std::move(result.entities);
	auto currentEntity = newText.entities.begin();
	auto entitiesEnd = newText.entities.end();
	auto emojiStart = result.text.constData();
	auto emojiEnd = emojiStart;
	auto end = emojiStart + result.text.size();
	auto canFindEmoji = true;
	for (auto ch = emojiEnd; ch != end;) {
		auto emojiLength = 0;
		auto emoji = canFindEmoji ? FindReplacement(ch, end, &emojiLength) : nullptr;
		auto newEmojiEnd = ch + emojiLength;

		while (currentEntity != entitiesEnd && ch >= emojiStart + currentEntity->offset() + currentEntity->length()) {
			++currentEntity;
		}
		if (emoji &&
			(ch == emojiStart || !ch->isLetterOrNumber() || !(ch - 1)->isLetterOrNumber()) &&
			(newEmojiEnd == end || !newEmojiEnd->isLetterOrNumber() || newEmojiEnd == emojiStart || !(newEmojiEnd - 1)->isLetterOrNumber()) &&
			(currentEntity == entitiesEnd || (ch < emojiStart + currentEntity->offset() && newEmojiEnd <= emojiStart + currentEntity->offset()) || (ch >= emojiStart + currentEntity->offset() + currentEntity->length() && newEmojiEnd > emojiStart + currentEntity->offset() + currentEntity->length()))
			) {
			if (newText.text.isEmpty()) newText.text.reserve(result.text.size());

			AppendPartToResult(newText, emojiStart, emojiEnd, ch);

			if (emoji->hasVariants()) {
				auto it = cEmojiVariants().constFind(emoji->nonColoredId());
				if (it != cEmojiVariants().cend()) {
					emoji = emoji->variant(it.value());
				}
			}
			newText.text.append(emoji->text());

			ch = emojiEnd = newEmojiEnd;
			canFindEmoji = true;
		} else {
			if (internal::IsReplaceEdge(ch)) {
				canFindEmoji = true;
			} else {
				canFindEmoji = false;
			}
			++ch;
		}
	}
	if (newText.text.isEmpty()) {
		result.entities = std::move(newText.entities);
	} else {
		AppendPartToResult(newText, emojiStart, emojiEnd, end);
		result = std::move(newText);
	}
}

RecentEmojiPack &GetRecent() {
	if (cRecentEmoji().isEmpty()) {
		RecentEmojiPack result;
		auto haveAlready = [&result](EmojiPtr emoji) {
			for (auto &row : result) {
				if (row.first->id() == emoji->id()) {
					return true;
				}
			}
			return false;
		};
		if (!cRecentEmojiPreload().isEmpty()) {
			auto preload = cRecentEmojiPreload();
			cSetRecentEmojiPreload(RecentEmojiPreload());
			result.reserve(preload.size());
			for (auto i = preload.cbegin(), e = preload.cend(); i != e; ++i) {
				if (auto emoji = Ui::Emoji::Find(i->first)) {
					if (!haveAlready(emoji)) {
						result.push_back(qMakePair(emoji, i->second));
					}
				}
			}
		}
		auto defaultRecent = {
			0xD83DDE02LLU,
			0xD83DDE18LLU,
			0x2764LLU,
			0xD83DDE0DLLU,
			0xD83DDE0ALLU,
			0xD83DDE01LLU,
			0xD83DDC4DLLU,
			0x263ALLU,
			0xD83DDE14LLU,
			0xD83DDE04LLU,
			0xD83DDE2DLLU,
			0xD83DDC8BLLU,
			0xD83DDE12LLU,
			0xD83DDE33LLU,
			0xD83DDE1CLLU,
			0xD83DDE48LLU,
			0xD83DDE09LLU,
			0xD83DDE03LLU,
			0xD83DDE22LLU,
			0xD83DDE1DLLU,
			0xD83DDE31LLU,
			0xD83DDE21LLU,
			0xD83DDE0FLLU,
			0xD83DDE1ELLU,
			0xD83DDE05LLU,
			0xD83DDE1ALLU,
			0xD83DDE4ALLU,
			0xD83DDE0CLLU,
			0xD83DDE00LLU,
			0xD83DDE0BLLU,
			0xD83DDE06LLU,
			0xD83DDC4CLLU,
			0xD83DDE10LLU,
			0xD83DDE15LLU,
		};
		for (auto oldKey : defaultRecent) {
			if (result.size() >= kRecentLimit) break;

			if (auto emoji = Ui::Emoji::FromOldKey(oldKey)) {
				if (!haveAlready(emoji)) {
					result.push_back(qMakePair(emoji, 1));
				}
			}
		}
		cSetRecentEmoji(result);
	}
	return cRefRecentEmoji();
}

void AddRecent(EmojiPtr emoji) {
	auto &recent = GetRecent();
	auto i = recent.begin(), e = recent.end();
	for (; i != e; ++i) {
		if (i->first == emoji) {
			++i->second;
			if (i->second > 0x8000) {
				for (auto j = recent.begin(); j != e; ++j) {
					if (j->second > 1) {
						j->second /= 2;
					} else {
						j->second = 1;
					}
				}
			}
			for (; i != recent.begin(); --i) {
				if ((i - 1)->second > i->second) {
					break;
				}
				qSwap(*i, *(i - 1));
			}
			break;
		}
	}
	if (i == e) {
		while (recent.size() >= kRecentLimit) {
			recent.pop_back();
		}
		recent.push_back(qMakePair(emoji, 1));
		for (i = recent.end() - 1; i != recent.begin(); --i) {
			if ((i - 1)->second > i->second) {
				break;
			}
			qSwap(*i, *(i - 1));
		}
	}
	UpdatesRecent.fire({});
}

rpl::producer<> UpdatedRecent() {
	return UpdatesRecent.events();
}

const QPixmap &SinglePixmap(EmojiPtr emoji, int fontHeight) {
	auto &map = (fontHeight == st::msgFont->height * cIntRetinaFactor())
		? MainEmojiMap
		: OtherEmojiMap[fontHeight];
	auto i = map.find(emoji->index());
	if (i != end(map)) {
		return i->second;
	}
	auto image = QImage(
		SizeNormal + st::emojiPadding * 2,
		fontHeight,
		QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(cRetinaFactor());
	image.fill(Qt::transparent);
	{
		QPainter p(&image);
		PainterHighQualityEnabler hq(p);
		Draw(
			p,
			emoji,
			SizeNormal,
			st::emojiPadding * cIntRetinaFactor(),
			(fontHeight - SizeNormal) / 2);
	}
	return map.emplace(
		emoji->index(),
		App::pixmapFromImageInPlace(std::move(image))
	).first->second;
}

void Draw(QPainter &p, EmojiPtr emoji, int size, int x, int y) {
#if defined Q_OS_MAC && !defined OS_MAC_OLD
	const auto s = (cScale() == kScaleForTouchBar)
		? SizeLarge
		: TouchbarSize;
	if (size == s) {
		TouchbarEmoji->draw(p, emoji, x, y);
		return;
	}
#endif
	if (size == SizeNormal) {
		InstanceNormal->draw(p, emoji, x, y);
	} else if (size == SizeLarge) {
		InstanceLarge->draw(p, emoji, x, y);
	} else {
		Unexpected("Size in Ui::Emoji::Draw.");
	}
}

Instance::Instance(int size) : _id(Universal->id()), _size(size) {
	Expects(Universal != nullptr);

	readCache();
	if (!cached()) {
		generateCache();
	}
}

bool Instance::cached() const {
	Expects(Universal != nullptr);

	return (Universal->id() == _id) && (_sprites.size() == SpritesCount);
}

void Instance::draw(QPainter &p, EmojiPtr emoji, int x, int y) {
	if (Universal && Universal->id() != _id) {
		generateCache();
	}
	const auto sprite = emoji->sprite();
	if (sprite >= _sprites.size()) {
		Assert(Universal != nullptr);
		Universal->draw(p, emoji, _size, x, y);
		return;
	}
	p.drawPixmap(
		QPoint(x, y),
		_sprites[sprite],
		QRect(emoji->column() * _size, emoji->row() * _size, _size, _size));
}

void Instance::readCache() {
	for (auto i = 0; i != SpritesCount; ++i) {
		auto image = LoadFromFile(_id, _size, i);
		if (image.isNull()) {
			return;
		}
		pushSprite(std::move(image));
	}
}

void Instance::checkUniversalImages() {
	Expects(Universal != nullptr);

	if (_id != Universal->id()) {
		_id = Universal->id();
		_generating = nullptr;
		_sprites.clear();
	}
	if (!Universal->ensureLoaded() && Universal->id() != 0) {
		ClearCurrentSetIdSync();
	}
}

void Instance::generateCache() {
	checkUniversalImages();

	const auto size = _size;
	const auto index = _sprites.size();
	crl::async([
		=,
		universal = Universal,
		guard = _generating.make_guard()
	]() mutable {
		crl::on_main(std::move(guard), [
			=,
			image = universal->generate(size, index)
		]() mutable {
			if (universal != Universal) {
				return;
			}
			pushSprite(std::move(image));
			if (cached()) {
				ClearUniversalChecked();
			} else {
				generateCache();
			}
		});
	});
}

void Instance::pushSprite(QImage &&data) {
	_sprites.push_back(App::pixmapFromImageInPlace(std::move(data)));
	_sprites.back().setDevicePixelRatio(cRetinaFactor());
}

const std::shared_ptr<UniversalImages> &SourceImages() {
	return Universal;
}

void ClearSourceImages(const std::shared_ptr<UniversalImages> &images) {
	if (images == Universal) {
		CanClearUniversal = true;
		ClearUniversalChecked();
	}
}

void ReplaceSourceImages(std::shared_ptr<UniversalImages> images) {
	Expects(images != nullptr);

	if (Universal->id() == images->id()) {
		Universal = std::move(images);
	}
}

} // namespace Emoji
} // namespace Ui
