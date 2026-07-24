/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/platform_text_recognition.h"

#include "base/platform/mac/base_utilities_mac.h"

#import <Foundation/Foundation.h>
#import <Vision/Vision.h>
#import <CoreImage/CoreImage.h>

namespace Platform {
namespace TextRecognition {

bool IsAvailable() {
	if (@available(macOS 10.15, *)) {
		return true;
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
						const auto convert = [&](CGRect box) {
							const auto x = box.origin.x * imageSize.width;
							const auto y = (1.0 - box.origin.y
								- box.size.height) * imageSize.height;
							const auto width = box.size.width
								* imageSize.width;
							const auto height = box.size.height
								* imageSize.height;
							return QRect(
								style::ConvertScale(x),
								style::ConvertScale(y),
								style::ConvertScale(width),
								style::ConvertScale(height));
						};
						auto glyphs = std::vector<QRect>();
						if (@available(macOS 11.0, *)) {
							const auto length = NSInteger(text.length);
							glyphs.reserve(length);
							for (auto i = NSInteger(); i < length; ++i) {
								NSError *rangeError = nil;
								auto *box = [recognizedText
									boundingBoxForRange:NSMakeRange(i, 1)
									error:&rangeError];
								glyphs.push_back((box && !rangeError)
									? convert(box.boundingBox)
									: QRect());
							}
						}
						result.items.push_back({
							NS2QString(text),
							convert(obs.boundingBox),
							std::move(glyphs)
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