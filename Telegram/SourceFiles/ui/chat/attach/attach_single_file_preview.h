/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "base/object_ptr.h"

namespace Ui {

struct PreparedFile;
class IconButton;

class SingleFilePreview final : public RpWidget {
public:
	SingleFilePreview(
		QWidget *parent,
		const PreparedFile &file);
	~SingleFilePreview();

	[[nodiscard]] rpl::producer<> deleteRequests() const;
	[[nodiscard]] rpl::producer<> editRequests() const;

private:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

	void preparePreview(const PreparedFile &file);
	void prepareThumb(const QImage &preview);

	QPixmap _fileThumb;
	QString _name;
	QString _statusText;
	int _nameWidth = 0;
	int _statusWidth = 0;
	bool _fileIsAudio = false;
	bool _fileIsImage = false;

	object_ptr<IconButton> _editMedia = { nullptr };
	object_ptr<IconButton> _deleteMedia = { nullptr };

};

} // namespace Ui
