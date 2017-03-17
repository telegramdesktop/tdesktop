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
#include "codegen/style/processor.h"

#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include "codegen/common/cpp_file.h"
#include "codegen/style/parsed_file.h"
#include "codegen/style/generator.h"

namespace codegen {
namespace style {
namespace {

constexpr int kErrorCantWritePath = 821;

QString destFileBaseName(const structure::Module &module) {
	return "style_" + QFileInfo(module.filepath()).baseName();
}

} // namespace

Processor::Processor(const Options &options)
: parser_(std::make_unique<ParsedFile>(options))
, options_(options) {
}

int Processor::launch() {
	if (!parser_->read()) {
		return -1;
	}

	auto module = parser_->getResult();
	if (!write(*module)) {
		return -1;
	}

	return 0;
}

bool Processor::write(const structure::Module &module) const {
	bool forceReGenerate = false;
	QDir dir(options_.outputPath);
	if (!dir.mkpath(".")) {
		common::logError(kErrorCantWritePath, "Command Line") << "can not open path for writing: " << dir.absolutePath().toStdString();
		return false;
	}

	QFileInfo srcFile(module.filepath());
	QString dstFilePath = dir.absolutePath() + '/' + (options_.isPalette ? "palette" : destFileBaseName(module));

	common::ProjectInfo project = {
		"codegen_style",
		srcFile.fileName(),
		forceReGenerate
	};

	Generator generator(module, dstFilePath, project, options_.isPalette);
	if (!generator.writeHeader()) {
		return false;
	}
	if (!generator.writeSource()) {
		return false;
	}
	return true;
}

Processor::~Processor() = default;

} // namespace style
} // namespace codegen
