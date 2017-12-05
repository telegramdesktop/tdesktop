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

#include <rpl/event_stream.h>
#include "ui/rp_widget.h"
#include "base/flags.h"

namespace Ui {

class AbstractButton : public RpWidget {
	Q_OBJECT

public:
	AbstractButton(QWidget *parent);

	Qt::KeyboardModifiers clickModifiers() const {
		return _modifiers;
	}

	void setDisabled(bool disabled = true);
	virtual void clearState();
	bool isOver() const {
		return _state & StateFlag::Over;
	}
	bool isDown() const {
		return _state & StateFlag::Down;
	}
	bool isDisabled() const {
		return _state & StateFlag::Disabled;
	}

	void setPointerCursor(bool enablePointerCursor);

	void setAcceptBoth(bool acceptBoth = true);

	void setClickedCallback(base::lambda<void()> callback) {
		_clickedCallback = std::move(callback);
	}

	auto clicks() const {
		return _clicks.events();
	}
	template <typename Handler>
	void addClickHandler(Handler &&handler) {
		clicks() | rpl::start_with_next(
			std::forward<Handler>(handler),
			lifetime());
	}

protected:
	void enterEventHook(QEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

signals:
	void clicked();

protected:
	enum class StateFlag {
		None     = 0,
		Over     = (1 << 0),
		Down     = (1 << 1),
		Disabled = (1 << 2),
	};
	friend constexpr bool is_flag_type(StateFlag) { return true; };
	using State = base::flags<StateFlag>;

	State state() const {
		return _state;
	}

	enum class StateChangeSource {
		ByUser = 0x00,
		ByPress = 0x01,
		ByHover = 0x02,
	};
	void setOver(bool over, StateChangeSource source = StateChangeSource::ByUser);

	virtual void onStateChanged(State was, StateChangeSource source) {
	}

private:
	void updateCursor();
	void checkIfOver(QPoint localPos);

	State _state = StateFlag::None;

	bool _acceptBoth = false;
	Qt::KeyboardModifiers _modifiers;
	bool _enablePointerCursor = true;

	base::lambda<void()> _clickedCallback;

	rpl::event_stream<> _clicks;

};

} // namespace Ui
