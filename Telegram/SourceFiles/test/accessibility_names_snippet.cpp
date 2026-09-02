// Standalone accessibility verification snippet for Phase 3.
//
// This file is intentionally a self-contained snippet (it has its own main())
// so it can be compiled directly against Qt + lib_ui in a full desktop-app
// build environment. It is NOT wired into the Test::Runner harness because the
// name checks do not need a session or a window; they only need a live
// QApplication, the style system, and Ui::Integration started.
//
// Build (in a configured desktop-app tree, sketch):
//   c++ accessibility_names_snippet.cpp \
//     -I<lib_ui> -I<lib_base> -I<rpl> \
//     -I$QT/include $(pkg-config --cflags --libs Qt6Widgets Qt6Gui) \
//     -l_ui -l_base -l_rpl -o accessibility_names_snippet
//
// Then run with a screen-reader-capable platform plugin, e.g.:
//   QT_ACCESSIBILITY=1 ./accessibility_names_snippet
//
// Expected output:
//   PASS: AbstractButton icon-only fallback exposes tooltip as name
//   PASS: SettingsButton exposes its text as the accessible name

#include <QtWidgets/QApplication>
#include <QtWidgets/QWidget>
#include <QtGui/QAccessible>

#include "ui/abstract_button.h"
#include "ui/widgets/buttons.h"
#include "ui/integration.h"
#include "styles/style_widgets.h"

#include <cassert>
#include <iostream>

int main(int argc, char **argv) {
	QApplication app(argc, argv);
	Ui::Integration::Instance().setScale(1.);

	QWidget parent;
	parent.show();

	// 1) Icon-only button: no text, only a tooltip. The new AbstractButton
	//    fallback must surface the tooltip as the accessible name.
	auto iconButton = Ui::CreateChild<Ui::IconButton>(
		&parent,
		st::defaultIconButton);
	iconButton->setToolTip(u"Close chat"_q);
	iconButton->show();

	{
		const auto iface = QAccessible::queryAccessibleInterface(iconButton);
		assert(iface);
		const auto name = iface->text(QAccessible::Name);
		std::cout
			<< (name == u"Close chat"_q ? "PASS" : "FAIL")
			<< ": AbstractButton icon-only fallback exposes tooltip as name"
			<< " (got: \"" << name.toStdString() << "\")\n";
		assert(name == u"Close chat"_q);
	}

	// 2) SettingsButton with text: text() must be exposed as the name.
	auto settings = Ui::CreateChild<Ui::SettingsButton>(
		&parent,
		rpl::single(u"Privacy and Security"_q));
	settings->show();

	{
		const auto iface = QAccessible::queryAccessibleInterface(settings);
		assert(iface);
		const auto name = iface->text(QAccessible::Name);
		std::cout
			<< (name == u"Privacy and Security"_q ? "PASS" : "FAIL")
			<< ": SettingsButton exposes its text as the accessible name"
			<< " (got: \"" << name.toStdString() << "\")\n";
		assert(name == u"Privacy and Security"_q);
	}

	std::cout << "All accessibility name checks passed.\n";
	return 0;
}
