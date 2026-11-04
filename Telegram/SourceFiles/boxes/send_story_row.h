/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/text/text.h"
#include "ui/userpic_view.h"
#include "ui/widgets/buttons.h"

namespace Ui {
class RadioView;
} // namespace Ui

namespace style {
struct SendStoryRowStyle;
} // namespace style

struct SendStoryRowIcon {
	not_null<const style::icon*> icon;
	not_null<const style::color*> bg1;
	not_null<const style::color*> bg2;
};

struct SendStoryRowContent {
	PeerData *peer = nullptr;
	std::optional<SendStoryRowIcon> icon;
	QString title;
	QString subtitle;
	bool subtitleLink = false;
	bool radio = false;
	bool chevron = false;
	bool divider = false;
	const style::SendStoryRowStyle *style = nullptr;
};

class SendStoryRow final : public Ui::RippleButton {
public:
	SendStoryRow(QWidget *parent, SendStoryRowContent content);
	~SendStoryRow();

	void setChecked(bool checked, anim::type animated = anim::type::normal);
	[[nodiscard]] bool checked() const;
	void setSubtitle(const QString &subtitle);
	void setPeer(not_null<PeerData*> peer, const QString &subtitle);
	void setDivider(bool divider);

	[[nodiscard]] QString subtitle() const;
	[[nodiscard]] bool hasRadio() const;

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	[[nodiscard]] int rowHeight() const;
	[[nodiscard]] int iconLeft() const;
	[[nodiscard]] int textLeft() const;
	void paintIcon(QPainter &p);
	void paintSubtitleArrow(QPainter &p, int left, int top);
	void paintChevron(QPainter &p);

	SendStoryRowContent _content;
	const style::SendStoryRowStyle &_st;
	std::unique_ptr<Ui::RadioView> _radio;
	Ui::PeerUserpicView _userpic;
	Ui::Text::String _title;
	Ui::Text::String _subtitle;

};
