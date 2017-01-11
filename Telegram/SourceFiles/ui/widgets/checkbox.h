/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "ui/widgets/buttons.h"
#include "styles/style_widgets.h"

namespace Ui {

class Checkbox : public RippleButton {
	Q_OBJECT

public:
	Checkbox(QWidget *parent, const QString &text, bool checked = false, const style::Checkbox &st = st::defaultCheckbox);

	bool checked() const;
	enum class NotifyAboutChange {
		Notify,
		DontNotify,
	};
	void setChecked(bool checked, NotifyAboutChange notify = NotifyAboutChange::Notify);

	void finishAnimations();

	QMargins getMargins() const override {
		return _st.margin;
	}
	int naturalWidth() const override;

protected:
	void paintEvent(QPaintEvent *e) override;

	void onStateChanged(State was, StateChangeSource source) override;
	int resizeGetHeight(int newWidth) override;

	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

public slots:
	void onClicked();

signals:
	void changed();

private:
	const style::Checkbox &_st;

	Text _text;
	QRect _checkRect;

	bool _checked;
	Animation _a_checked;

};

class Radiobutton : public RippleButton {
	Q_OBJECT

public:
	Radiobutton(QWidget *parent, const QString &group, int value, const QString &text, bool checked = false, const style::Checkbox &st = st::defaultCheckbox);

	bool checked() const;
	void setChecked(bool checked);

	int val() const {
		return _value;
	}

	QMargins getMargins() const override {
		return _st.margin;
	}
	int naturalWidth() const override;

	~Radiobutton();

protected:
	void paintEvent(QPaintEvent *e) override;

	void onStateChanged(State was, StateChangeSource source) override;
	int resizeGetHeight(int newWidth) override;

	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

public slots:
	void onClicked();

signals:
	void changed();

private:
	void onChanged();

	const style::Checkbox &_st;

	Text _text;
	QRect _checkRect;

	bool _checked;
	Animation _a_checked;

	void *_group;
	int _value;

};

} // namespace Ui
