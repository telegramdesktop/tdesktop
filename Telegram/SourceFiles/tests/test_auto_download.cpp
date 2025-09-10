/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tests/test_main.h"

#include "data/data_auto_download.h"
#include "logs.h"

using namespace Data::AutoDownload;

namespace Test {

void test_auto_download() {
	// Test that audio auto-download settings are properly respected
	auto fullData = Full::FullDisabled();
	
	// Enable audio auto-download for all sources
	fullData.setBytesLimit(Source::User, Type::VoiceMessage, 1024 * 1024); // 1MB
	fullData.setBytesLimit(Source::User, Type::Music, 10 * 1024 * 1024); // 10MB
	fullData.setBytesLimit(Source::Group, Type::VoiceMessage, 1024 * 1024);
	fullData.setBytesLimit(Source::Group, Type::Music, 10 * 1024 * 1024);
	fullData.setBytesLimit(Source::Channel, Type::VoiceMessage, 1024 * 1024);
	fullData.setBytesLimit(Source::Channel, Type::Music, 10 * 1024 * 1024);
	
	// Test that user settings are respected for voice messages
	auto shouldDownloadSmallVoice = fullData.shouldDownload(Source::User, Type::VoiceMessage, 512 * 1024); // 512KB
	auto shouldDownloadLargeVoice = fullData.shouldDownload(Source::User, Type::VoiceMessage, 2 * 1024 * 1024); // 2MB
	
	// Test that user settings are respected for music
	auto shouldDownloadSmallMusic = fullData.shouldDownload(Source::User, Type::Music, 5 * 1024 * 1024); // 5MB
	auto shouldDownloadLargeMusic = fullData.shouldDownload(Source::User, Type::Music, 20 * 1024 * 1024); // 20MB
	
	// Log test results
	LOG(("Auto-download test: Small voice should download: %1").arg(shouldDownloadSmallVoice ? "true" : "false"));
	LOG(("Auto-download test: Large voice should not download: %1").arg(shouldDownloadLargeVoice ? "true" : "false"));
	LOG(("Auto-download test: Small music should download: %1").arg(shouldDownloadSmallMusic ? "true" : "false"));
	LOG(("Auto-download test: Large music should not download: %1").arg(shouldDownloadLargeMusic ? "true" : "false"));
	
	// Validate results
	if (shouldDownloadSmallVoice && !shouldDownloadLargeVoice && 
		shouldDownloadSmallMusic && !shouldDownloadLargeMusic) {
		LOG(("Audio auto-download test: PASSED - User settings are properly respected"));
	} else {
		LOG(("Audio auto-download test: FAILED - User settings not respected"));
	}
}

} // namespace Test