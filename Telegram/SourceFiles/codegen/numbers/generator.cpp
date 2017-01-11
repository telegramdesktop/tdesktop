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
#include "codegen/numbers/generator.h"

#include <QtCore/QDir>
#include <QtCore/QSet>
#include <functional>

namespace codegen {
namespace numbers {
namespace {

} // namespace

Generator::Generator(const Rules &rules, const QString &destBasePath, const common::ProjectInfo &project)
: rules_(rules)
, basePath_(destBasePath)
, project_(project) {
}

bool Generator::writeHeader() {
	header_ = std::make_unique<common::CppFile>(basePath_ + ".h", project_);

	header_->stream() << "QVector<int> phoneNumberParse(const QString &number);\n";

	return header_->finalize();
}

bool Generator::writeSource() {
	source_ = std::make_unique<common::CppFile>(basePath_ + ".cpp", project_);

	source_->stream() << "\
QVector<int> phoneNumberParse(const QString &number) {\n\
	QVector<int> result;\n\
\n\
	int32 len = number.size();\n\
	if (len > 0) switch (number.at(0).unicode()) {\n";

	QString already;
	for (auto i = rules_.data.cend(), e = rules_.data.cbegin(); i != e;) {
		--i;
		QString k = i.key();
		bool onlyLastChanged = true;
		while (!already.isEmpty() && (already.size() > k.size() || !already.endsWith(k.at(already.size() - 1)))) {
			if (!onlyLastChanged) {
				source_->stream() << QString("\t").repeated(1 + already.size()) << "}\n";
				source_->stream() << QString("\t").repeated(already.size()) << "break;\n";
			}
			already = already.mid(0, already.size() - 1);
			onlyLastChanged = false;
		}
		if (already == k) {
			source_->stream() << QString("\t").repeated(1 + already.size()) << "}\n";
		} else {
			bool onlyFirstCheck = true;
			while (already.size() < k.size()) {
				if (!onlyFirstCheck) source_->stream() << QString("\t").repeated(1 + already.size()) << "if (len > " << already.size() << ") switch (number.at(" << already.size() << ").unicode()) {\n";
				source_->stream() << QString("\t").repeated(1 + already.size()) << "case '" << k.at(already.size()).toLatin1() << "':\n";
				already.push_back(k.at(already.size()));
				onlyFirstCheck = false;
			}
		}
		if (i.value().isEmpty()) {
			source_->stream() << QString("\t").repeated(1 + already.size()) << "return QVector<int>(1, " << k.size() << ");\n";
		} else {
			source_->stream() << QString("\t").repeated(1 + already.size()) << "result.reserve(" << (i.value().size() + 1) << ");\n";
			source_->stream() << QString("\t").repeated(1 + already.size()) << "result.push_back(" << k.size() << ");\n";
			for (int j = 0, l = i.value().size(); j < l; ++j) {
				source_->stream() << QString("\t").repeated(1 + already.size()) << "result.push_back(" << i.value().at(j) << ");\n";
			}
			source_->stream() << QString("\t").repeated(1 + already.size()) << "return result;\n";
		}
	}
	bool onlyLastChanged = true;
	while (!already.isEmpty()) {
		if (!onlyLastChanged) {
			source_->stream() << QString("\t").repeated(1 + already.size()) << "}\n";
		}
		already = already.mid(0, already.size() - 1);
		onlyLastChanged = false;
	}
	source_->stream() << "\
	}\n\
\n\
	return result;\n\
}\n";

	return source_->finalize();
}

} // namespace numbers
} // namespace codegen
