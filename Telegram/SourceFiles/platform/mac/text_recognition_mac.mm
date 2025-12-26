/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/platform_text_recognition.h"

#include "base/platform/mac/base_utilities_mac.h"
#include "base/options.h"

#import <Foundation/Foundation.h>
#import <Vision/Vision.h>
#import <CoreImage/CoreImage.h>

namespace Platform {
namespace TextRecognition {

namespace {

base::options::toggle TextRecognitionOption({
	.id = "text-recognition-mac",
	.name = "Text Recognition",
	.description = "Enable text recognition from images on macOS 10.15+.",
	.defaultValue = false,
	.scope = base::options::macos,
});

} // namespace

bool IsAvailable() {
	if (@available(macOS 10.15, *)) {
		return TextRecognitionOption.value();
	}
	return false;
}

Result RecognizeText(const QImage &image) {
	auto result = Result();

	if (!IsAvailable()) {
		return result;
	}

	@autoreleasepool {
		CGImageRef cgImage = image.toCGImage();
		if (!cgImage) {
			return result;
		}
		CIImage *image = [CIImage imageWithCGImage:cgImage];
		CFRelease(cgImage);

		if (!image) {
			return result;
		}

		if (@available(macOS 10.15, *)) {
			VNRecognizeTextRequest *request
				= [[VNRecognizeTextRequest alloc] init];
			request.recognitionLevel = VNRequestTextRecognitionLevelAccurate;

			VNImageRequestHandler *handler = [[VNImageRequestHandler alloc]
				initWithCIImage:image options:@{}];

			NSError *error = nil;
			const auto success
				= [handler performRequests:@[request] error:&error];

			if (success && !error) {
				const auto imageSize = image.extent.size;
				for (VNRecognizedTextObservation *obs in request.results) {
					VNRecognizedText *recognizedText = [obs
						topCandidates:1].firstObject;
					if (recognizedText) {
						const auto text = recognizedText.string;
						const auto boundingBox = obs.boundingBox;
						const auto x = boundingBox.origin.x * imageSize.width;
						const auto y = (1.0 - boundingBox.origin.y
							- boundingBox.size.height) * imageSize.height;
						const auto width = boundingBox.size.width
							* imageSize.width;
						const auto height = boundingBox.size.height
							* imageSize.height;
						result.items.push_back({
							NS2QString(text),
							QRect(x, y, width, height)
						});
					}
				}
				result.success = true;
			}

			[request release];
			[handler release];
		}
	}

	return result;
}

} // namespace TextRecognition
} // namespace Platform