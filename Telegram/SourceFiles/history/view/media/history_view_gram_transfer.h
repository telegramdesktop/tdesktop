/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"

namespace HistoryView {

class Element;
class Media;

class GramReadLine final : public base::has_weak_ptr {
public:
	explicit GramReadLine(Fn<void()> repaint);

	struct Turn {
		crl::time at = 0;
		rpl::lifetime waiting;
	};
	[[nodiscard]] Turn join(crl::time now);

private:
	void leave(crl::time at);
	void schedule(crl::time now);

	const Fn<void()> _repaint;
	std::vector<crl::time> _turns;
	base::Timer _timer;

};

[[nodiscard]] std::unique_ptr<Media> CreateGramTransferMedia(
	not_null<Element*> parent,
	Element *replacing);

} // namespace HistoryView
