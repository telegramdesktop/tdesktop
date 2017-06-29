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

	void setText(const QString &text);

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
	void resizeToText();

	const style::Checkbox &_st;

	Text _text;
	QRect _checkRect;

	bool _checked;
	Animation _a_checked;

};

class Radiobutton;

class RadiobuttonGroup {
public:
	RadiobuttonGroup() = default;
	RadiobuttonGroup(int value) : _value(value), _hasValue(true) {
	}

	void setChangedCallback(base::lambda<void(int value)> callback) {
		_changedCallback = std::move(callback);
	}

	bool hasValue() const {
		return _hasValue;
	}
	int value() const {
		return _value;
	}
	void setValue(int value);

private:
	friend class Radiobutton;
	void registerButton(Radiobutton *button) {
		if (!base::contains(_buttons, button)) {
			_buttons.push_back(button);
		}
	}
	void unregisterButton(Radiobutton *button) {
		_buttons.erase(std::remove(_buttons.begin(), _buttons.end(), button), _buttons.end());
	}

	int _value = 0;
	bool _hasValue = false;
	base::lambda<void(int value)> _changedCallback;
	std::vector<Radiobutton*> _buttons;

};

class Radiobutton : public RippleButton {
public:
	Radiobutton(QWidget *parent, const std::shared_ptr<RadiobuttonGroup> &group, int value, const QString &text, const style::Checkbox &st = st::defaultCheckbox);

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

private:
	friend class RadiobuttonGroup;
	void handleNewGroupValue(int value);

	std::shared_ptr<RadiobuttonGroup> _group;
	int _value = 0;

	const style::Checkbox &_st;

	Text _text;
	QRect _checkRect;

	bool _checked = false;
	Animation _a_checked;

};

template <typename Enum>
class Radioenum;

template <typename Enum>
class RadioenumGroup {
public:
	RadioenumGroup() = default;
	RadioenumGroup(Enum value) : _group(static_cast<int>(value)) {
	}

	template <typename Callback>
	void setChangedCallback(Callback &&callback) {
		_group.setChangedCallback([callback](int value) {
			callback(static_cast<Enum>(value));
		});
	}

	bool hasValue() const {
		return _group.hasValue();
	}
	Enum value() const {
		return static_cast<Enum>(_group.value());
	}
	void setValue(Enum value) {
		_group.setValue(static_cast<int>(value));
	}

private:
	template <typename OtherEnum>
	friend class Radioenum;

	RadiobuttonGroup _group;

};

template <typename Enum>
class Radioenum : public Radiobutton {
public:
	Radioenum(QWidget *parent, const std::shared_ptr<RadioenumGroup<Enum>> &group, Enum value, const QString &text, const style::Checkbox &st = st::defaultCheckbox)
		: Radiobutton(parent, std::shared_ptr<RadiobuttonGroup>(group, &group->_group), static_cast<int>(value), text, st) {
	}

};

class ToggleView {
public:
	ToggleView(const style::Toggle &st, bool toggled, base::lambda<void()> updateCallback);

	void setToggledFast(bool toggled);
	void setToggledAnimated(bool toggled);
	void finishAnimation();
	void setStyle(const style::Toggle &st);
	void setUpdateCallback(base::lambda<void()> updateCallback);

	void paint(Painter &p, int left, int top, int outerWidth, TimeMs ms);

private:
	gsl::not_null<const style::Toggle*> _st;
	bool _toggled = false;
	base::lambda<void()> _updateCallback;
	Animation _toggleAnimation;

};

} // namespace Ui
