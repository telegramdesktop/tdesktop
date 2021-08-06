/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/chat/attach/attach_abstract_single_file_preview.h"

namespace Ui {

struct PreparedFile;

class SingleFilePreview final : public AbstractSingleFilePreview {
public:
	SingleFilePreview(
		QWidget *parent,
		const PreparedFile &file,
		AttachControls::Type type = AttachControls::Type::Full);

private:
	void preparePreview(const PreparedFile &file);

};

} // namespace Ui
