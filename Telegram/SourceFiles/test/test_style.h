/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include <QtCore/QString>
#include <QtGui/QColor>

#include <vector>

namespace Test {

// Reading a colour from the live chat style before that style has
// finished resolving measures the previous palette, not the surface
// that is about to paint. HistoryInner paints from its own cached
// ChatTheme — not controller->defaultChatTheme() — as soon as a theme
// or wallpaper resolves, and the accent palette can still move after
// the chat is already open. Two disposable overlays independently
// lost a clean-looking run to this: one compared an ordinary arm
// that was still on a wallpaper/gradient object, the other recovered
// ink against a baseline taken before the accent settled.
//
// StyleSettled answers when those values may be read: it returns only
// after the caller-supplied probe has been unchanged for |stableFor|.
// StyleBaseline answers whether they still hold at a later frame:
// assertHolds FAILs a moved colour or identity instead of treating the
// current value as the recorded one.

inline constexpr auto kStyleColorTolerance = 0;

struct StyleSample {
	QString label;
	QColor color;
	quintptr token = 0;
	bool identity = false;
};

using StyleProbe = Fn<std::vector<StyleSample>()>;

[[nodiscard]] bool StyleSettled(
	const QString &name,
	const StyleProbe &probe,
	crl::time stableFor,
	crl::time deadline,
	int colorTolerance = kStyleColorTolerance);

class StyleBaseline final {
public:
	void add(const QString &label, QColor color);
	void addIdentity(const QString &label, quintptr token);

	void assertHolds(
		const QString &name,
		const StyleProbe &probe,
		int colorTolerance = kStyleColorTolerance) const;

	[[nodiscard]] bool empty() const;

private:
	std::vector<StyleSample> _values;

};

} // namespace Test
