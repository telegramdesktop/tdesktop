/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/special_fields.h"

#include "lang/lang_keys.h"
#include "data/data_countries.h" // Data::ValidPhoneCode
#include "numbers.h"

#include <QtCore/QRegularExpression>

namespace Ui {
namespace {

constexpr auto kMaxUsernameLength = 32;

// Rest of the phone number, without country code (seen 12 at least),
// need more for service numbers.
constexpr auto kMaxPhoneTailLength = 32;

// Max length of country phone code.
constexpr auto kMaxPhoneCodeLength = 4;

} // namespace

CountryCodeInput::CountryCodeInput(
	QWidget *parent,
	const style::InputField &st)
: MaskedInputField(parent, st) {
}

void CountryCodeInput::startErasing(QKeyEvent *e) {
	setFocus();
	keyPressEvent(e);
}

void CountryCodeInput::codeSelected(const QString &code) {
	auto wasText = getLastText();
	auto wasCursor = cursorPosition();
	auto newText = '+' + code;
	auto newCursor = newText.size();
	setText(newText);
	_nosignal = true;
	correctValue(wasText, wasCursor, newText, newCursor);
	_nosignal = false;
	changed();
}

void CountryCodeInput::correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) {
	QString newText, addToNumber;
	int oldPos(nowCursor);
	int newPos(-1);
	int oldLen(now.length());
	int start = 0;
	int digits = 5;
	newText.reserve(oldLen + 1);
	if (oldLen && now[0] == '+') {
		if (start == oldPos) {
			newPos = newText.length();
		}
		++start;
	}
	newText += '+';
	for (int i = start; i < oldLen; ++i) {
		if (i == oldPos) {
			newPos = newText.length();
		}
		auto ch = now[i];
		if (ch.isDigit()) {
			if (!digits || !--digits) {
				addToNumber += ch;
			} else {
				newText += ch;
			}
		}
	}
	if (!addToNumber.isEmpty()) {
		auto validCode = Data::ValidPhoneCode(newText.mid(1));
		addToNumber = newText.mid(1 + validCode.length()) + addToNumber;
		newText = '+' + validCode;
	}
	setCorrectedText(now, nowCursor, newText, newPos);

	if (!_nosignal && was != newText) {
		_codeChanged.fire(newText.mid(1));
	}
	if (!addToNumber.isEmpty()) {
		_addedToNumber.fire_copy(addToNumber);
	}
}

PhonePartInput::PhonePartInput(QWidget *parent, const style::InputField &st)
: MaskedInputField(parent, st/*, tr::lng_phone_ph(tr::now)*/) {
}

void PhonePartInput::paintAdditionalPlaceholder(Painter &p) {
	if (!_pattern.isEmpty()) {
		auto t = getDisplayedText();
		auto ph = _additionalPlaceholder.mid(t.size());
		if (!ph.isEmpty()) {
			p.setClipRect(rect());
			auto phRect = placeholderRect();
			int tw = phFont()->width(t);
			if (tw < phRect.width()) {
				phRect.setLeft(phRect.left() + tw);
				placeholderAdditionalPrepare(p);
				p.drawText(phRect, ph, style::al_topleft);
			}
		}
	}
}

void PhonePartInput::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Backspace && cursorPosition() == 0) {
		_frontBackspaceEvent.fire_copy(e);
	} else {
		MaskedInputField::keyPressEvent(e);
	}
}

