/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class PeerData;

namespace Window {
class SessionController;
} // namespace Window

namespace Data {
struct ChatTheme;
} // namespace Data

namespace Ui {

class RpWidget;
class PlainShadow;
class VerticalLayout;

class ChooseThemeController final {
public:
	ChooseThemeController(
		not_null<RpWidget*> parent,
		not_null<Window::SessionController*> window,
		not_null<PeerData*> peer);
	~ChooseThemeController();

	[[nodiscard]] bool shouldBeShown() const;
	[[nodiscard]] rpl::producer<bool> shouldBeShownValue() const;
	[[nodiscard]] int height() const;

	void hide();
	void show();
	void raise();
	void setFocus();

	[[nodiscard]] rpl::lifetime &lifetime();

private:
	struct Entry;

	void init(rpl::producer<QSize> outer);
	void initButtons();
	void initList();
	void fill(const std::vector<Data::ChatTheme> &themes);
	void close();

	void clearCurrentBackgroundState();
	void paintEntry(QPainter &p, const Entry &entry);
	void applyInitialInnerLeft();
	void updateInnerLeft(int now);

	[[nodiscard]] Entry *findChosen();
	[[nodiscard]] const Entry *findChosen() const;

	const not_null<Window::SessionController*> _controller;
	const not_null<PeerData*> _peer;
	const std::unique_ptr<VerticalLayout> _wrap;
	const std::unique_ptr<PlainShadow> _topShadow;

	const not_null<RpWidget*> _content;
	const not_null<RpWidget*> _inner;
	std::vector<Entry> _entries;
	QString _pressed;
	QString _chosen;
	std::optional<QPoint> _pressPosition;
	std::optional<QPoint> _dragStartPosition;
	int _dragStartInnerLeft = 0;
	bool _initialInnerLeftApplied = false;

	rpl::variable<bool> _shouldBeShown = false;
	rpl::variable<bool> _forceHidden = false;
	rpl::variable<bool> _dark = false;
	rpl::lifetime _cachingLifetime;

};

} // namespace Ui
