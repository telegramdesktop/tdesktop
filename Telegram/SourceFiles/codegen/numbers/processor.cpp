/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "codegen/numbers/processor.h"

#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include "codegen/common/cpp_file.h"
#include "codegen/numbers/parsed_file.h"
#include "codegen/numbers/generator.h"

namespace codegen {
namespace numbers {
namespace {

constexpr int kErrorCantWritePath = 851;

} // namespace

Processor::Processor(const Options &options)
: parser_(std::make_unique<ParsedFile>(options))
, options_(options) {
}

int Processor::launch() {
	if (!parser_->read()) {
		return -1;
	}

	auto result = parser_->getResult();
	if (!write(result)) {
		return -1;
	}

	return 0;
}

bool Processor::write(const Rules &rules) const {
	QDir dir(options_.outputPath);
	if (!dir.mkpath(".")) {
		common::logError(kErrorCantWritePath, "Command Line") << "can not open path for writing: " << dir.absolutePath().toStdString();
		return false;
	}

	QFileInfo srcFile(options_.inputPath);
	QString dstFilePath = dir.absolutePath() + "/numbers";

	common::ProjectInfo project = {
		"codegen_style",
		srcFile.fileName(),
		false, // forceReGenerate
	};

	Generator generator(rules, dstFilePath, project);
	if (!generator.writeHeader()) {
		return false;
	}
	if (!generator.writeSource()) {
		return false;
	}

	return true;
}

Processor::~Processor() = default;

} // namespace numbers
} // namespace codegen
