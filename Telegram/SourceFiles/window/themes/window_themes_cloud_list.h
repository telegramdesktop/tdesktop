/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_cloud_themes.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/checkbox.h"
#include "base/unique_qptr.h"
#include "base/binary_guard.h"

namespace style {
struct colorizer;
} // namespace style

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Window {

class SessionController;

namespace Theme {

struct EmbeddedScheme;

struct CloudListColors {
	QImage background;
	QColor sent;
	QColor received;
	QColor radiobuttonInactive;
	QColor radiobuttonActive;
};

[[nodiscard]] CloudListColors ColorsFromScheme(const EmbeddedScheme &scheme);
[[nodiscard]] CloudListColors ColorsFromScheme(
	const EmbeddedScheme &scheme,
	const style::colorizer &colorizer);

class CloudListCheck final : public Ui::AbstractCheckView {
public:
	using Colors = CloudListColors;

	explicit CloudListCheck(bool checked);
	CloudListCheck(const Colors &colors, bool checked);

	QSize getSize() const override;
	void paint(
		Painter &p,
		int left,
		int top,
		int outerWidth) override;
	QImage prepareRippleMask() const override;
	bool checkRippleStartPosition(QPoint position) const override;

	void setColors(const Colors &colors);

private:
	void paintNotSupported(Painter &p, int left, int top, int outerWidth);
	void paintWithColors(Painter &p, int left, int top, int outerWidth);
	void checkedChangedHook(anim::type animated) override;
	void validateBackgroundCache(int width);
	void ensureContrast();

	std::optional<Colors> _colors;
	Ui::RadioView _radio;
	QImage _backgroundFull;
	QImage _backgroundCache;
	int _backgroundCacheWidth = -1;

};

class CloudList final {
public:
	CloudList(
		not_null<QWidget*> parent,
		not_null<Window::SessionController*> window);

	void showAll();
	[[nodiscard]] rpl::producer<bool> empty() const;
	[[nodiscard]] rpl::producer<bool> allShown() const;
	[[nodiscard]] object_ptr<Ui::RpWidget> takeWidget();

private:
	struct Element {
		Data::CloudTheme theme;
		not_null<CloudListCheck*> check;
		std::unique_ptr<Ui::Radiobutton> button;
		std::shared_ptr<Data::DocumentMedia> media;
		base::binary_guard generating;
		bool waiting = false;

		uint64 id() const {
			return theme.id;
		}
	};
	void setup();
	[[nodiscard]] std::vector<Data::CloudTheme> collectAll() const;
	void rebuildUsing(std::vector<Data::CloudTheme> &&list);
	bool applyChangesFrom(std::vector<Data::CloudTheme> &&list);
	bool removeStaleUsing(const std::vector<Data::CloudTheme> &list);
	bool insertTillLimit(
		const std::vector<Data::CloudTheme> &list,
		int limit);
	void refreshElementUsing(Element &element, const Data::CloudTheme &data);
	void insert(int index, const Data::CloudTheme &theme);
	void refreshColors(Element &element);
	void showMenu(Element &element);
	void refreshColorsFromDocument(Element &element);
	void setWaiting(Element &element, bool waiting);
	void subscribeToDownloadFinished();
	int resizeGetHeight(int newWidth);
	void updateGeometry();

	[[nodiscard]] bool amCreator(const Data::CloudTheme &theme) const;
	[[nodiscard]] int groupValueForId(uint64 id);

	const not_null<Window::SessionController*> _window;
	object_ptr<Ui::RpWidget> _owned;
	const not_null<Ui::RpWidget*> _outer;
	const std::shared_ptr<Ui::RadiobuttonGroup> _group;
	rpl::variable<bool> _showAll = false;
	rpl::variable<int> _count = 0;
	std::vector<Element> _elements;
	std::vector<uint64> _idByGroupValue;
	base::flat_map<uint64, int> _groupValueById;
	rpl::lifetime _downloadFinishedLifetime;
	base::unique_qptr<Ui::PopupMenu> _contextMenu;

};

} // namespace Theme
} // namespace Window
