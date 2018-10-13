/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "codegen/emoji/generator.h"

#include <QtCore/QtPlugin>
#include <QtCore/QBuffer>
#include <QtGui/QFontDatabase>
#include <QtGui/QGuiApplication>
#include <QtGui/QImage>
#include <QtGui/QPainter>
#include <QtCore/QDir>

#ifdef SUPPORT_IMAGE_GENERATION
Q_IMPORT_PLUGIN(QWebpPlugin)
#ifdef Q_OS_MAC
Q_IMPORT_PLUGIN(QCocoaIntegrationPlugin)
#elif defined Q_OS_WIN
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)
#else // !Q_OS_MAC && !Q_OS_WIN
Q_IMPORT_PLUGIN(QXcbIntegrationPlugin)
#endif // !Q_OS_MAC && !Q_OS_WIN
#endif // SUPPORT_IMAGE_GENERATION

namespace codegen {
namespace emoji {
namespace {

constexpr auto kErrorCantWritePath = 851;

constexpr auto kOriginalBits = 12;
constexpr auto kIdSizeBits = 6;
constexpr auto kColumnBits = 5;
constexpr auto kRowBits = 7;

common::ProjectInfo Project = {
	"codegen_emoji",
	"empty",
	false, // forceReGenerate
};

QRect computeSourceRect(const QImage &image) {
	auto size = image.width();
	auto result = QRect(2, 2, size - 4, size - 4);
	auto top = 1, bottom = 1, left = 1, right = 1;
	auto rgbBits = reinterpret_cast<const QRgb*>(image.constBits());
	for (auto i = 0; i != size; ++i) {
		if (rgbBits[i] > 0
			|| rgbBits[(size - 1) * size + i] > 0
			|| rgbBits[i * size] > 0
			|| rgbBits[i * size + (size - 1)] > 0) {
			logDataError() << "Bad border.";
			return QRect();
		}
		if (rgbBits[1 * size + i] > 0) {
			top = -1;
		} else if (top > 0 && rgbBits[2 * size + i] > 0) {
			top = 0;
		}
		if (rgbBits[(size - 2) * size + i] > 0) {
			bottom = -1;
		} else if (bottom > 0 && rgbBits[(size - 3) * size + i] > 0) {
			bottom = 0;
		}
		if (rgbBits[i * size + 1] > 0) {
			left = -1;
		} else if (left > 0 && rgbBits[i * size + 2] > 0) {
			left = 0;
		}
		if (rgbBits[i * size + (size - 2)] > 0) {
			right = -1;
		} else if (right > 0 && rgbBits[i * size + (size - 3)] > 0) {
			right = 0;
		}
	}
	if (top < 0) {
		if (bottom <= 0) {
			logDataError() << "Bad vertical :(";
			return QRect();
		} else {
			result.setY(result.y() + 1);
		}
	} else if (bottom < 0) {
		if (top <= 0) {
			logDataError() << "Bad vertical :(";
			return QRect();
		} else {
			result.setY(result.y() - 1);
		}
	}
	if (left < 0) {
		if (right <= 0) {
			logDataError() << "Bad horizontal :(";
			return QRect();
		} else {
			result.setX(result.x() + 1);
		}
	} else if (right < 0) {
		if (left <= 0) {
			logDataError() << "Bad horizontal :(";
			return QRect();
		} else {
			result.setX(result.x() - 1);
		}
	}
	return result;
}

uint32 Crc32Table[256];
class Crc32Initializer {
public:
	Crc32Initializer() {
		uint32 poly = 0x04C11DB7U;
		for (auto i = 0; i != 256; ++i) {
			Crc32Table[i] = reflect(i, 8) << 24;
			for (auto j = 0; j != 8; ++j) {
				Crc32Table[i] = (Crc32Table[i] << 1) ^ (Crc32Table[i] & (1 << 31) ? poly : 0);
			}
			Crc32Table[i] = reflect(Crc32Table[i], 32);
		}
	}

private:
	uint32 reflect(uint32 val, char ch) {
		uint32 result = 0;
		for (int i = 1; i < (ch + 1); ++i) {
			if (val & 1) {
				result |= 1 << (ch - i);
			}
			val >>= 1;
		}
		return result;
	}

};

uint32 countCrc32(const void *data, std::size_t size) {
	static Crc32Initializer InitTable;

	auto buffer = static_cast<const unsigned char*>(data);
	auto result = uint32(0xFFFFFFFFU);
	for (auto i = std::size_t(0); i != size; ++i) {
		result = (result >> 8) ^ Crc32Table[(result & 0xFFU) ^ buffer[i]];
	}
	return (result ^ 0xFFFFFFFFU);
}

} // namespace

Generator::Generator(const Options &options) : project_(Project)
#ifdef SUPPORT_IMAGE_GENERATION
, writeImages_(options.writeImages)
#endif // SUPPORT_IMAGE_GENERATION
, data_(PrepareData())
, replaces_(PrepareReplaces(options.replacesPath)) {
	QDir dir(options.outputPath);
	if (!dir.mkpath(".")) {
		common::logError(kErrorCantWritePath, "Command Line") << "can not open path for writing: " << dir.absolutePath().toStdString();
		data_ = Data();
	}
	if (!CheckAndConvertReplaces(replaces_, data_)) {
		replaces_ = Replaces(replaces_.filename);
	}

	outputPath_ = dir.absolutePath() + "/emoji";
	spritePath_ = dir.absolutePath() + "/emoji";
	suggestionsPath_ = dir.absolutePath() + "/emoji_suggestions_data";
}

int Generator::generate() {
	if (data_.list.empty() || replaces_.list.isEmpty()) {
		return -1;
	}

#ifdef SUPPORT_IMAGE_GENERATION
	if (writeImages_) {
		return writeImages() ? 0 : -1;
	}
#endif // SUPPORT_IMAGE_GENERATION

	if (!writeSource()) {
		return -1;
	}
	if (!writeHeader()) {
		return -1;
	}
	if (!writeSuggestionsSource()) {
		return -1;
	}
	if (!writeSuggestionsHeader()) {
		return -1;
	}

	return 0;
}

constexpr auto kEmojiInRow = 32;
constexpr auto kEmojiRowsInFile = 16;
constexpr auto kEmojiQuality = 99;
constexpr auto kEmojiSize = 72;
constexpr auto kEmojiFontSize = 72;
constexpr auto kEmojiDelta = 67;
constexpr auto kScaleFromLarge = true;

#ifdef SUPPORT_IMAGE_GENERATION
QImage Generator::generateImage(int imageIndex) {
	constexpr auto kLargeEmojiSize = 180;
	constexpr auto kLargeEmojiFontSize = 180;
	constexpr auto kLargeEmojiDelta = 167;

	auto emojiCount = int(data_.list.size());
	auto columnsCount = kEmojiInRow;
	auto fullRowsCount = (emojiCount / columnsCount) + ((emojiCount % columnsCount) ? 1 : 0);
	auto imagesCount = (fullRowsCount / kEmojiRowsInFile) + ((fullRowsCount % kEmojiRowsInFile) ? 1 : 0);

	auto sourceSize = kScaleFromLarge ? kLargeEmojiSize : kEmojiSize;

	auto font = QGuiApplication::font();
	font.setFamily(QStringLiteral("Apple Color Emoji"));
	font.setPixelSize(kScaleFromLarge ? kLargeEmojiFontSize : kEmojiFontSize);

	auto singleSize = 4 + sourceSize;
	const auto inFileShift = (imageIndex * kEmojiInRow * kEmojiRowsInFile);
	if (inFileShift >= emojiCount) {
		return QImage();
	}
	const auto maxInFile = emojiCount - inFileShift;
	const auto inFileCount = std::min(maxInFile, kEmojiInRow * kEmojiRowsInFile);
	auto rowsCount = (inFileCount / columnsCount) + ((inFileCount % columnsCount) ? 1 : 0);
	auto emojiImage = QImage(columnsCount * kEmojiSize, rowsCount * kEmojiSize, QImage::Format_ARGB32);
	emojiImage.fill(Qt::transparent);
	auto singleImage = QImage(singleSize, singleSize, QImage::Format_ARGB32);
	{
		QPainter p(&emojiImage);
		p.setRenderHint(QPainter::SmoothPixmapTransform);

		auto column = 0;
		auto row = 0;
		for (auto i = 0; i != inFileCount; ++i) {
			auto &emoji = data_.list[inFileShift + i];
			{
				singleImage.fill(Qt::transparent);

				QPainter q(&singleImage);
				q.setPen(QColor(0, 0, 0, 255));
				q.setFont(font);
				const auto delta = kScaleFromLarge ? kLargeEmojiDelta : kEmojiDelta;
				q.drawText(2, 2 + delta, emoji.id);
			}
			auto sourceRect = computeSourceRect(singleImage);
			if (sourceRect.isEmpty()) {
				return QImage();
			}
			auto targetRect = QRect(column * kEmojiSize, row * kEmojiSize, kEmojiSize, kEmojiSize);
			if (kScaleFromLarge) {
				p.drawImage(targetRect, singleImage.copy(sourceRect).scaled(kEmojiSize, kEmojiSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
			} else {
				p.drawImage(targetRect, singleImage, sourceRect);
			}
			++column;
			if (column == columnsCount) {
				column = 0;
				++row;
			}
		}
	}
	return emojiImage;
}

bool Generator::writeImages() {
	auto imageIndex = 0;
	while (true) {
		auto image = generateImage(imageIndex);
		if (image.isNull()) {
			break;
		}
		auto postfix = '_' + QString::number(imageIndex + 1);
		auto filename = spritePath_ + postfix + ".webp";
		auto bytes = QByteArray();
		{
			QBuffer buffer(&bytes);
			if (!image.save(&buffer, "WEBP", kEmojiQuality)) {
				logDataError() << "Could not save 'emoji" << postfix.toStdString() << ".webp'.";
				return false;
			}
		}
		auto needResave = !QFileInfo(filename).exists();
		if (!needResave) {
			QFile file(filename);
			if (!file.open(QIODevice::ReadOnly)) {
				needResave = true;
			} else {
				auto already = file.readAll();
				if (already.size() != bytes.size() || memcmp(already.constData(), bytes.constData(), already.size())) {
					needResave = true;
				}
			}
		}
        if (needResave) {
			QFile file(filename);
			if (!file.open(QIODevice::WriteOnly)) {
				logDataError() << "Could not open 'emoji" << postfix.toStdString() << ".webp'.";
				return false;
			} else {
				if (file.write(bytes) != bytes.size()) {
					logDataError() << "Could not write 'emoji" << postfix.toStdString() << ".webp'.";
					return false;
				}
			}
		}
		++imageIndex;
	}
	return true;
}
#endif // SUPPORT_IMAGE_GENERATION

bool Generator::writeSource() {
	source_ = std::make_unique<common::CppFile>(outputPath_ + ".cpp", project_);

	source_->include("emoji_suggestions_data.h").include("ui/emoji_config.h").newline();
	source_->pushNamespace("Ui").pushNamespace("Emoji").pushNamespace();
	source_->stream() << "\
\n\
std::vector<One> Items;\n\
\n";
	if (!writeInitCode()) {
		return false;
	}
	if (!writeSections()) {
		return false;
	}
	if (!writeFindReplace()) {
		return false;
	}
	if (!writeFind()) {
		return false;
	}
	source_->popNamespace().newline().pushNamespace("internal");
	source_->stream() << "\
\n\
int FullCount() {\n\
	return Items.size();\n\
}\n\
\n\
EmojiPtr ByIndex(int index) {\n\
	return (index >= 0 && index < Items.size()) ? &Items[index] : nullptr;\n\
}\n\
\n\
EmojiPtr FindReplace(const QChar *start, const QChar *end, int *outLength) {\n\
	auto index = FindReplaceIndex(start, end, outLength);\n\
	return index ? &Items[index - 1] : nullptr;\n\
}\n\
\n\
const std::vector<std::pair<QString, int>> GetReplacementPairs() {\n\
	return ReplacementPairs;\n\
}\n\
\n\
EmojiPtr Find(const QChar *start, const QChar *end, int *outLength) {\n\
	auto index = FindIndex(start, end, outLength);\n\
	return index ? &Items[index - 1] : nullptr;\n\
}\n\
\n\
void Init() {\n\
	auto id = IdData;\n\
	auto takeString = [&id](int size) {\n\
		auto result = QString::fromRawData(reinterpret_cast<const QChar*>(id), size);\n\
		id += size;\n\
		return result;\n\
	};\n\
\n\
	Items.reserve(base::array_size(Data));\n\
	for (auto &data : Data) {\n\
		Items.emplace_back(\n\
			takeString(data.idSize),\n\
			data.original ? &Items[data.original - 1] : nullptr,\n\
			uint32(Items.size()),\n\
			data.postfixed ? true : false,\n\
			data.variated ? true : false,\n\
			One::CreationTag());\n\
	}\n\
	InitReplacements();\n\
}\n\
\n";
	source_->popNamespace();

	if (!writeGetSections()) {
		return false;
	}

	return source_->finalize();
}

bool Generator::writeHeader() {
	auto header = std::make_unique<common::CppFile>(outputPath_ + ".h", project_);
	header->pushNamespace("Ui").pushNamespace("Emoji").pushNamespace("internal");
	header->stream() << "\
\n\
void Init();\n\
\n\
int FullCount();\n\
EmojiPtr ByIndex(int index);\n\
\n\
EmojiPtr Find(const QChar *ch, const QChar *end, int *outLength = nullptr);\n\
\n\
inline bool IsReplaceEdge(const QChar *ch) {\n\
	return true;\n\
\n\
//	switch (ch->unicode()) {\n\
//	case '.': case ',': case ':': case ';': case '!': case '?': case '#': case '@':\n\
//	case '(': case ')': case '[': case ']': case '{': case '}': case '<': case '>':\n\
//	case '+': case '=': case '-': case '_': case '*': case '/': case '\\\\': case '^': case '$':\n\
//	case '\"': case '\\'':\n\
//	case 8212: case 171: case 187: // --, <<, >>\n\
//		return true;\n\
//	}\n\
//	return false;\n\
}\n\
\n\
const std::vector<std::pair<QString, int>> GetReplacementPairs();\n\
EmojiPtr FindReplace(const QChar *ch, const QChar *end, int *outLength = nullptr);\n\
\n";
	header->popNamespace().stream() << "\
\n\
constexpr auto kPostfix = static_cast<ushort>(0xFE0F);\n\
\n\
enum class Section {\n\
	Recent,\n\
	People,\n\
	Nature,\n\
	Food,\n\
	Activity,\n\
	Travel,\n\
	Objects,\n\
	Symbols,\n\
};\n\
\n\
int GetSectionCount(Section section);\n\
EmojiPack GetSection(Section section);\n\
\n";
	return header->finalize();
}

template <typename Callback>
bool Generator::enumerateWholeList(Callback callback) {
	auto index = 0;
	auto variated = -1;
	auto coloredCount = 0;
	for (auto &item : data_.list) {
		if (!callback(item.id, item.postfixed, item.variated, item.colored, variated)) {
			return false;
		}
		if (coloredCount > 0 && (item.variated || !item.colored)) {
			if (!colorsCount_) {
				colorsCount_ = coloredCount;
			} else if (colorsCount_ != coloredCount) {
				logDataError() << "different colored emoji count exist.";
				return false;
			}
			coloredCount = 0;
		}
		if (item.variated) {
			variated = index;
		} else if (item.colored) {
			if (variated <= 0) {
				logDataError() << "wrong order of colored items.";
				return false;
			}
			++coloredCount;
		} else if (variated >= 0) {
			variated = -1;
		}
		++index;
	}
	return true;
}

bool Generator::writeInitCode() {
	source_->stream() << "\
struct DataStruct {\n\
	uint32 original : " << kOriginalBits << ";\n\
	uint32 idSize : " << kIdSizeBits << ";\n\
	uint32 postfixed : 1;\n\
	uint32 variated : 1;\n\
};\n\
\n\
const ushort IdData[] = {";
	startBinary();
	if (!enumerateWholeList([this](Id id, bool isPostfixed, bool isVariated, bool isColored, int original) {
		return writeStringBinary(source_.get(), id);
	})) {
		return false;
	}
	if (_binaryFullLength >= std::numeric_limits<ushort>::max()) {
		logDataError() << "Too many IdData elements.";
		return false;
	}
	source_->stream() << " };\n\
\n\
const DataStruct Data[] = {\n";
	if (!enumerateWholeList([this](Id id, bool isPostfixed, bool isVariated, bool isColored, int original) {
		if (original + 1 >= (1 << kOriginalBits)) {
			logDataError() << "Too many entries.";
			return false;
		}
		if (id.size() >= (1 << kIdSizeBits)) {
			logDataError() << "Too large id.";
			return false;
		}
		source_->stream() << "\
	{ uint32(" << (isColored ? (original + 1) : 0) << "), uint32(" << id.size() << "), uint32(" << (isPostfixed ? "1" : "0") << "), uint32(" << (isVariated ? "1" : "0") << ") },\n";
		return true;
	})) {
		return false;
	}

	source_->stream() << "\
};\n";

	return true;
}

bool Generator::writeSections() {
	source_->stream() << "\
const ushort SectionData[] = {";
	startBinary();
	for (auto &category : data_.categories) {
		for (auto index : category) {
			writeIntBinary(source_.get(), index);
		}
	}
	source_->stream() << " };\n\
\n\
EmojiPack FillSection(int offset, int size) {\n\
	auto result = EmojiPack();\n\
	result.reserve(size);\n\
	for (auto index : gsl::make_span(SectionData + offset, size)) {\n\
		result.push_back(&Items[index]);\n\
	}\n\
	return result;\n\
}\n\n";
	return true;
}

bool Generator::writeGetSections() {
	constexpr const char *sectionNames[] = {
		"Section::People",
		"Section::Nature",
		"Section::Food",
		"Section::Activity",
		"Section::Travel",
		"Section::Objects",
		"Section::Symbols",
	};
	source_->stream() << "\
\n\
int GetSectionCount(Section section) {\n\
	switch (section) {\n\
	case Section::Recent: return GetRecent().size();\n";
	auto countIndex = 0;
	for (auto name : sectionNames) {
		if (countIndex >= int(data_.categories.size())) {
			logDataError() << "category " << countIndex << " not found.";
			return false;
		}
		source_->stream() << "\
	case " << name << ": return " << data_.categories[countIndex++].size() << ";\n";
	}
	source_->stream() << "\
	}\n\
	return 0;\n\
}\n\
\n\
EmojiPack GetSection(Section section) {\n\
	switch (section) {\n\
	case Section::Recent: {\n\
		auto result = EmojiPack();\n\
		result.reserve(GetRecent().size());\n\
		for (auto &item : GetRecent()) {\n\
			result.push_back(item.first);\n\
		}\n\
		return result;\n\
	} break;\n";
	auto index = 0;
	auto offset = 0;
	for (auto name : sectionNames) {
		if (index >= int(data_.categories.size())) {
			logDataError() << "category " << index << " not found.";
			return false;
		}
		auto &category = data_.categories[index++];
		source_->stream() << "\
\n\
	case " << name << ": {\n\
		static auto result = FillSection(" << offset << ", " << category.size() << ");\n\
		return result;\n\
	} break;\n";
		offset += category.size();
	}
	source_->stream() << "\
	}\n\
	return EmojiPack();\n\
}\n\
\n";
	return true;
}

bool Generator::writeFindReplace() {
	source_->stream() << "\
\n\
const std::vector<std::pair<QString, int>> ReplacementPairs = {\n";
	for (const auto &[what, index] : data_.replaces) {
		source_->stream() << "\
	{ qsl(\"" << what << "\"), " << index << " },\n";
	}
	source_->stream() << "\
};\n\
\n\
int FindReplaceIndex(const QChar *start, const QChar *end, int *outLength) {\n\
	auto ch = start;\n\
\n";

	if (!writeFindFromDictionary(data_.replaces)) {
		return false;
	}

	source_->stream() << "\
}\n";

	return true;
}

bool Generator::writeFind() {
	source_->stream() << "\
\n\
int FindIndex(const QChar *start, const QChar *end, int *outLength) {\n\
	auto ch = start;\n\
\n";

	if (!writeFindFromDictionary(data_.map, true)) {
		return false;
	}

	source_->stream() << "\
}\n\
\n";

	return true;
}

bool Generator::writeFindFromDictionary(const std::map<QString, int, std::greater<QString>> &dictionary, bool skipPostfixes) {
	auto tabs = [](int size) {
		return QString(size, '\t');
	};

	std::map<int, int> uniqueFirstChars;
	auto foundMax = 0, foundMin = 65535;
	for (auto &item : dictionary) {
		auto ch = item.first[0].unicode();
		if (foundMax < ch) foundMax = ch;
		if (foundMin > ch) foundMin = ch;
		uniqueFirstChars[ch] = 0;
	}

	enum class UsedCheckType {
		Switch,
		If,
	};
	auto checkTypes = QVector<UsedCheckType>();
	auto chars = QString();
	auto tabsUsed = 1;
	auto lengthsCounted = std::map<QString, bool>();

	auto writeSkipPostfix = [this, &tabs, skipPostfixes](int tabsCount) {
		if (skipPostfixes) {
			source_->stream() << tabs(tabsCount) << "if (++ch != end && ch->unicode() == kPostfix) ++ch;\n";
		} else {
			source_->stream() << tabs(tabsCount) << "++ch;\n";
		}
	};

	// Returns true if at least one check was finished.
	auto finishChecksTillKey = [this, &chars, &checkTypes, &tabsUsed, tabs](const QString &key) {
		auto result = false;
		while (!chars.isEmpty() && key.midRef(0, chars.size()) != chars) {
			result = true;

			auto wasType = checkTypes.back();
			chars.resize(chars.size() - 1);
			checkTypes.pop_back();
			if (wasType == UsedCheckType::Switch || wasType == UsedCheckType::If) {
				--tabsUsed;
				if (wasType == UsedCheckType::Switch) {
					source_->stream() << tabs(tabsUsed) << "break;\n";
				}
				if ((!chars.isEmpty() && key.midRef(0, chars.size()) != chars) || key == chars) {
					source_->stream() << tabs(tabsUsed) << "}\n";
				}
			}
		}
		return result;
	};

	// Check if we can use "if" for a check on "charIndex" in "it" (otherwise only "switch")
	auto canUseIfForCheck = [](auto it, auto end, int charIndex) {
		auto key = it->first;
		auto i = it;
		auto keyStart = key.mid(0, charIndex);
		for (++i; i != end; ++i) {
			auto nextKey = i->first;
			if (nextKey.mid(0, charIndex) != keyStart) {
				return true;
			} else if (nextKey.size() > charIndex && nextKey[charIndex] != key[charIndex]) {
				return false;
			}
		}
		return true;
	};

	for (auto i = dictionary.cbegin(), e = dictionary.cend(); i != e; ++i) {
		auto &item = *i;
		auto key = item.first;
		auto weContinueOldSwitch = finishChecksTillKey(key);
		while (chars.size() != key.size()) {
			auto checking = chars.size();
			auto partialKey = key.mid(0, checking);
			if (dictionary.find(partialKey) != dictionary.cend()) {
				if (lengthsCounted.find(partialKey) == lengthsCounted.cend()) {
					lengthsCounted.insert(std::make_pair(partialKey, true));
					source_->stream() << tabs(tabsUsed) << "if (outLength) *outLength = (ch - start);\n";
				}
			}

			auto keyChar = key[checking];
			auto keyCharString = "0x" + QString::number(keyChar.unicode(), 16);
			auto usedIfForCheck = !weContinueOldSwitch && canUseIfForCheck(i, e, checking);
			if (weContinueOldSwitch) {
				weContinueOldSwitch = false;
			} else if (!usedIfForCheck) {
				source_->stream() << tabs(tabsUsed) << "if (ch != end) switch (ch->unicode()) {\n";
			}
			if (usedIfForCheck) {
				source_->stream() << tabs(tabsUsed) << "if (ch != end && ch->unicode() == " << keyCharString << ") {\n";
				checkTypes.push_back(UsedCheckType::If);
			} else {
				source_->stream() << tabs(tabsUsed) << "case " << keyCharString << ":\n";
				checkTypes.push_back(UsedCheckType::Switch);
			}
			writeSkipPostfix(++tabsUsed);
			chars.push_back(keyChar);
		}
		if (lengthsCounted.find(key) == lengthsCounted.cend()) {
			lengthsCounted.insert(std::make_pair(key, true));
			source_->stream() << tabs(tabsUsed) << "if (outLength) *outLength = (ch - start);\n";
		}

		// While IsReplaceEdge() currently is always true we just return the value.
		//source_->stream() << tabs(1 + chars.size()) << "if (ch + " << chars.size() << " == end || IsReplaceEdge(*(ch + " << chars.size() << ")) || (ch + " << chars.size() << ")->unicode() == ' ') {\n";
		//source_->stream() << tabs(1 + chars.size()) << "\treturn &Items[" << item.second << "];\n";
		//source_->stream() << tabs(1 + chars.size()) << "}\n";
		source_->stream() << tabs(tabsUsed) << "return " << (item.second + 1) << ";\n";
	}
	finishChecksTillKey(QString());

	source_->stream() << "\
\n\
	return 0;\n";
	return true;
}

bool Generator::writeSuggestionsSource() {
	suggestionsSource_ = std::make_unique<common::CppFile>(suggestionsPath_ + ".cpp", project_);
	suggestionsSource_->stream() << "\
#include <map>\n\
\n";
	suggestionsSource_->pushNamespace("Ui").pushNamespace("Emoji").pushNamespace("internal").pushNamespace();
	suggestionsSource_->stream() << "\
\n";
	if (!writeReplacements()) {
		return false;
	}
	suggestionsSource_->popNamespace().newline();
	if (!writeGetReplacements()) {
		return false;
	}

	return suggestionsSource_->finalize();
}

bool Generator::writeSuggestionsHeader() {
	auto maxLength = 0;
	for (auto &replace : replaces_.list) {
		if (maxLength < replace.replacement.size()) {
			maxLength = replace.replacement.size();
		}
	}
	auto header = std::make_unique<common::CppFile>(suggestionsPath_ + ".h", project_);
	header->include("emoji_suggestions.h").newline();
	header->pushNamespace("Ui").pushNamespace("Emoji").pushNamespace("internal");
	header->stream() << "\
\n\
struct Replacement {\n\
	utf16string emoji;\n\
	utf16string replacement;\n\
	std::vector<utf16string> words;\n\
};\n\
\n\
constexpr auto kReplacementMaxLength = " << maxLength << ";\n\
\n\
void InitReplacements();\n\
const std::vector<Replacement> &GetAllReplacements();\n\
const std::vector<const Replacement*> *GetReplacements(utf16char first);\n\
utf16string GetReplacementEmoji(utf16string replacement);\n\
\n";
	return header->finalize();
}

bool Generator::writeReplacements() {
	QMap<QChar, QVector<int>> byCharIndices;
	suggestionsSource_->stream() << "\
struct ReplacementStruct {\n\
	small emojiSize;\n\
	small replacementSize;\n\
	small wordsCount;\n\
};\n\
\n\
const utf16char ReplacementData[] = {";
	startBinary();
	for (auto i = 0, size = replaces_.list.size(); i != size; ++i) {
		auto &replace = replaces_.list[i];
		if (!writeStringBinary(suggestionsSource_.get(), replace.id)) {
			return false;
		}
		if (!writeStringBinary(suggestionsSource_.get(), replace.replacement)) {
			return false;
		}
		for (auto &word : replace.words) {
			if (!writeStringBinary(suggestionsSource_.get(), word)) {
				return false;
			}
			auto &index = byCharIndices[word[0]];
			if (index.isEmpty() || index.back() != i) {
				index.push_back(i);
			}
		}
	}
	suggestionsSource_->stream() << " };\n\
\n\
const small ReplacementWordLengths[] = {";
	startBinary();
	for (auto &replace : replaces_.list) {
		auto wordLengths = QStringList();
		for (auto &word : replace.words) {
			writeIntBinary(suggestionsSource_.get(), word.size());
		}
	}
	suggestionsSource_->stream() << " };\n\
\n\
const ReplacementStruct ReplacementInitData[] = {\n";
	for (auto &replace : replaces_.list) {
		suggestionsSource_->stream() << "\
	{ small(" << replace.id.size() << "), small(" << replace.replacement.size() << "), small(" << replace.words.size() << ") },\n";
	}
	suggestionsSource_->stream() << "};\n\
\n\
const medium ReplacementIndices[] = {";
	startBinary();
	for (auto &byCharIndex : byCharIndices) {
		for (auto index : byCharIndex) {
			writeIntBinary(suggestionsSource_.get(), index);
		}
	}
	suggestionsSource_->stream() << " };\n\
\n\
struct ReplacementIndexStruct {\n\
	utf16char ch;\n\
	medium count;\n\
};\n\
\n\
const internal::checksum ReplacementChecksums[] = {\n";
	startBinary();
	for (auto &replace : replaces_.list) {
		writeUintBinary(suggestionsSource_.get(), countCrc32(replace.replacement.constData(), replace.replacement.size() * sizeof(QChar)));
	}
	suggestionsSource_->stream() << " };\n\
\n\
const ReplacementIndexStruct ReplacementIndexData[] = {\n";
	startBinary();
	for (auto i = byCharIndices.cbegin(), e = byCharIndices.cend(); i != e; ++i) {
		suggestionsSource_->stream() << "\
	{ utf16char(" << i.key().unicode() << "), medium(" << i.value().size() << ") },\n";
	}
	suggestionsSource_->stream() << "};\n\
\n\
std::vector<Replacement> Replacements;\n\
std::map<utf16char, std::vector<const Replacement*>> ReplacementsMap;\n\
std::map<internal::checksum, const Replacement*> ReplacementsHash;\n\
\n";
	return true;
}

bool Generator::writeGetReplacements() {
	suggestionsSource_->stream() << "\
void InitReplacements() {\n\
	if (!Replacements.empty()) {\n\
		return;\n\
	}\n\
	auto data = ReplacementData;\n\
	auto takeString = [&data](int size) {\n\
		auto result = utf16string(data, size);\n\
		data += size;\n\
		return result;\n\
	};\n\
	auto wordSize = ReplacementWordLengths;\n\
\n\
	Replacements.reserve(" << replaces_.list.size() << ");\n\
	for (auto item : ReplacementInitData) {\n\
		auto emoji = takeString(item.emojiSize);\n\
		auto replacement = takeString(item.replacementSize);\n\
		auto words = std::vector<utf16string>();\n\
		words.reserve(item.wordsCount);\n\
		for (auto i = 0; i != item.wordsCount; ++i) {\n\
			words.push_back(takeString(*wordSize++));\n\
		}\n\
		Replacements.push_back({ std::move(emoji), std::move(replacement), std::move(words) });\n\
	}\n\
\n\
	auto indices = ReplacementIndices;\n\
	auto items = &Replacements[0];\n\
	for (auto item : ReplacementIndexData) {\n\
		auto index = std::vector<const Replacement*>();\n\
		index.reserve(item.count);\n\
		for (auto i = 0; i != item.count; ++i) {\n\
			index.push_back(items + (*indices++));\n\
		}\n\
		ReplacementsMap.emplace(item.ch, std::move(index));\n\
	}\n\
\n\
	for (auto checksum : ReplacementChecksums) {\n\
		ReplacementsHash.emplace(checksum, items++);\n\
	}\n\
}\n\
\n\
const std::vector<const Replacement*> *GetReplacements(utf16char first) {\n\
	if (ReplacementsMap.empty()) {\n\
		InitReplacements();\n\
	}\n\
	auto it = ReplacementsMap.find(first);\n\
	return (it == ReplacementsMap.cend()) ? nullptr : &it->second;\n\
}\n\
\n\
const std::vector<Replacement> &GetAllReplacements() {\n\
	return Replacements;\n\
}\n\
\n\
utf16string GetReplacementEmoji(utf16string replacement) {\n\
	auto code = internal::countChecksum(replacement.data(), replacement.size() * sizeof(utf16char));\n\
	auto it = ReplacementsHash.find(code);\n\
	return (it == ReplacementsHash.cend()) ? utf16string() : it->second->emoji;\n\
}\n\
\n";
	return true;
}

void Generator::startBinary() {
	_binaryFullLength = _binaryCount = 0;
}

bool Generator::writeStringBinary(common::CppFile *source, const QString &string) {
	if (string.size() >= 256) {
		logDataError() << "Too long string: " << string.toStdString();
		return false;
	}
	for (auto ch : string) {
		if (_binaryFullLength > 0) source->stream() << ",";
		if (!_binaryCount++) {
			source->stream() << "\n";
		} else {
			if (_binaryCount == 12) {
				_binaryCount = 0;
			}
			source->stream() << " ";
		}
		source->stream() << "0x" << QString::number(ch.unicode(), 16);
		++_binaryFullLength;
	}
	return true;
}

void Generator::writeIntBinary(common::CppFile *source, int data) {
	if (_binaryFullLength > 0) source->stream() << ",";
	if (!_binaryCount++) {
		source->stream() << "\n";
	} else {
		if (_binaryCount == 12) {
			_binaryCount = 0;
		}
		source->stream() << " ";
	}
	source->stream() << data;
	++_binaryFullLength;
}

void Generator::writeUintBinary(common::CppFile *source, uint32 data) {
	if (_binaryFullLength > 0) source->stream() << ",";
	if (!_binaryCount++) {
		source->stream() << "\n";
	} else {
		if (_binaryCount == 12) {
			_binaryCount = 0;
		}
		source->stream() << " ";
	}
	source->stream() << "0x" << QString::number(data, 16).toUpper() << "U";
	++_binaryFullLength;
}

} // namespace emoji
} // namespace codegen
