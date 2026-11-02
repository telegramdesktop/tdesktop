/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "test/test_widget_ops.h"

#include <QtCore/QByteArray>

#include <vector>

// widget-sandbox's shared paint-policy fixture against the heavy-box rule
// Telegram compiles: TwidgetCore's twlite::paint and the glue's limit
// functions in twidget/renderer.h.
// Reference: docs/helpers/test_widget_paint_policy.md.

namespace Test {

// Harness-only: left undefined in a build without the harness, because no
// production code calls it.
//
// Any thread, synchronous; reads no app state and no file. |fixture| is the
// bytes of widget-sandbox's packages/core/tests/fixtures/paint-policy.json,
// read and kept by the caller. Returns one row per entry of each section
// (constants, shadows, boxes, hidesSubtree, animations, messages, reports,
// cases), in fixture order, named by section and 1-based number, then one
// coverage row: the bytes parse, every top-level key is known, every
// section is present and nonempty, and no case step is of an unknown kind.
// Each case runs all its steps on one PaintCounter, recording every
// mismatching check and going on after it. `reports` judges a labelled
// mirror of WidgetHost's reading of a `limit` report, not the host itself,
// plus Renderer::limitExceeded() on a stub renderer. The caller turns each
// row into a Check().
[[nodiscard]] std::vector<WidgetOpsSelfCheckRow> RunWidgetPaintPolicyCheck(
	const QByteArray &fixture);

} // namespace Test
