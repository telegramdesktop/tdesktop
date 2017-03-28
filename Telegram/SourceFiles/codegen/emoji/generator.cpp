/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "codegen/emoji/generator.h"

#include <QtCore/QtPlugin>
#include <QtCore/QBuffer>
#include <QtGui/QFontDatabase>
#include <QtGui/QGuiApplication>
#include <QtGui/QImage>
#include <QtGui/QPainter>
#include <QtCore/QDir>

Q_IMPORT_PLUGIN(QWebpPlugin)
#ifdef Q_OS_MAC
Q_IMPORT_PLUGIN(QCocoaIntegrationPlugin)
#elif defined Q_OS_WIN
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)
#else // !Q_OS_MAC && !Q_OS_WIN
Q_IMPORT_PLUGIN(QXcbIntegrationPlugin)
#endif // !Q_OS_MAC && !Q_OS_WIN

namespace codegen {
namespace emoji {
namespace {

constexpr int kErrorCantWritePath = 851;

common::ProjectInfo Project = {
	"codegen_emoji",
	"empty",
	true, // forceReGenerate
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

QString computeId(Id id) {
	auto idAsParams = QStringList();
	for (auto i = 0, size = id.size(); i != size; ++i) {
		idAsParams.push_back("0x" + QString::number(id[i].unicode(), 16));
	}
	return "internal::ComputeId(" + idAsParams.join(", ") + ")";
}

} // namespace

Generator::Generator(const Options &options) : project_(Project), data_(PrepareData()) {
	QDir dir(options.outputPath);
	if (!dir.mkpath(".")) {
		common::logError(kErrorCantWritePath, "Command Line") << "can not open path for writing: " << dir.absolutePath().toStdString();
		data_ = Data();
	}

	outputPath_ = dir.absolutePath() + "/emoji_config";
	spritePath_ = dir.absolutePath() + "/emoji";
}

int Generator::generate() {
	if (data_.list.empty()) {
		return -1;
	}

#ifdef Q_OS_MAC
	if (!writeImages()) {
		return -1;
	}
#endif // Q_OS_MAC

	if (!writeSource()) {
		return -1;
	}

	return 0;
}

constexpr auto kVariantsCount = 5;
constexpr auto kEmojiInRow = 40;

QImage Generator::generateImage(int variantIndex) {
	constexpr int kEmojiSizes[kVariantsCount + 1] = { 18, 22, 27, 36, 45, 180 };
	constexpr bool kBadSizes[kVariantsCount] = { true, true, false, false, false };
	constexpr int kEmojiFontSizes[kVariantsCount + 1] = { 14, 20, 27, 36, 45, 180 };
	constexpr int kEmojiDeltas[kVariantsCount + 1] = { 15, 20, 25, 34, 42, 167 };

	auto emojiCount = data_.list.size();
	auto columnsCount = kEmojiInRow;
	auto rowsCount = (emojiCount / columnsCount) + ((emojiCount % columnsCount) ? 1 : 0);

	auto emojiSize = kEmojiSizes[variantIndex];
	auto isBad = kBadSizes[variantIndex];
	auto sourceSize = (isBad ? kEmojiSizes[kVariantsCount] : emojiSize);

	auto font = QGuiApplication::font();
	font.setFamily(QStringLiteral("Apple Color Emoji"));
	font.setPixelSize(kEmojiFontSizes[isBad ? kVariantsCount : variantIndex]);

	auto singleSize = 4 + sourceSize;
	auto emojiImage = QImage(columnsCount * emojiSize, rowsCount * emojiSize, QImage::Format_ARGB32);
	emojiImage.fill(Qt::transparent);
	auto singleImage = QImage(singleSize, singleSize, QImage::Format_ARGB32);
	{
		QPainter p(&emojiImage);
		p.setRenderHint(QPainter::SmoothPixmapTransform);

		auto column = 0;
		auto row = 0;
		for (auto &emoji : data_.list) {
			{
				singleImage.fill(Qt::transparent);

				QPainter q(&singleImage);
				q.setPen(QColor(0, 0, 0, 255));
				q.setFont(font);
				q.drawText(2, 2 + kEmojiDeltas[isBad ? kVariantsCount : variantIndex], emoji.id);
			}
			auto sourceRect = computeSourceRect(singleImage);
			if (sourceRect.isEmpty()) {
				return QImage();
			}
			auto targetRect = QRect(column * emojiSize, row * emojiSize, emojiSize, emojiSize);
			if (isBad) {
				p.drawImage(targetRect, singleImage.copy(sourceRect).scaled(emojiSize, emojiSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
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
	constexpr const char *variantPostfix[] = { "", "_125x", "_150x", "_200x", "_250x" };
	for (auto variantIndex = 0; variantIndex != kVariantsCount; variantIndex++) {
		auto image = generateImage(variantIndex);
		auto postfix = variantPostfix[variantIndex];
		auto filename = spritePath_ + postfix + ".webp";
		auto bytes = QByteArray();
		{
			QBuffer buffer(&bytes);
			if (!image.save(&buffer, "WEBP", (variantIndex < 3) ? 100 : 99)) {
				logDataError() << "Could not save 'emoji" << postfix << ".webp'.";
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
				logDataError() << "Could not open 'emoji" << postfix << ".png'.";
				return false;
			} else {
				if (file.write(bytes) != bytes.size()) {
					logDataError() << "Could not write 'emoji" << postfix << ".png'.";
					return false;
				}
			}
		}
	}
	return true;
}

bool Generator::writeSource() {
	source_ = std::make_unique<common::CppFile>(outputPath_ + ".cpp", project_);

	source_->pushNamespace("Ui").pushNamespace("Emoji").pushNamespace();
	source_->stream() << "\
\n\
constexpr auto kCount = " << data_.list.size() << ";\n\
auto WorkingIndex = -1;\n\
\n\
std::vector<One> Items;\n\
\n";
	source_->popNamespace().newline().pushNamespace("internal");
	source_->stream() << "\
\n\
EmojiPtr ByIndex(int index) {\n\
	return (index >= 0 && index < Items.size()) ? &Items[index] : nullptr;\n\
}\n\
\n\
template <typename ...Args>\n\
inline QString ComputeId(Args... args) {\n\
	auto utf16 = { args... };\n\
	auto result = QString();\n\
	result.reserve(utf16.size());\n\
	for (auto ch : utf16) {\n\
		result.append(QChar(ch));\n\
	}\n\
	return result;\n\
}\n";
	if (!writeFindReplace()) {
		return false;
	}
	if (!writeFind()) {
		return false;
	}
	source_->popNamespace();

	if (!writeInitCode()) {
		return false;
	}
	if (!writePacks()) {
		return false;
	}
	source_->stream() << "\
\n\
int Index() {\n\
	return WorkingIndex;\n\
}\n\
\n\
int One::variantsCount() const {\n\
	return hasVariants() ? " << colorsCount_ << " : 0;\n\
}\n\
\n\
int One::variantIndex(EmojiPtr variant) const {\n\
	return (variant - original());\n\
}\n\
\n\
EmojiPtr One::variant(int index) const {\n\
	return (index >= 0 && index <= variantsCount()) ? (original() + index) : this;\n\
}\n\
\n\
int One::index() const {\n\
	return (this - &Items[0]);\n\
}\n\
\n";

	return source_->finalize();
}

bool Generator::writeInitCode() {
	constexpr const char *variantNames[] = {
		"dbisOne",
		"dbisOneAndQuarter",
		"dbisOneAndHalf",
		"dbisTwo"
	};

	source_->stream() << "\
\n\
void Init() {\n\
	auto tag = One::CreationTag();\n\
	auto scaleForEmoji = cRetina() ? dbisTwo : cScale();\n\
\n\
	switch (scaleForEmoji) {\n";
	auto variantIndex = 0;
	for (auto name : variantNames) {
		source_->stream() << "\
	case " << name << ": WorkingIndex = " << variantIndex++ << "; break;\n";
	}
	source_->stream() << "\
	};\n\
\n\
	Items.reserve(kCount);\n\
\n";

	auto column = 0;
	auto row = 0;
	auto index = 0;
	auto variated = -1;
	auto coloredCount = 0;
	for (auto &item : data_.list) {
		source_->stream() << "\
	Items.emplace_back(" << computeId(item.id) << ", " << column << ", " << row << ", " << (item.postfixed ? "true" : "false") << ", " << (item.variated ? "true" : "false") << ", " << (item.colored ? "&Items[" + QString::number(variated) + "]" : "nullptr") << ", tag);\n";
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
		if (++column == kEmojiInRow) {
			column = 0;
			++row;
		}
		++index;
	}

	source_->stream() << "\
}\n";
	return true;
}

bool Generator::writePacks() {
	constexpr const char *packNames[] = {
		"dbiesPeople",
		"dbiesNature",
		"dbiesFood",
		"dbiesActivity",
		"dbiesTravel",
		"dbiesObjects",
		"dbiesSymbols",
	};
	source_->stream() << "\
\n\
int GetPackCount(DBIEmojiSection section) {\n\
	switch (section) {\n";
	auto countIndex = 0;
	for (auto name : packNames) {
		if (countIndex >= int(data_.categories.size())) {
			logDataError() << "category " << countIndex << " not found.";
			return false;
		}
		source_->stream() << "\
	case " << name << ": return " << data_.categories[countIndex++].size() << ";\n";
	}
	source_->stream() << "\
	case dbiesRecent: return GetRecent().size();\n\
	}\n\
	return 0;\n\
}\n\
\n\
EmojiPack GetPack(DBIEmojiSection section) {\n\
	switch (section) {\n";
	auto index = 0;
	for (auto name : packNames) {
		if (index >= int(data_.categories.size())) {
			logDataError() << "category " << index << " not found.";
			return false;
		}
		auto &category = data_.categories[index++];
		source_->stream() << "\
	case " << name << ": {\n\
		static auto result = EmojiPack();\n\
		if (result.isEmpty()) {\n\
			result.reserve(" << category.size() << ");\n";
		for (auto index : category) {
			source_->stream() << "\
			result.push_back(&Items[" << index << "]);\n";
		}
		source_->stream() << "\
		}\n\
		return result;\n\
	} break;\n\n";
	}
	source_->stream() << "\
	case dbiesRecent: {\n\
		auto result = EmojiPack();\n\
		result.reserve(GetRecent().size());\n\
		for (auto &item : GetRecent()) {\n\
			result.push_back(item.first);\n\
		}\n\
		return result;\n\
	} break;\n\
	}\n\
	return EmojiPack();\n\
}\n";
	return true;
}

bool Generator::writeFindReplace() {
	source_->stream() << "\
\n\
EmojiPtr FindReplace(const QChar *start, const QChar *end, int *outLength) {\n\
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
EmojiPtr Find(const QChar *start, const QChar *end, int *outLength) {\n\
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
		source_->stream() << tabs(tabsUsed) << "return &Items[" << item.second << "];\n";
	}
	finishChecksTillKey(QString());

	source_->stream() << "\
\n\
	return nullptr;\n";
	return true;
}

} // namespace emoji
} // namespace codegen
