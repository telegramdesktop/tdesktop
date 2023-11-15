/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/layers/box_content.h"

namespace Main {
class Session;
} // namespace Main

namespace Data {
namespace AutoDownload {
enum class Source;
} // namespace AutoDownload
} // namespace Data

class AutoDownloadBox : public Ui::BoxContent {
public:
	AutoDownloadBox(
		QWidget*,
		not_null<Main::Session*> session,
		Data::AutoDownload::Source source);

protected:
	void prepare() override;

private:
	void setupContent();

	const not_null<Main::Session*> _session;

	Data::AutoDownload::Source _source;

};
