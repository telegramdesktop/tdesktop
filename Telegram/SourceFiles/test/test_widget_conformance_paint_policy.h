/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "test/test_widget_ops.h"

#include <QtCore/QString>

#include <vector>

// The widget conformance runner's read of the paint-policy fixture of the
// widget-sandbox pin its package is in, for its first stage. Internal to
// test_widget_conformance.cpp; reference:
// docs/helpers/test_widget_conformance.md.

namespace Test::Conformance {

// |package| is the absolute packages/conformance directory. Synchronous,
// retains nothing: reads the cleaned path of its sibling
// ../core/tests/fixtures/paint-policy.json and returns
// RunWidgetPaintPolicyCheck()'s rows of those bytes verbatim, in their
// order (test_widget_paint_policy.h), or one failed "paint policy
// fixture: " row with the path and the error when |package| is not
// absolute or the file cannot be opened or read. Empty or unparsable
// bytes give that check's own failed coverage row.
[[nodiscard]] std::vector<WidgetOpsSelfCheckRow> RunPaintPolicyFixture(
	const QString &package);

} // namespace Test::Conformance
