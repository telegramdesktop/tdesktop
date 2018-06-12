/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <memory>
#include <map>
#include <functional>
#include <QtCore/QString>
#include <QtCore/QSet>
#include "codegen/common/cpp_file.h"
#include "codegen/style/structure_types.h"

namespace codegen {
namespace style {
namespace structure {
class Module;
} // namespace structure

class Generator {
public:
	Generator(const structure::Module &module, const QString &destBasePath, const common::ProjectInfo &project, bool isPalette);
	Generator(const Generator &other) = delete;
	Generator &operator=(const Generator &other) = delete;

	bool writeHeader();
	bool writeSource();

private:
	QString typeToString(structure::Type type) const;
	QString typeToDefaultValue(structure::Type type) const;
	QString valueAssignmentCode(structure::Value value) const;

	bool writeHeaderStyleNamespace();
	bool writeStructsForwardDeclarations();
	bool writeStructsDefinitions();
	bool writePaletteDefinition();
	bool writeRefsDeclarations();

	bool writeIncludesInSource();
	bool writeVariableDefinitions();
	bool writeRefsDefinition();
	bool writeSetPaletteColor();
	bool writeVariableInit();
	bool writePxValuesInit();
	bool writeFontFamiliesInit();
	bool writeIconValues();
	bool writeIconsInit();

	bool collectUniqueValues();

	const structure::Module &module_;
	QString basePath_, baseName_;
	const common::ProjectInfo &project_;
	std::unique_ptr<common::CppFile> source_, header_;
	bool isPalette_ = false;

	QMap<int, bool> pxValues_;
	QMap<std::string, int> fontFamilies_;
	QMap<QString, int> iconMasks_; // icon file -> index
	std::map<QString, int, std::greater<QString>> paletteIndices_;

	std::vector<int> _scales = { 4, 5, 6, 8 }; // scale / 4 gives our 1.00, 1.25, 1.50, 2.00
	std::vector<const char *> _scaleNames = { "dbisOne", "dbisOneAndQuarter", "dbisOneAndHalf", "dbisTwo" };

};

} // namespace style
} // namespace codegen
