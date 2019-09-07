/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/abstract_button.h"
#include "ui/effects/animations.h"
#include "styles/style_widgets.h"

#include <memory>

namespace Ui {

class RippleAnimation;
class NumbersAnimation;

class LinkButton : public AbstractButton {
public:
	LinkButton(QWidget *parent, const QString &text, const style::LinkButton &st = st::defaultLinkButton);

	int naturalWidth() const override;

	void setText(const QString &text);
	void setColorOverride(std::optional<QColor> textFg);

protected:
	void paintEvent(QPaintEvent *e) override;

	void onStateChanged(State was, StateChangeSource source) override;

private:
	const style::LinkButton &_st;
	QString _text;
	int _textWidth = 0;
	std::optional<QColor> _textFgOverride;

};

class RippleButton : public AbstractButton {
public:
	RippleButton(QWidget *parent, const style::RippleAnimation &st);

	void setForceRippled(
		bool rippled,
		anim::type animated = anim::type::normal);
	bool forceRippled() const {
		return _forceRippled;
	}

	static QPoint DisabledRippleStartPosition() {
		return QPoint(-0x3FFFFFFF, -0x3FFFFFFF);
	}

	void clearState() override;

	~RippleButton();

protected:
	void paintRipple(QPainter &p, int x, int y, const QColor *colorOverride = nullptr);

	void onStateChanged(State was, StateChangeSource source) override;

	virtual QImage prepareRippleMask() const;
	virtual QPoint prepareRippleStartPosition() const;

private:
	void ensureRipple();

	const style::RippleAnimation &_st;
	std::unique_ptr<RippleAnimation> _ripple;
	bool _forceRippled = false;
	rpl::lifetime _forceRippledSubscription;

};

class FlatButton : public RippleButton {
public:
	FlatButton(QWidget *parent, const QString &text, const style::FlatButton &st);

	void setText(const QString &text);
	void setWidth(int w);
	void setTextMargins(QMargins margins);

	int32 textWidth() const;

protected:
	void paintEvent(QPaintEvent *e) override;

	void onStateChanged(State was, StateChangeSource source) override;

private:
	QString _text;
	QMargins _textMargins;
	int _width = 0;

	const style::FlatButton &_st;

};

class RoundButton : public RippleButton, private base::Subscriber {
public:
	RoundButton(
		QWidget *parent,
		rpl::producer<QString> text,
		const style::RoundButton &st);

	void setText(rpl::producer<QString> text);

	void setNumbersText(const QString &numbersText) {
		setNumbersText(numbersText, numbersText.toInt());
	}
	void setNumbersText(int numbers) {
		setNumbersText(QString::number(numbers), numbers);
	}
	void setWidthChangedCallback(Fn<void()> callback);
	void finishNumbersAnimation();

	int contentWidth() const;

	void setFullWidth(int newFullWidth);
	void setFullRadius(bool enabled);

	enum class TextTransform {
		NoTransform,
		ToUpper,
	};
	void setTextTransform(TextTransform transform);

	~RoundButton();

protected:
	void paintEvent(QPaintEvent *e) override;

	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

private:
	void setNumbersText(const QString &numbersText, int numbers);
	void numbersAnimationCallback();
	void resizeToText(const QString &text);

	rpl::variable<QString> _textFull;
	QString _text;
	int _textWidth;

	std::unique_ptr<NumbersAnimation> _numbers;

	int _fullWidthOverride = 0;

	const style::RoundButton &_st;

	TextTransform _transform = TextTransform::ToUpper;
	bool _fullRadius = false;

};

class IconButton : public RippleButton {
public:
	IconButton(QWidget *parent, const style::IconButton &st);

	// Pass nullptr to restore the default icon.
	void setIconOverride(const style::icon *iconOverride, const style::icon *iconOverOverride = nullptr);
	void setRippleColorOverride(const style::color *colorOverride);

protected:
	void paintEvent(QPaintEvent *e) override;

	void onStateChanged(State was, StateChangeSource source) override;

	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

private:
	const style::IconButton &_st;
	const style::icon *_iconOverride = nullptr;
	const style::icon *_iconOverrideOver = nullptr;
	const style::color *_rippleColorOverride = nullptr;

	Ui::Animations::Simple _a_over;

};

class CrossButton : public RippleButton {
public:
	CrossButton(QWidget *parent, const style::CrossButton &st);

	void toggle(bool shown, anim::type animated);
	void show(anim::type animated) {
		return toggle(true, animated);
	}
	void hide(anim::type animated) {
		return toggle(false, animated);
	}
	void finishAnimating() {
		_showAnimation.stop();
		animationCallback();
	}

	bool toggled() const {
		return _shown;
	}
	void setLoadingAnimation(bool enabled);

protected:
	void paintEvent(QPaintEvent *e) override;

	void onStateChanged(State was, StateChangeSource source) override;

	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

private:
	bool loadingCallback(crl::time now);
	bool stopLoadingAnimation(crl::time now);
	void animationCallback();

	const style::CrossButton &_st;

	bool _shown = false;
	Ui::Animations::Simple _showAnimation;

	crl::time _loadingStopMs = 0;
	Ui::Animations::Basic _loadingAnimation;

};

} // namespace Ui
