/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <memory>
#include <QtCore/QString>
#include "codegen/style/options.h"

namespace codegen {
namespace style {
namespace structure {
class Module;
} // namespace structure
class ParsedFile;

// Walks through a file, parses it and parses dependency files if necessary.
// Uses Generator class to produce the final output.
class Processor {
public:
	explicit Processor(const Options &options);
	Processor(const Processor &other) = delete;
	Processor &operator=(const Processor &other) = delete;

	// Returns 0 on success.
	int launch();

	~Processor();

private:
	bool write(const structure::Module &module) const;

	std::unique_ptr<ParsedFile> parser_;
	const Options &options_;

};

} // namespace style
} // namespace codegen
