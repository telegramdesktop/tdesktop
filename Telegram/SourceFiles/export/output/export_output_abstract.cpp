/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export/output/export_output_abstract.h"

#include "export/output/export_output_text.h"
#include "export/output/export_output_json.h"

namespace Export {
namespace Output {

std::unique_ptr<AbstractWriter> CreateWriter(Format format) {
	return std::make_unique<JsonWriter>();
}

} // namespace Output
} // namespace Export
