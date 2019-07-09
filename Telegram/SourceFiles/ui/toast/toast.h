/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animations.h"

namespace Ui {
namespace Toast {

namespace internal {
	class Manager;
	class Widget;
} // namespace internal

static constexpr const int DefaultDuration = 1500;
struct Config {
	QString text;
	int durationMs = DefaultDuration;
	int maxWidth = 0;
	QMargins padding;
};
void Show(QWidget *parent, const Config &config);
void Show(const Config &config);
void Show(const QString &text);

class Instance {
	struct Private {
	};

public:

	Instance(const Config &config, QWidget *widgetParent, const Private &);
	Instance(const Instance &other) = delete;
	Instance &operator=(const Instance &other) = delete;

	void hideAnimated();
	void hide();

private:
	void opacityAnimationCallback();

	bool _hiding = false;
	Ui::Animations::Simple _a_opacity;

	const crl::time _hideAtMs;

	// ToastManager should reset _widget pointer if _widget is destroyed.
	friend class internal::Manager;
	friend void Show(QWidget *parent, const Config &config);
	std::unique_ptr<internal::Widget> _widget;

};

} // namespace Toast
} // namespace Ui
