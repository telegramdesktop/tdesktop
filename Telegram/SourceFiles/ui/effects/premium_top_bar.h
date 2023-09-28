/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"
#include "ui/rp_widget.h"
#include "ui/effects/premium_stars_colored.h"

namespace Ui {
class FlatLabel;
} // namespace Ui

namespace Ui::Premium {

[[nodiscard]] QString Svg();
[[nodiscard]] QByteArray ColorizedSvg();
[[nodiscard]] QImage GenerateStarForLightTopBar(QRectF rect);

class TopBarAbstract : public RpWidget {
public:
	using RpWidget::RpWidget;

	void setRoundEdges(bool value);

	virtual void setPaused(bool paused) = 0;
	virtual void setTextPosition(int x, int y) = 0;

	[[nodiscard]] virtual rpl::producer<int> additionalHeight() const = 0;

protected:
	void paintEdges(QPainter &p, const QBrush &brush) const;
	void paintEdges(QPainter &p) const;

	[[nodiscard]] QRectF starRect(
		float64 topProgress,
		float64 sizeProgress) const;

	[[nodiscard]] bool isDark() const;
	void computeIsDark();

private:
	bool _roundEdges = true;
	bool _isDark = false;

};

class TopBar final : public TopBarAbstract {
public:
	TopBar(
		not_null<QWidget*> parent,
		Fn<QVariant()> clickContextOther,
		rpl::producer<QString> title,
		rpl::producer<TextWithEntities> about,
		bool light = false);
	~TopBar();

	void setPaused(bool paused) override;
	void setTextPosition(int x, int y) override;

	rpl::producer<int> additionalHeight() const override;

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	const bool _light = false;
	const style::font &_titleFont;
	const style::margins &_titlePadding;
	object_ptr<FlatLabel> _about;
	ColoredMiniStars _ministars;
	QSvgRenderer _star;

	struct {
		float64 top = 0.;
		float64 body = 0.;
		float64 title = 0.;
		float64 scaleTitle = 0.;
	} _progress;

	QRectF _starRect;

	QPoint _titlePosition;
	QPainterPath _titlePath;

};

} // namespace Ui::Premium
