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

	// List of files we need to generate with other instance of Generator.
	// It is not empty only if rebuild_ flag is true.
	QStringList dependenciesToGenerate_;

};

} // namespace style
} // namespace codegen
