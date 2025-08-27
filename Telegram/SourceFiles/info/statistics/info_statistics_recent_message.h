/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

class HistoryItem;

namespace Data {
class DocumentMedia;
class PhotoMedia;
class Story;
} // namespace Data

namespace Ui {
class SpoilerAnimation;
} // namespace Ui

namespace Info::Statistics {

struct SavedState;

class MessagePreview final : public Ui::RpWidget {
public:
	MessagePreview(
		not_null<Ui::RpWidget*> parent,
		not_null<HistoryItem*> item,
		QImage cachedPreview);
	MessagePreview(
		not_null<Ui::RpWidget*> parent,
		not_null<Data::Story*> story,
		QImage cachedPreview);

	void setInfo(int views, int shares, int reactions);
	void saveState(SavedState &state) const;

protected:
	void paintEvent(QPaintEvent *e) override;

	int resizeGetHeight(int newWidth) override;

private:
	void processPreview();

	FullMsgId _messageId;
	FullStoryId _storyId;
	Ui::Text::String _text;
	Ui::Text::String _date;
	Ui::Text::String _views;
	Ui::Text::String _shares;
	Ui::Text::String _reactions;

	int _viewsWidth = 0;
	int _sharesWidth = 0;
	int _reactionsWidth = 0;

	QImage _cornerCache;
	QImage _preview;

	std::shared_ptr<Data::PhotoMedia> _photoMedia;
	std::shared_ptr<Data::DocumentMedia> _documentMedia;
	std::unique_ptr<Ui::SpoilerAnimation> _spoiler;

	rpl::lifetime _lifetimeDownload;

};

} // namespace Info::Statistics
