/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <memory>
#include <QtCore/QString>
#include "codegen/lang/options.h"

namespace codegen {
namespace lang {
class ParsedFile;
struct LangPack;

// Walks through a file, parses it and generates the output.
class Processor {
public:
	explicit Processor(const Options &options);
	Processor(const Processor &other) = delete;
	Processor &operator=(const Processor &other) = delete;

	// Returns 0 on success.
	int launch();

	~Processor();

private:
	bool write(const LangPack &langpack) const;

	std::unique_ptr<ParsedFile> parser_;
	const Options &options_;

};

} // namespace lang
} // namespace codegen