void PhonePartInput::correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) {
	QString newText;
	int oldPos(nowCursor), newPos(-1), oldLen(now.length()), digitCount = 0;
	for (int i = 0; i < oldLen; ++i) {
		if (now[i].isDigit()) {
			++digitCount;
		}
	}
	if (digitCount > kMaxPhoneTailLength) {
		digitCount = kMaxPhoneTailLength;
	}

	bool inPart = !_pattern.isEmpty();
	int curPart = -1, leftInPart = 0;
	newText.reserve(oldLen);
	for (int i = 0; i < oldLen; ++i) {
		if (i == oldPos && newPos < 0) {
			newPos = newText.length();
		}

		auto ch = now[i];
		if (ch.isDigit()) {
			if (!digitCount--) {
				break;
			}
			if (inPart) {
				if (leftInPart) {
					--leftInPart;
				} else {
					newText += ' ';
					++curPart;
					inPart = curPart < _pattern.size();
					leftInPart = inPart ? (_pattern.at(curPart) - 1) : 0;

					++oldPos;
				}
			}
			newText += ch;
		} else if (ch == ' ' || ch == '-' || ch == '(' || ch == ')') {
			if (inPart) {
				if (leftInPart) {
				} else {
					newText += ch;
					++curPart;
					inPart = curPart < _pattern.size();
					leftInPart = inPart ? _pattern.at(curPart) : 0;
				}
			} else {
				newText += ch;
			}
		}
	}
	auto newlen = newText.size();
	while (newlen > 0 && newText.at(newlen - 1).isSpace()) {
		--newlen;
	}
	if (newlen < newText.size()) {
		newText = newText.mid(0, newlen);
	}
	setCorrectedText(now, nowCursor, newText, newPos);
}

void PhonePartInput::addedToNumber(const QString &added) {
	setFocus();
	auto wasText = getLastText();
	auto wasCursor = cursorPosition();
	auto newText = added + wasText;
	auto newCursor = newText.size();
	setText(newText);
	setCursorPosition(added.length());
	correctValue(wasText, wasCursor, newText, newCursor);
	startPlaceholderAnimation();
}

void PhonePartInput::chooseCode(const QString &code) {
	_pattern = phoneNumberParse(code);
	if (!_pattern.isEmpty() && _pattern.at(0) == code.size()) {
		_pattern.pop_front();
	} else {
		_pattern.clear();
	}
	_additionalPlaceholder = QString();
	if (!_pattern.isEmpty()) {
		_additionalPlaceholder.reserve(20);
		for (const auto part : std::as_const(_pattern)) {
			_additionalPlaceholder.append(' ');
			_additionalPlaceholder.append(QString(part, QChar(0x2212)));
		}
	}
	setPlaceholderHidden(!_additionalPlaceholder.isEmpty());

	auto wasText = getLastText();
	auto wasCursor = cursorPosition();
	auto newText = getLastText();
	auto newCursor = newText.size();
	correctValue(wasText, wasCursor, newText, newCursor);

	startPlaceholderAnimation();
}

UsernameInput::UsernameInput(
	QWidget *parent,
	const style::InputField &st,
	rpl::producer<QString> placeholder,
	const QString &val,
	const QString &linkPlaceholder)
: MaskedInputField(parent, st, std::move(placeholder), val) {
	setLinkPlaceholder(linkPlaceholder);
}

void UsernameInput::setLinkPlaceholder(const QString &placeholder) {
	_linkPlaceholder = placeholder;
	if (!_linkPlaceholder.isEmpty()) {
		setTextMargins(style::margins(_st.textMargins.left() + _st.font->width(_linkPlaceholder), _st.textMargins.top(), _st.textMargins.right(), _st.textMargins.bottom()));
		setPlaceholderHidden(true);
	}
}

void UsernameInput::paintAdditionalPlaceholder(Painter &p) {
	if (!_linkPlaceholder.isEmpty()) {
		p.setFont(_st.font);
		p.setPen(_st.placeholderFg);
		p.drawText(QRect(_st.textMargins.left(), _st.textMargins.top(), width(), height() - _st.textMargins.top() - _st.textMargins.bottom()), _linkPlaceholder, style::al_topleft);
	}
}

void UsernameInput::correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) {
	auto newPos = nowCursor;
	auto from = 0, len = now.size();
	for (; from < len; ++from) {
		if (!now.at(from).isSpace()) {
			break;
		}
		if (newPos > 0) --newPos;
	}
	len -= from;
	if (len > kMaxUsernameLength) {
		len = kMaxUsernameLength + (now.at(from) == '@' ? 1 : 0);
	}
	for (int32 to = from + len; to > from;) {
		--to;
		if (!now.at(to).isSpace()) {
			break;
		}
		--len;
	}
	setCorrectedText(now, nowCursor, now.mid(from, len), newPos);
}

QString ExtractPhonePrefix(const QString &phone) {
	const auto pattern = phoneNumberParse(phone);
	if (!pattern.isEmpty()) {
		return phone.mid(0, pattern[0]);
	}
	return QString();
}

