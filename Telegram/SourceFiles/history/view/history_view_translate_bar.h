/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/wrap/slide_wrap.h"

class History;
struct LanguageId;

namespace Ui {
class PlainShadow;
} // namespace Ui

namespace HistoryView {

class TranslateBar final {
public:
	TranslateBar(not_null<QWidget*> parent, not_null<History*> history);
	~TranslateBar();

	void show();
	void hide();
	void raise();
	void finishAnimating();

	void setShadowGeometryPostprocess(Fn<QRect(QRect)> postprocess);

	void move(int x, int y);
	void resizeToWidth(int width);
	[[nodiscard]] int height() const;
	[[nodiscard]] rpl::producer<int> heightValue() const;

	[[nodiscard]] rpl::lifetime &lifetime() {
		return _wrap.lifetime();
	}

private:
	void setup(not_null<History*> history);
	void updateShadowGeometry(QRect wrapGeometry);
	void updateControlsGeometry(QRect wrapGeometry);

	Ui::SlideWrap<> _wrap;
	std::unique_ptr<Ui::PlainShadow> _shadow;
	Fn<QRect(QRect)> _shadowGeometryPostprocess;
	bool _shouldBeShown = false;
	bool _forceHidden = false;

};

} // namespace HistoryView
