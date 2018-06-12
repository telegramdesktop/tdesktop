/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <memory>
#include <map>
#include <set>
#include <functional>
#include <QtCore/QString>
#include <QtCore/QSet>
#include "codegen/common/cpp_file.h"
#include "codegen/lang/parsed_file.h"

namespace codegen {
namespace lang {

class Generator {
public:
	Generator(const LangPack &langpack, const QString &destBasePath, const common::ProjectInfo &project);
	Generator(const Generator &other) = delete;
	Generator &operator=(const Generator &other) = delete;

	bool writeHeader();
	bool writeSource();

private:
	QString getFullKey(const LangPack::Entry &entry);

	template <typename ComputeResult>
	void writeSetSearch(const std::set<QString, std::greater<QString>> &set, ComputeResult computeResult, const QString &invalidResult);

	const LangPack &langpack_;
	QString basePath_, baseName_;
	const common::ProjectInfo &project_;
	std::unique_ptr<common::CppFile> source_, header_;

};

} // namespace lang
} // namespace codegen
