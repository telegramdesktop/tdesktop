/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Profile {

class CoverDropArea : public TWidget {
public:
	CoverDropArea(QWidget *parent, const QString &title, const QString &subtitle);

	void showAnimated();

	using HideFinishCallback = base::lambda<void(CoverDropArea*)>;
	void hideAnimated(HideFinishCallback &&callback);

	bool hiding() const {
		return _hiding;
	}

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	void setupAnimation();

	QString _title, _subtitle;
	int _titleWidth, _subtitleWidth;

	QPixmap _cache;
	Animation _a_appearance;
	bool _hiding = false;
	HideFinishCallback _hideFinishCallback;

};

} // namespace Profile
