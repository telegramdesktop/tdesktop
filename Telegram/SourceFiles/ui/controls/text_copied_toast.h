/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"

namespace Ui::Toast {
class Instance;
} // namespace Ui::Toast

namespace Ui {

class Show;
class RpWidget;

void ShowTextCopiedToast(const std::shared_ptr<Show> &show);

class TextCopiedToastPending final {
public:
	explicit TextCopiedToastPending(std::shared_ptr<Show> show);
	~TextCopiedToastPending();

	void finished();

private:
	struct State;

	const std::shared_ptr<Show> _show;
	base::weak_ptr<Toast::Instance> _toast;
	QPointer<RpWidget> _slot;
	base::weak_ptr<State> _state;
	bool _finished = false;

};

} // namespace Ui
