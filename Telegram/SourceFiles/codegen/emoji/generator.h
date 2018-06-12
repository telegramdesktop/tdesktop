/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <memory>
#include <QtCore/QString>
#include <QtCore/QSet>
#include "codegen/common/cpp_file.h"
#include "codegen/emoji/options.h"
#include "codegen/emoji/data.h"
#include "codegen/emoji/replaces.h"

namespace codegen {
namespace emoji {

using uint32 = unsigned int;

class Generator {
public:
	Generator(const Options &options);
	Generator(const Generator &other) = delete;
	Generator &operator=(const Generator &other) = delete;

	int generate();

private:
#ifdef SUPPORT_IMAGE_GENERATION
	QImage generateImage(int variantIndex);
	bool writeImages();
#endif // SUPPORT_IMAGE_GENERATION

	bool writeSource();
	bool writeHeader();
	bool writeSuggestionsSource();
	bool writeSuggestionsHeader();

	template <typename Callback>
	bool enumerateWholeList(Callback callback);

	bool writeInitCode();
	bool writeSections();
	bool writeReplacements();
	bool writeGetSections();
	bool writeFindReplace();
	bool writeFind();
	bool writeFindFromDictionary(const std::map<QString, int, std::greater<QString>> &dictionary, bool skipPostfixes = false);
	bool writeGetReplacements();
	void startBinary();
	bool writeStringBinary(common::CppFile *source, const QString &string);
	void writeIntBinary(common::CppFile *source, int data);
	void writeUintBinary(common::CppFile *source, uint32 data);

	const common::ProjectInfo &project_;
	int colorsCount_ = 0;
#ifdef SUPPORT_IMAGE_GENERATION
	bool writeImages_ = false;
#endif // SUPPORT_IMAGE_GENERATION
	QString outputPath_;
	QString spritePath_;
	std::unique_ptr<common::CppFile> source_;
	Data data_;

	QString suggestionsPath_;
	std::unique_ptr<common::CppFile> suggestionsSource_;
	Replaces replaces_;

	int _binaryFullLength = 0;
	int _binaryCount = 0;

};

} // namespace emoji
} // namespace codegen
