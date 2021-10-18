/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/widgets/sent_code_field.h"

#include "lang/lang_keys.h"

#include <QRegularExpression>

namespace Ui {

SentCodeField::SentCodeField(
	QWidget *parent,
	const style::InputField &st,
	rpl::producer<QString> placeholder,
	const QString &val)
: Ui::InputField(parent, st, std::move(placeholder), val) {
	connect(this, &Ui::InputField::changed, [this] { fix(); });
}

void SentCodeField::setAutoSubmit(int length, Fn<void()> submitCallback) {
	_autoSubmitLength = length;
	_submitCallback = std::move(submitCallback);
}

void SentCodeField::setChangedCallback(Fn<void()> changedCallback) {
	_changedCallback = std::move(changedCallback);
}

QString SentCodeField::getDigitsOnly() const {
	return QString(
		getLastText()
	).remove(
		QRegularExpression("[^\\d]")
	);
}

void SentCodeField::fix() {
	if (_fixing) return;

	_fixing = true;
	auto newText = QString();
	const auto now = getLastText();
	auto oldPos = textCursor().position();
	auto newPos = -1;
	auto oldLen = now.size();
	auto digitCount = 0;
	for (const auto &ch : now) {
		if (ch.isDigit()) {
			++digitCount;
		}
	}

	if (_autoSubmitLength > 0 && digitCount > _autoSubmitLength) {
		digitCount = _autoSubmitLength;
	}
	const auto strict = (_autoSubmitLength > 0)
		&& (digitCount == _autoSubmitLength);

	newText.reserve(oldLen);
	int i = 0;
	for (const auto &ch : now) {
		if (i++ == oldPos) {
			newPos = newText.length();
		}
		if (ch.isDigit()) {
			if (!digitCount--) {
				break;
			}
			newText += ch;
			if (strict && !digitCount) {
				break;
			}
		} else if (ch == '-') {
			newText += ch;
		}
	}
	if (newPos < 0) {
		newPos = newText.length();
	}
	if (newText != now) {
		setText(newText);
		setCursorPosition(newPos);
	}
	_fixing = false;

	if (_changedCallback) {
		_changedCallback();
	}
	if (strict && _submitCallback) {
		_submitCallback();
	}
}

SentCodeCall::SentCodeCall(
	FnMut<void()> callCallback,
	Fn<void()> updateCallback)
: _call(std::move(callCallback))
, _update(std::move(updateCallback)) {
	_timer.setCallback([=] {
		if (_status.state == State::Waiting) {
			if (--_status.timeout <= 0) {
				_status.state = State::Calling;
				_timer.cancel();
				if (_call) {
					_call();
				}
			}
		}
		if (_update) {
			_update();
		}
	});
}

void SentCodeCall::setStatus(const Status &status) {
	_status = status;
	if (_status.state == State::Waiting) {
		_timer.callEach(1000);
	}
}

QString SentCodeCall::getText() const {
	switch (_status.state) {
	case State::Waiting: {
		if (_status.timeout >= 3600) {
			return tr::lng_code_call(
				tr::now,
				lt_minutes,
				(u"%1:%2"_q)
					.arg(_status.timeout / 3600)
					.arg((_status.timeout / 60) % 60, 2, 10, QChar('0')),
				lt_seconds,
				(u"%1"_q).arg(_status.timeout % 60, 2, 10, QChar('0')));
		}
		return tr::lng_code_call(
			tr::now,
			lt_minutes,
			QString::number(_status.timeout / 60),
			lt_seconds,
			(u"%1"_q).arg(_status.timeout % 60, 2, 10, QChar('0')));
	} break;
	case State::Calling: return tr::lng_code_calling(tr::now);
	case State::Called: return tr::lng_code_called(tr::now);
	}
	return QString();
}

void SentCodeCall::callDone() {
	if (_status.state == State::Calling) {
		_status.state = State::Called;
		if (_update) {
			_update();
		}
	}
}

} // namespace Ui
