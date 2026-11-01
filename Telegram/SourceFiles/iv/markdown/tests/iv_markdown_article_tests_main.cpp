/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/markdown/tests/iv_markdown_article_tests.h"

#include "base/platform/base_platform_file_utilities.h"
#include "ui/style/style_core.h"
#include "ui/style/style_core_font.h"
#include "ui/text/text.h"

#include <QtCore/QFile>
#include <QtGui/QFontInfo>
#include <QtGui/QGuiApplication>

#include <array>
#include <cstdio>

#include "styles/style_basic.h"

namespace Iv::Markdown::Tests {
namespace {

[[nodiscard]] QString OffscreenPlatform() {
	auto expect = Expect();
	expect.text(
		u"platform"_q,
		QGuiApplication::platformName(),
		u"offscreen"_q);
	return expect.result();
}

[[nodiscard]] QString BundledFont() {
	auto expect = Expect();
	expect.text(
		u"family"_q,
		QFontInfo(st::normalFont->f).family(),
		u"Open Sans"_q);
	return expect.result();
}

[[nodiscard]] QString StyleScale() {
	auto expect = Expect();
	expect.number(u"scale"_q, style::Scale(), 100);
	expect.number(u"pixel size"_q, st::normalFont->f.pixelSize(), 13);
	return expect.result();
}

[[nodiscard]] QString TextLeaf() {
	auto leaf = Ui::Text::String();
	SetLeaf(leaf, u"Hello world"_q);
	auto expect = Expect();
	expect.number(u"length"_q, leaf.length(), 11);
	expect.text(
		u"copy [6,11)"_q,
		leaf.toTextForMimeData(TextSelection(6, 11)).expanded,
		u"world"_q);
	return expect.result();
}

[[nodiscard]] int RunGroups() {
	const auto groups = std::array{
		SetupChecks,
		RangeChecks,
		SegmentChecks,
		TableChecks,
		CodeChecks,
		RichCopyChecks,
	};
	auto passed = 0;
	auto total = 0;
	for (const auto group : groups) {
		for (const auto &check : group()) {
			const auto name = check.name.toUtf8();
			const auto reason = check.run();
			++total;
			if (reason.isEmpty()) {
				++passed;
				std::printf("PASS %s\n", name.constData());
				continue;
			}
			const auto utf8 = reason.toUtf8();
			std::printf("FAIL %s: %s\n", name.constData(), utf8.constData());
		}
	}
	std::printf("%d of %d checks passed.\n", passed, total);
	return (passed == total) ? 0 : 1;
}

} // namespace

std::vector<Check> SetupChecks() {
	return {
		{ u"setup.offscreen-platform"_q, OffscreenPlatform },
		{ u"setup.bundled-font"_q, BundledFont },
		{ u"setup.style-scale"_q, StyleScale },
		{ u"setup.text-leaf"_q, TextLeaf },
	};
}

} // namespace Iv::Markdown::Tests

int main(int argc, char *argv[]) {
	qputenv("QT_QPA_PLATFORM", "offscreen");
	auto app = QGuiApplication(argc, argv);

	const auto resources = base::Platform::BundledResourcesPath()
		+ u"/lib_ui.rcc"_q;
	if (!QFile::exists(resources)) {
		std::printf(
			"FAIL setup.resources: missing %s\n",
			resources.toUtf8().constData());
		return 2;
	}
	style::SetDevicePixelRatio(1);
	style::internal::StartFonts();
	style::StartManager(style::kScaleDefault);

	return Iv::Markdown::Tests::RunGroups();
}
