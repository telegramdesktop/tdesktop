/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/wrap/slide_wrap.h"
#include "spellcheck/spellcheck_types.h"

class History;
struct LanguageId;

namespace Ui {
class PlainShadow;
class PopupMenu;
class IconButton;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace HistoryView {

class TranslateBar final {
public:
	TranslateBar(
		not_null<QWidget*> parent,
		not_null<Window::SessionController*> controller,
		not_null<History*> history);
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
	[[nodiscard]] base::unique_qptr<Ui::PopupMenu> createMenu(
		not_null<Ui::IconButton*> button);
	void showMenu(base::unique_qptr<Ui::PopupMenu> menu);

	void showHiddenToast(not_null<PeerData*> peer);
	void showSettingsToast(not_null<PeerData*> peer, LanguageId ignored);
	void showToast(
		TextWithEntities text,
		const QString &buttonText,
		Fn<void()> buttonCallback);

	const not_null<Window::SessionController*> _controller;
	const not_null<History*> _history;
	Ui::SlideWrap<> _wrap;
	std::unique_ptr<Ui::PlainShadow> _shadow;
	Fn<QRect(QRect)> _shadowGeometryPostprocess;
	base::unique_qptr<Ui::PopupMenu> _menu;
	rpl::variable<LanguageId> _overridenTo;
	rpl::variable<LanguageId> _to;
	bool _shouldBeShown = false;
	bool _forceHidden = false;

};

} // namespace HistoryView
