/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

template <typename Object>
class object_ptr;

namespace style {
struct TextStyle;
struct PeerListItem;
struct DialogRow;
} // namespace style

namespace Ui {

class RpWidget;

class LoadingElement {
public:
	virtual ~LoadingElement() = default;

	[[nodiscard]] virtual int height() const = 0;
	virtual void paint(QPainter &p, int width) = 0;
};

class LoadingLine final : public LoadingElement {
public:
	LoadingLine(int thickness, int skip, const QColor &color);

	[[nodiscard]] int height() const override;
	void paint(QPainter &p, int width) override;

private:
	const int _thickness;
	const int _skip;
	const QColor _color;

};

object_ptr<Ui::RpWidget> CreateLoadingTextWidget(
	not_null<Ui::RpWidget*> parent,
	const style::TextStyle &st,
	int lines,
	rpl::producer<bool> rtl);

object_ptr<Ui::RpWidget> CreateLoadingPeerListItemWidget(
	not_null<Ui::RpWidget*> parent,
	const style::PeerListItem &st,
	int lines,
	std::optional<QColor> bgOverride);

object_ptr<Ui::RpWidget> CreateLoadingDialogRowWidget(
	not_null<Ui::RpWidget*> parent,
	const style::DialogRow &st,
	int lines);

} // namespace Ui
