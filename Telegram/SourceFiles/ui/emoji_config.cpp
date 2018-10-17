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
#include "auth_session.h"

namespace Ui {
namespace Emoji {
namespace {

constexpr auto kSaveRecentEmojiTimeout = 3000;
constexpr auto kUniversalSize = 72;
constexpr auto kImagesPerRow = 32;
constexpr auto kImageRowsPerSprite = 16;

constexpr auto kVersion = 3;

class UniversalImages {
public:
	void ensureLoaded();
	void clear();

	void draw(QPainter &p, EmojiPtr emoji, int size, int x, int y) const;

	QImage generate(int size, int index) const;

private:
	std::vector<QImage> _sprites;

};

auto SizeNormal = -1;
auto SizeLarge = -1;
auto SpritesCount = -1;

std::unique_ptr<Instance> InstanceNormal;
std::unique_ptr<Instance> InstanceLarge;
UniversalImages Universal;

std::map<int, QPixmap> MainEmojiMap;
std::map<int, std::map<int, QPixmap>> OtherEmojiMap;

int RowsCount(int index) {
	if (index + 1 < SpritesCount) {
		return kImageRowsPerSprite;
	}
	const auto count = internal::FullCount()
		- (index * kImagesPerRow * kImageRowsPerSprite);
	return (count / kImagesPerRow)
		+ ((count % kImagesPerRow) ? 1 : 0);
}

QString CacheFileFolder() {
	return cWorkingDir() + "tdata/emoji";
}

QString CacheFilePath(int size, int index) {
	return CacheFileFolder()
		+ "/cache_"
		+ QString::number(size)
		+ '_'
		+ QString::number(index);
}

void SaveToFile(const QImage &image, int size, int index) {
	Expects(image.bytesPerLine() == image.width() * 4);

	QFile f(CacheFilePath(size, index));
	if (!f.open(QIODevice::WriteOnly)) {
		if (!QDir::current().mkpath(CacheFileFolder())
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
		uint32(kVersion),
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

QImage LoadFromFile(int size, int index) {
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
		|| header[0] != kVersion
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

void UniversalImages::ensureLoaded() {
	Expects(SpritesCount > 0);

	if (!_sprites.empty()) {
		return;
	}
	_sprites.reserve(SpritesCount);
	const auto base = qsl(":/gui/emoji/emoji_");
	for (auto i = 0; i != SpritesCount; ++i) {
		auto image = QImage();
		image.load(base + QString::number(i + 1) + ".webp", "WEBP");
		_sprites.push_back(std::move(image));
	}
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

	const auto factored = (size / p.device()->devicePixelRatio());
	const auto large = kUniversalSize;

	PainterHighQualityEnabler hq(p);
	p.drawImage(
		QRect(x, y, factored, factored),
		_sprites[emoji->sprite()],
		QRect(emoji->column() * large, emoji->row() * large, large, large));
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
		PainterHighQualityEnabler hq(p);
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
	SaveToFile(result, size, index);
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

	if (InstanceNormal->cached() && InstanceLarge->cached()) {
		Universal.clear();
	}
}

} // namespace

void Init() {
	internal::Init();

	SizeNormal = ConvertScale(18, cScale() * cIntRetinaFactor());
	SizeLarge = int(ConvertScale(18 * 4 / 3., cScale() * cIntRetinaFactor()));
	const auto count = internal::FullCount();
	const auto persprite = kImagesPerRow * kImageRowsPerSprite;
	SpritesCount = (count / persprite) + ((count % persprite) ? 1 : 0);

	InstanceNormal = std::make_unique<Instance>(SizeNormal);
	InstanceLarge = std::make_unique<Instance>(SizeLarge);
}

void Clear() {
	MainEmojiMap.clear();
	OtherEmojiMap.clear();

	InstanceNormal = nullptr;
	InstanceLarge = nullptr;
}

int GetSizeNormal() {
	Expects(SizeNormal > 0);

	return SizeNormal;
}

int GetSizeLarge() {
	Expects(SizeLarge > 0);

	return SizeLarge;
}

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
}

const QPixmap &SinglePixmap(EmojiPtr emoji, int fontHeight) {
	auto &map = (fontHeight == st::msgFont->height)
		? MainEmojiMap
		: OtherEmojiMap[fontHeight];
	auto i = map.find(emoji->index());
	if (i == end(map)) {
		auto image = QImage(
			SizeNormal + st::emojiPadding * cIntRetinaFactor() * 2,
			fontHeight * cIntRetinaFactor(),
			QImage::Format_ARGB32_Premultiplied);
		image.setDevicePixelRatio(cRetinaFactor());
		image.fill(Qt::transparent);
		{
			QPainter p(&image);
			Draw(
				p,
				emoji,
				SizeNormal,
				st::emojiPadding * cIntRetinaFactor(),
				(fontHeight * cIntRetinaFactor() - SizeNormal) / 2);
		}
		i = map.emplace(
			emoji->index(),
			App::pixmapFromImageInPlace(std::move(image))).first;
	}
	return i->second;
}

void Draw(QPainter &p, EmojiPtr emoji, int size, int x, int y) {
	if (size == SizeNormal) {
		InstanceNormal->draw(p, emoji, x, y);
	} else if (size == SizeLarge) {
		InstanceLarge->draw(p, emoji, x, y);
	} else {
		Unexpected("Size in Ui::Emoji::Draw.");
	}
}

Instance::Instance(int size) : _size(size) {
	readCache();
	if (!cached()) {
		Universal.ensureLoaded();
		generateCache();
	}
}

bool Instance::cached() const {
	return (_sprites.size() == SpritesCount);
}

void Instance::draw(QPainter &p, EmojiPtr emoji, int x, int y) {
	const auto sprite = emoji->sprite();
	if (sprite >= _sprites.size()) {
		Universal.draw(p, emoji, _size, x, y);
		return;
	}
	p.drawPixmap(
		QPoint(x, y),
		_sprites[sprite],
		QRect(emoji->column() * _size, emoji->row() * _size, _size, _size));
}

void Instance::readCache() {
	for (auto i = 0; i != SpritesCount; ++i) {
		auto image = LoadFromFile(_size, i);
		if (image.isNull()) {
			return;
		}
		pushSprite(std::move(image));
	}
}

void Instance::generateCache() {
	const auto size = _size;
	const auto index = _sprites.size();
	auto [left, right] = base::make_binary_guard();
	_generating = std::move(left);
	crl::async([=, guard = std::move(right)]() mutable {
		crl::on_main([
			this,
			image = Universal.generate(size, index),
			guard = std::move(guard)
		]() mutable {
			if (!guard.alive()) {
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

} // namespace Emoji
} // namespace Ui
