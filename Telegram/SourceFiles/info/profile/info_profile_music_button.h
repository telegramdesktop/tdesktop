/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/abstract_button.h"
#include "ui/text/format_song_name.h"
#include "ui/text/text.h"

namespace Info::Profile {

struct PillColors {
	QColor fill;
	QColor primary;
	QColor secondary;
};

[[nodiscard]] PillColors ComputePillColors(std::optional<QColor> edge);
void PaintPillBackground(
	QPainter &p,
	const QRect &rect,
	const PillColors &colors);
[[nodiscard]] int PillTextBaseline();
[[nodiscard]] int PillArrowTop();

struct MusicButtonData {
	Ui::Text::FormatSongName name;
};

class MusicButton final : public Ui::AbstractButton {
public:
	explicit MusicButton(QWidget *parent);
	~MusicButton();

	void updateData(MusicButtonData data);
	void setOverrideBg(std::optional<QColor> color);
	void setOpacity(float64 opacity);
	[[nodiscard]] bool actionable() const;

private:
	void paintEvent(QPaintEvent *e) override;
	int resizeGetHeight(int newWidth) override;

	[[nodiscard]] int chromeWidth() const;
	void refreshShares(int newWidth);
	void validateCache();
	void paintRun(
		QPainter &p,
		const Ui::Text::String &text,
		int left,
		int share,
		const QColor &color) const;

	Ui::Text::String _performer;
	Ui::Text::String _title;
	std::optional<QColor> _overrideBg;
	PillColors _colors;
	QImage _cache;

	const QString _noteSymbol;
	float64 _opacity = 0.;
	const int _noteWidth;
	const int _baseline;
	const int _arrowTop;
	int _textSkip = 0;
	int _performerShare = 0;
	int _titleShare = 0;

};

} // namespace Info::Profile
