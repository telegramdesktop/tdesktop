/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"

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
