/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/layers/box_content.h"

namespace Ui {
template <typename Enum>
class RadioenumGroup;
template <typename Enum>
class Radioenum;
class LinkButton;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

class DownloadPathBox : public Ui::BoxContent {
public:
	DownloadPathBox(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

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
	Directory typeFromPath(const QString &path);

	void save();
	void updateControlsVisibility();
	void setPathText(const QString &text);
	void editPath();

	const not_null<Window::SessionController*> _controller;
	QString _path;
	QByteArray _pathBookmark;

	std::shared_ptr<Ui::RadioenumGroup<Directory>> _group;
	object_ptr<Ui::Radioenum<Directory>> _default;
	object_ptr<Ui::Radioenum<Directory>> _temp;
	object_ptr<Ui::Radioenum<Directory>> _dir;
	object_ptr<Ui::LinkButton> _pathLink;

};
