/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"

namespace Ui {

class InputField;
class IconButton;
enum class InputSubmitSettings;

class RateCallBox : public Ui::BoxContent {
public:
	RateCallBox(QWidget*, InputSubmitSettings sendWay);

	struct Result {
		int rating = 0;
		QString comment;
	};

	[[nodiscard]] rpl::producer<Result> sends() const;

protected:
	void prepare() override;
	void setInnerFocus() override;

	void resizeEvent(QResizeEvent *e) override;

private:
	void updateMaxHeight();
	void ratingChanged(int value);
	void send();
	void commentResized();

	const InputSubmitSettings _sendWay;
	int _rating = 0;

	std::vector<object_ptr<Ui::IconButton>> _stars;
	object_ptr<Ui::InputField> _comment = { nullptr };

	rpl::event_stream<Result> _sends;

};

} // namespace Ui
