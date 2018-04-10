/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <memory>
#include <QtCore/QString>
#include "codegen/numbers/options.h"

namespace codegen {
namespace numbers {
class ParsedFile;
struct Rules;

// Walks through a file, parses it and generates number formatter.
class Processor {
public:
	explicit Processor(const Options &options);
	Processor(const Processor &other) = delete;
	Processor &operator=(const Processor &other) = delete;

	// Returns 0 on success.
	int launch();

	~Processor();

private:
	bool write(const Rules &rules) const;

	std::unique_ptr<ParsedFile> parser_;
	const Options &options_;

};

} // namespace numbers
} // namespace codegen
