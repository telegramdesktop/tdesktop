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
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#include "codegen/style/generator.h"

#include <QtGui/QImage>
#include "codegen/style/tokenized_file.h"

using Token = codegen::style::TokenizedFile::Token;
using Type = Token::Type;

namespace codegen {
namespace style {
namespace {

} // namespace

Generator::Generator(const QString &filepath, bool rebuildDependencies)
: file_(std::make_unique<TokenizedFile>(filepath))
, rebuild_(rebuildDependencies) {

}

int Generator::process() {
	if (!file_->read()) {
		return -1;
	}

	while (true) {
		auto token = file_->getToken();
		if (token.type == Type::Using) {
			continue;
		}
		if (file_->atEnd() && !file_->failed()) {
			break;
		}
		return -1;
	}
	return 0;
}

Generator::~Generator() = default;

} // namespace style
} // namespace codegen