PhoneInput::PhoneInput(
	QWidget *parent,
	const style::InputField &st,
	rpl::producer<QString> placeholder,
	const QString &defaultValue,
	QString value)
: MaskedInputField(parent, st, std::move(placeholder), value)
, _defaultValue(defaultValue) {
	if (value.isEmpty()) {
		clearText();
	} else {
		auto pos = value.size();
		correctValue(QString(), 0, value, pos);
	}
}

void PhoneInput::focusInEvent(QFocusEvent *e) {
	MaskedInputField::focusInEvent(e);
	setSelection(cursorPosition(), cursorPosition());
}

void PhoneInput::clearText() {
	auto value = _defaultValue;
	setText(value);
	auto pos = value.size();
	correctValue(QString(), 0, value, pos);
}

void PhoneInput::paintAdditionalPlaceholder(Painter &p) {
	if (!_pattern.isEmpty()) {
		auto t = getDisplayedText();
		auto ph = _additionalPlaceholder.mid(t.size());
		if (!ph.isEmpty()) {
			p.setClipRect(rect());
			auto phRect = placeholderRect();
			int tw = phFont()->width(t);
			if (tw < phRect.width()) {
				phRect.setLeft(phRect.left() + tw);
				placeholderAdditionalPrepare(p);
				p.drawText(phRect, ph, style::al_topleft);
			}
		}
	}
}

void PhoneInput::correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) {
	auto digits = now;
	digits.replace(QRegularExpression("[^\\d]"), QString());
	_pattern = phoneNumberParse(digits);

	QString newPlaceholder;
	if (_pattern.isEmpty()) {
		newPlaceholder = QString();
	} else if (_pattern.size() == 1 && _pattern.at(0) == digits.size()) {
		newPlaceholder = QString(_pattern.at(0) + 2, ' ') + tr::lng_contact_phone(tr::now);
	} else {
		newPlaceholder.reserve(20);
		for (int i = 0, l = _pattern.size(); i < l; ++i) {
			if (i) {
				newPlaceholder.append(' ');
			} else {
				newPlaceholder.append('+');
			}
			newPlaceholder.append(i ? QString(_pattern.at(i), QChar(0x2212)) : digits.mid(0, _pattern.at(i)));
		}
	}
	if (_additionalPlaceholder != newPlaceholder) {
		_additionalPlaceholder = newPlaceholder;
		setPlaceholderHidden(!_additionalPlaceholder.isEmpty());
		update();
	}

	QString newText;
	int oldPos(nowCursor), newPos(-1), oldLen(now.length()), digitCount = qMin(digits.size(), kMaxPhoneCodeLength + kMaxPhoneTailLength);

	bool inPart = !_pattern.isEmpty(), plusFound = false;
	int curPart = 0, leftInPart = inPart ? _pattern.at(curPart) : 0;
	newText.reserve(oldLen + 1);
	newText.append('+');
	for (int i = 0; i < oldLen; ++i) {
		if (i == oldPos && newPos < 0) {
			newPos = newText.length();
		}

		QChar ch(now[i]);
		if (ch.isDigit()) {
			if (!digitCount--) {
				break;
			}
			if (inPart) {
				if (leftInPart) {
					--leftInPart;
				} else {
					newText += ' ';
					++curPart;
					inPart = curPart < _pattern.size();
					leftInPart = inPart ? (_pattern.at(curPart) - 1) : 0;

					++oldPos;
				}
			}
			newText += ch;
		} else if (ch == ' ' || ch == '-' || ch == '(' || ch == ')') {
			if (inPart) {
				if (leftInPart) {
				} else {
					newText += ch;
					++curPart;
					inPart = curPart < _pattern.size();
					leftInPart = inPart ? _pattern.at(curPart) : 0;
				}
			} else {
				newText += ch;
			}
		} else if (ch == '+') {
			plusFound = true;
		}
	}
	if (!plusFound && newText == qstr("+")) {
		newText = QString();
		newPos = 0;
	}
	int32 newlen = newText.size();
	while (newlen > 0 && newText.at(newlen - 1).isSpace()) {
		--newlen;
	}
	if (newlen < newText.size()) {
		newText = newText.mid(0, newlen);
	}
	setCorrectedText(now, nowCursor, newText, newPos);
}

} // namespace Ui
