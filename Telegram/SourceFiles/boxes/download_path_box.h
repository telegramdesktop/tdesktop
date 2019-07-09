/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"
#include "base/observer.h"

namespace Ui {
template <typename Enum>
class RadioenumGroup;
template <typename Enum>
class Radioenum;
class LinkButton;
} // namespace Ui

class DownloadPathBox : public BoxContent {
public:
	DownloadPathBox(QWidget *parent);

protected:
	void prepare() override;

	void resizeEvent(QResizeEvent *e) override;

private:
	enum class Directory {
		Downloads,
		Temp,
		Custom,
	};
	void radioChanged(Directory value);
	Directory typeFromPath(const QString &path) {
		if (path.isEmpty()) {
			return Directory::Downloads;
		} else if (path == qsl("tmp")) {
			return Directory::Temp;
		}
		return Directory::Custom;
	}

	void save();
	void updateControlsVisibility();
	void setPathText(const QString &text);
	void editPath();

	QString _path;
	QByteArray _pathBookmark;

	std::shared_ptr<Ui::RadioenumGroup<Directory>> _group;
	object_ptr<Ui::Radioenum<Directory>> _default;
	object_ptr<Ui::Radioenum<Directory>> _temp;
	object_ptr<Ui::Radioenum<Directory>> _dir;
	object_ptr<Ui::LinkButton> _pathLink;

};
