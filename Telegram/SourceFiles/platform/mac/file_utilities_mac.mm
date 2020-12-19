/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/mac/file_utilities_mac.h"

#include "base/platform/mac/base_utilities_mac.h"
#include "lang/lang_keys.h"
#include "base/qt_adapters.h"
#include "styles/style_window.h"

#include <QtWidgets/QApplication>
#include <QtGui/QScreen>

#include <Cocoa/Cocoa.h>
#include <CoreFoundation/CFURL.h>

namespace {

using namespace Platform;

QString strNeedToReload() {
	const uint32 letters[] = { 0xAD92C02B, 0xA2217C97, 0x5E55F4F5, 0x2207DAAC, 0xD18BA536, 0x03E41869, 0xB96D2BFD, 0x810C7284, 0xE412099E, 0x5AAD0837, 0xE6637AEE, 0x8E5E2FF5, 0xE3BDA123, 0x94A5CE38, 0x4A42F7D1, 0xCE4677DC, 0x40A81701, 0x9C5B38CD, 0x61801E1A, 0x6FF16179 };
	return MakeFromLetters(letters);
}

QString strNeedToRefresh1() {
	const uint32 letters[] = { 0xEDDFCD66, 0x434DF1FB, 0x820B76AB, 0x48CE7965, 0x3609C0BA, 0xFC9A990C, 0x3EDD1C51, 0xE2BDA036, 0x7140CEE9, 0x65DB414D, 0x88592EC3, 0x2CB2613A };
	return MakeFromLetters(letters);
}

QString strNeedToRefresh2() {
	const uint32 letters[] = { 0x8AE4915D, 0x7159D7EF, 0x79C74167, 0x29B7611C, 0x0E6B9ADD, 0x0D93610F, 0xEBEAFE7A, 0x5BD17540, 0x121EF3B7, 0x61B02E26, 0x2174AAEE, 0x61AD3325 };
	return MakeFromLetters(letters);
}

} // namespace

@interface OpenWithApp : NSObject {
	NSString *fullname;
	NSURL *app;
	NSImage *icon;

}

@property (nonatomic, retain) NSString *fullname;
@property (nonatomic, retain) NSURL *app;
@property (nonatomic, retain) NSImage *icon;

@end // @interface OpenWithApp

@implementation OpenWithApp

@synthesize fullname, app, icon;

- (void) dealloc {
	[fullname release];
	[app release];
	[icon release];
	[super dealloc];
}

@end // @implementation OpenWithApp

@interface OpenFileWithInterface : NSObject {
}

- (id) init:(NSString *)file;
- (BOOL) popupAtX:(int)x andY:(int)y;
- (void) itemChosen:(id)sender;
- (void) dealloc;

@end // @interface OpenFileWithInterface

@implementation OpenFileWithInterface {
	NSString *toOpen;

	NSURL *defUrl;
	NSString *defBundle, *defName, *defVersion;
	NSImage *defIcon;

	NSMutableArray *apps;

	NSMenu *menu;

}

- (void) fillAppByUrl:(NSURL*)url bundle:(NSString**)bundle name:(NSString**)name version:(NSString**)version icon:(NSImage**)icon {
	NSBundle *b = [NSBundle bundleWithURL:url];
	if (b) {
		NSString *path = [url path];
		*name = [[NSFileManager defaultManager] displayNameAtPath: path];
		if (!*name) *name = (NSString*)[b objectForInfoDictionaryKey:@"CFBundleDisplayName"];
		if (!*name) *name = (NSString*)[b objectForInfoDictionaryKey:@"CFBundleName"];
		if (*name) {
			*bundle = [b bundleIdentifier];
			if (bundle) {
				*version = (NSString*)[b objectForInfoDictionaryKey:@"CFBundleShortVersionString"];
				*icon = [[NSWorkspace sharedWorkspace] iconForFile: path];
				if (*icon && [*icon isValid]) [*icon setSize: CGSizeMake(16., 16.)];
				return;
			}
		}
	}
	*bundle = *name = *version = nil;
	*icon = nil;
}

- (id) init:(NSString*)file {
	toOpen = [file retain];
	if (self = [super init]) {
		NSURL *url = [NSURL fileURLWithPath:file];
		defUrl = [[NSWorkspace sharedWorkspace] URLForApplicationToOpenURL:url];
		if (defUrl) {
			[self fillAppByUrl:defUrl bundle:&defBundle name:&defName version:&defVersion icon:&defIcon];
			if (!defBundle || !defName) {
				defUrl = nil;
			}
		}
		NSArray *appsList = (NSArray*)LSCopyApplicationURLsForURL(CFURLRef(url), kLSRolesAll);
		NSMutableDictionary *data = [NSMutableDictionary dictionaryWithCapacity:16];
		int fullcount = 0;
		for (id app in appsList) {
			if (fullcount > 15) break;

			NSString *bundle = nil, *name = nil, *version = nil;
			NSImage *icon = nil;
			[self fillAppByUrl:(NSURL*)app bundle:&bundle name:&name version:&version icon:&icon];
			if (bundle && name) {
				if ([bundle isEqualToString:defBundle] && [version isEqualToString:defVersion]) continue;
				NSString *key = [[NSArray arrayWithObjects:bundle, name, nil] componentsJoinedByString:@"|"];
				if (!version) version = @"";

				NSMutableDictionary *versions = (NSMutableDictionary*)[data objectForKey:key];
				if (!versions) {
					versions = [NSMutableDictionary dictionaryWithCapacity:2];
					[data setValue:versions forKey:key];
				}
				if (![versions objectForKey:version]) {
					[versions setValue:[NSArray arrayWithObjects:name, icon, app, nil] forKey:version];
					++fullcount;
				}
			}
		}
		if (fullcount || defUrl) {
			apps = [NSMutableArray arrayWithCapacity:fullcount];
			for (id key in data) {
				NSMutableDictionary *val = (NSMutableDictionary*)[data objectForKey:key];
				for (id ver in val) {
					NSArray *app = (NSArray*)[val objectForKey:ver];
					OpenWithApp *a = [[OpenWithApp alloc] init];
					NSString *fullname = (NSString*)[app objectAtIndex:0], *version = (NSString*)ver;
					BOOL showVersion = ([val count] > 1);
					if (!showVersion) {
						NSError *error = NULL;
						NSRegularExpression *regex = [NSRegularExpression regularExpressionWithPattern:@"^\\d+\\.\\d+\\.\\d+(\\.\\d+)?$" options:NSRegularExpressionCaseInsensitive error:&error];
						showVersion = ![regex numberOfMatchesInString:version options:NSMatchingWithoutAnchoringBounds range:{0,[version length]}];
					}
					if (showVersion) fullname = [[NSArray arrayWithObjects:fullname, @" (", version, @")", nil] componentsJoinedByString:@""];
					[a setFullname:fullname];
					[a setIcon:(NSImage*)[app objectAtIndex:1]];
					[a setApp:(NSURL*)[app objectAtIndex:2]];
					[apps addObject:a];
					[a release];
				}
			}
		}
		[apps sortUsingDescriptors:[NSArray arrayWithObject:[NSSortDescriptor sortDescriptorWithKey:@"fullname" ascending:YES]]];
		[appsList release];
		menu = nil;
	}
	return self;
}

- (BOOL) popupAtX:(int)x andY:(int)y {
	if (![apps count] && !defName) return NO;
	menu = [[NSMenu alloc] initWithTitle:@"Open With"];

	int index = 0;
	if (defName) {
		NSMenuItem *item = [menu insertItemWithTitle:[[NSArray arrayWithObjects:defName, @" (default)", nil] componentsJoinedByString:@""] action:@selector(itemChosen:) keyEquivalent:@"" atIndex:index++];
		if (defIcon) [item setImage:defIcon];
		[item setTarget:self];
		[menu insertItem:[NSMenuItem separatorItem] atIndex:index++];
	}
	if ([apps count]) {
		for (id a in apps) {
			OpenWithApp *app = (OpenWithApp*)a;
			NSMenuItem *item = [menu insertItemWithTitle:[a fullname] action:@selector(itemChosen:) keyEquivalent:@"" atIndex:index++];
			if ([app icon]) [item setImage:[app icon]];
			[item setTarget:self];
		}
		[menu insertItem:[NSMenuItem separatorItem] atIndex:index++];
	}
	NSMenuItem *item = [menu insertItemWithTitle:Q2NSString(tr::lng_mac_choose_program_menu(tr::now)) action:@selector(itemChosen:) keyEquivalent:@"" atIndex:index++];
	[item setTarget:self];

	[menu popUpMenuPositioningItem:nil atLocation:CGPointMake(x, y) inView:nil];

	return YES;
}

- (void) itemChosen:(id)sender {
	NSArray *items = [menu itemArray];
	NSURL *url = nil;
	for (int i = 0, l = [items count]; i < l; ++i) {
		if ([items objectAtIndex:i] == sender) {
			if (defName) i -= 2;
			if (i < 0) {
				url = defUrl;
			} else if (i < int([apps count])) {
				url = [(OpenWithApp*)[apps objectAtIndex:i] app];
			}
			break;
		}
	}
	if (url) {
		[[NSWorkspace sharedWorkspace] openFile:toOpen withApplication:[url path]];
	} else if (!Platform::File::UnsafeShowOpenWith(NS2QString(toOpen))) {
		Platform::File::UnsafeLaunch(NS2QString(toOpen));
	}
}

- (void) dealloc {
	[toOpen release];
	if (menu) [menu release];
	[super dealloc];
}

@end // @implementation OpenFileWithInterface

@interface NSURL(CompareUrls)

- (BOOL) isEquivalent:(NSURL *)aURL;

@end // @interface NSURL(CompareUrls)

@implementation NSURL(CompareUrls)

- (BOOL) isEquivalent:(NSURL *)aURL {
	if ([self isEqual:aURL]) return YES;
	if ([[self scheme] caseInsensitiveCompare:[aURL scheme]] != NSOrderedSame) return NO;
	if ([[self host] caseInsensitiveCompare:[aURL host]] != NSOrderedSame) return NO;
	if ([[self path] compare:[aURL path]] != NSOrderedSame) return NO;
	if ([[self port] compare:[aURL port]] != NSOrderedSame) return NO;
	if ([[self query] compare:[aURL query]] != NSOrderedSame) return NO;
	return YES;
}

@end // @implementation NSURL(CompareUrls)

@interface ChooseApplicationDelegate : NSObject<NSOpenSavePanelDelegate> {
}

- (id) init:(NSArray *)recommendedApps withPanel:(NSOpenPanel *)creator withSelector:(NSPopUpButton *)menu withGood:(NSTextField *)goodLabel withBad:(NSTextField *)badLabel withIcon:(NSImageView *)badIcon withAccessory:(NSView *)acc;
- (BOOL) panel:(id)sender shouldEnableURL:(NSURL *)url;
- (void) panelSelectionDidChange:(id)sender;
- (void) menuDidClose;
- (void) dealloc;

@end // @interface ChooseApplicationDelegate

@implementation ChooseApplicationDelegate {
	BOOL onlyRecommended;
	NSArray *apps;
	NSOpenPanel *panel;
	NSPopUpButton *selector;
	NSTextField *good, *bad;
	NSImageView *icon;
	NSString *recom;
	NSView *accessory;

}

- (id) init:(NSArray *)recommendedApps withPanel:(NSOpenPanel *)creator withSelector:(NSPopUpButton *)menu withGood:(NSTextField *)goodLabel withBad:(NSTextField *)badLabel withIcon:(NSImageView *)badIcon withAccessory:(NSView *)acc {
	if (self = [super init]) {
		onlyRecommended = YES;
		recom = [Q2NSString(tr::lng_mac_recommended_apps(tr::now)) copy];
		apps = recommendedApps;
		panel = creator;
		selector = menu;
		good = goodLabel;
		bad = badLabel;
		icon = badIcon;
		accessory = acc;
		[selector setAction:@selector(menuDidClose)];
	}
	return self;
}

- (BOOL) isRecommended:(NSURL *)url {
	if (apps) {
		for (id app in apps) {
			if ([(NSURL*)app isEquivalent:url]) {
				return YES;
			}
		}
	}
	return NO;
}

- (BOOL) panel:(id)sender shouldEnableURL:(NSURL *)url {
	NSNumber *isDirectory;
	if ([url getResourceValue:&isDirectory forKey:NSURLIsDirectoryKey error:nil] && isDirectory != nil && [isDirectory boolValue]) {
		if (onlyRecommended) {
			CFStringRef ext = CFURLCopyPathExtension((CFURLRef)url);
			NSNumber *isPackage;
			if ([url getResourceValue:&isPackage forKey:NSURLIsPackageKey error:nil] && isPackage != nil && [isPackage boolValue]) {
				return [self isRecommended:url];
			}
		}
		return YES;
	}
	return NO;
}

- (void) panelSelectionDidChange:(id)sender {
	NSArray *urls = [panel URLs];
	if ([urls count]) {
		if ([self isRecommended:[urls firstObject]]) {
			[bad removeFromSuperview];
			[icon removeFromSuperview];
			[accessory addSubview:good];
		} else {
			[good removeFromSuperview];
			[accessory addSubview:bad];
			[accessory addSubview:icon];
		}
	} else {
		[good removeFromSuperview];
		[bad removeFromSuperview];
		[icon removeFromSuperview];
	}
}

- (void) menuDidClose {
	onlyRecommended = [[[selector selectedItem] title] isEqualToString:recom];
	[self refreshPanelTable];
}

- (BOOL) refreshDataInViews: (NSArray*)subviews {
	for (id view in subviews) {
		NSString *cls = [view className];
		if ([cls isEqualToString:Q2NSString(strNeedToReload())]) {
			[view reloadData];
		} else if ([cls isEqualToString:Q2NSString(strNeedToRefresh1())] || [cls isEqualToString:Q2NSString(strNeedToRefresh2())]) {
			[view reloadData];
			return YES;
		} else {
			NSArray *next = [view subviews];
			if ([next count] && [self refreshDataInViews:next]) {
				return YES;
			}
		}
	}
	return NO;
}


- (void) refreshPanelTable {
	@autoreleasepool {

	[self refreshDataInViews:[[panel contentView] subviews]];
	[panel validateVisibleColumns];

	}
}

- (void) dealloc {
	if (apps) {
		[apps release];
		[recom release];
	}
	[super dealloc];
}

@end // @implementation ChooseApplicationDelegate

namespace Platform {
namespace File {

QString UrlToLocal(const QUrl &url) {
	auto result = url.toLocalFile();
	if (result.startsWith(qsl("/.file/id="))) {
		NSString *nsurl = [[[NSURL URLWithString: [NSString stringWithUTF8String: (qsl("file://") + result).toUtf8().constData()]] filePathURL] path];
		if (!nsurl) return QString();

		return NS2QString(nsurl);
	}
	return result;
}

bool UnsafeShowOpenWithDropdown(const QString &filepath, QPoint menuPosition) {
	@autoreleasepool {

	NSString *file = Q2NSString(filepath);
	@try {
		OpenFileWithInterface *menu = [[[OpenFileWithInterface alloc] init:file] autorelease];
		const auto screen = base::QScreenNearestTo(menuPosition);
		if (!screen) {
			return false;
		}
		const auto r = screen->geometry();
		auto x = menuPosition.x();
		auto y = r.y() + r.height() - menuPosition.y();
		return !![menu popupAtX:x andY:y];
	}
	@catch (NSException *exception) {
	}
	@finally {
	}

	}
	return false;
}

bool UnsafeShowOpenWith(const QString &filepath) {
	@autoreleasepool {

	NSString *file = Q2NSString(filepath);
	@try {
		NSURL *url = [NSURL fileURLWithPath:file];
		NSString *ext = [url pathExtension];
		NSArray *names = [url pathComponents];
		NSString *name = [names count] ? [names lastObject] : @"";
		NSArray *apps = (NSArray*)LSCopyApplicationURLsForURL(CFURLRef(url), kLSRolesAll);

		NSOpenPanel *openPanel = [NSOpenPanel openPanel];

		NSRect fullRect = { { 0., 0. }, { st::macAccessoryWidth, st::macAccessoryHeight } };
		NSView *accessory = [[NSView alloc] initWithFrame:fullRect];

		[accessory setAutoresizesSubviews:YES];

		NSPopUpButton *selector = [[NSPopUpButton alloc] init];
		[accessory addSubview:selector];
		[selector addItemWithTitle:Q2NSString(tr::lng_mac_recommended_apps(tr::now))];
		[selector addItemWithTitle:Q2NSString(tr::lng_mac_all_apps(tr::now))];
		[selector sizeToFit];

		NSTextField *enableLabel = [[NSTextField alloc] init];
		[accessory addSubview:enableLabel];
		[enableLabel setStringValue:Q2NSString(tr::lng_mac_enable_filter(tr::now))];
		[enableLabel setFont:[selector font]];
		[enableLabel setBezeled:NO];
		[enableLabel setDrawsBackground:NO];
		[enableLabel setEditable:NO];
		[enableLabel setSelectable:NO];
		[enableLabel sizeToFit];

		NSRect selectorFrame = [selector frame], enableFrame = [enableLabel frame];
		enableFrame.size.width += st::macEnableFilterAdd;
		enableFrame.origin.x = (fullRect.size.width - selectorFrame.size.width - enableFrame.size.width) / 2.;
		selectorFrame.origin.x = (fullRect.size.width - selectorFrame.size.width + enableFrame.size.width) / 2.;
		enableFrame.origin.y = fullRect.size.height - selectorFrame.size.height - st::macEnableFilterTop + (selectorFrame.size.height - enableFrame.size.height) / 2.;
		selectorFrame.origin.y = fullRect.size.height - selectorFrame.size.height - st::macSelectorTop;
		[enableLabel setFrame:enableFrame];
		[enableLabel setAutoresizingMask:NSViewMinXMargin|NSViewMaxXMargin];
		[selector setFrame:selectorFrame];
		[selector setAutoresizingMask:NSViewMinXMargin|NSViewMaxXMargin];

		NSButton *button = [[NSButton alloc] init];
		[accessory addSubview:button];
		[button setButtonType:NSSwitchButton];
		[button setFont:[selector font]];
		[button setTitle:Q2NSString(tr::lng_mac_always_open_with(tr::now))];
		[button sizeToFit];
		NSRect alwaysRect = [button frame];
		alwaysRect.origin.x = (fullRect.size.width - alwaysRect.size.width) / 2;
		alwaysRect.origin.y = selectorFrame.origin.y - alwaysRect.size.height - st::macAlwaysThisAppTop;
		[button setFrame:alwaysRect];
		[button setAutoresizingMask:NSViewMinXMargin|NSViewMaxXMargin];
#ifdef OS_MAC_STORE
		[button setHidden:YES];
#endif // OS_MAC_STORE
		NSTextField *goodLabel = [[NSTextField alloc] init];
		[goodLabel setStringValue:Q2NSString(tr::lng_mac_this_app_can_open(tr::now, lt_file, NS2QString(name)))];
		[goodLabel setFont:[NSFont systemFontOfSize:[NSFont smallSystemFontSize]]];
		[goodLabel setBezeled:NO];
		[goodLabel setDrawsBackground:NO];
		[goodLabel setEditable:NO];
		[goodLabel setSelectable:NO];
		[goodLabel sizeToFit];
		NSRect goodFrame = [goodLabel frame];
		goodFrame.origin.x = (fullRect.size.width - goodFrame.size.width) / 2.;
		goodFrame.origin.y = alwaysRect.origin.y - goodFrame.size.height - st::macAppHintTop;
		[goodLabel setFrame:goodFrame];

		NSTextField *badLabel = [[NSTextField alloc] init];
		[badLabel setStringValue:Q2NSString(tr::lng_mac_not_known_app(tr::now, lt_file, NS2QString(name)))];
		[badLabel setFont:[goodLabel font]];
		[badLabel setBezeled:NO];
		[badLabel setDrawsBackground:NO];
		[badLabel setEditable:NO];
		[badLabel setSelectable:NO];
		[badLabel sizeToFit];
		NSImageView *badIcon = [[NSImageView alloc] init];
		NSImage *badImage = [NSImage imageNamed:NSImageNameCaution];
		[badIcon setImage:badImage];
		[badIcon setFrame:NSMakeRect(0, 0, st::macCautionIconSize, st::macCautionIconSize)];

		NSRect badFrame = [badLabel frame], badIconFrame = [badIcon frame];
		badFrame.origin.x = (fullRect.size.width - badFrame.size.width + badIconFrame.size.width) / 2.;
		badIconFrame.origin.x = (fullRect.size.width - badFrame.size.width - badIconFrame.size.width) / 2.;
		badFrame.origin.y = alwaysRect.origin.y - badFrame.size.height - st::macAppHintTop;
		badIconFrame.origin.y = badFrame.origin.y;
		[badLabel setFrame:badFrame];
		[badIcon setFrame:badIconFrame];

		[openPanel setAccessoryView:accessory];

		ChooseApplicationDelegate *delegate = [[ChooseApplicationDelegate alloc] init:apps withPanel:openPanel withSelector:selector withGood:goodLabel withBad:badLabel withIcon:badIcon withAccessory:accessory];
		[openPanel setDelegate:delegate];

		[openPanel setCanChooseDirectories:NO];
		[openPanel setCanChooseFiles:YES];
		[openPanel setAllowsMultipleSelection:NO];
		[openPanel setResolvesAliases:YES];
		[openPanel setTitle:Q2NSString(tr::lng_mac_choose_app(tr::now))];
		[openPanel setMessage:Q2NSString(tr::lng_mac_choose_text(tr::now, lt_file, NS2QString(name)))];

		NSArray *appsPaths = [[NSFileManager defaultManager] URLsForDirectory:NSApplicationDirectory inDomains:NSLocalDomainMask];
		if ([appsPaths count]) [openPanel setDirectoryURL:[appsPaths firstObject]];
		[openPanel beginWithCompletionHandler:^(NSInteger result){
			if (result == NSFileHandlingPanelOKButton) {
				if ([[openPanel URLs] count] > 0) {
					NSURL *app = [[openPanel URLs] objectAtIndex:0];
					NSString *path = [app path];
					if ([button state] == NSOnState) {
						NSArray *UTIs = (NSArray *)UTTypeCreateAllIdentifiersForTag(kUTTagClassFilenameExtension,
																					(CFStringRef)ext,
																					nil);
						for (NSString *UTI in UTIs) {
							OSStatus result = LSSetDefaultRoleHandlerForContentType((CFStringRef)UTI,
																					kLSRolesAll,
																					(CFStringRef)[[NSBundle bundleWithPath:path] bundleIdentifier]);
							DEBUG_LOG(("App Info: set default handler for '%1' UTI result: %2").arg(NS2QString(UTI)).arg(result));
						}

						[UTIs release];
					}
					[[NSWorkspace sharedWorkspace] openFile:file withApplication:[app path]];
				}
			}
			[selector release];
			[button release];
			[enableLabel release];
			[goodLabel release];
			[badLabel release];
			[badIcon release];
			[accessory release];
			[delegate release];
		}];
	}
	@catch (NSException *exception) {
		[[NSWorkspace sharedWorkspace] openFile:file];
	}
	@finally {
	}

	}

	return YES;
}

void UnsafeLaunch(const QString &filepath) {
	@autoreleasepool {

	NSString *file = Q2NSString(filepath);
	if ([[NSWorkspace sharedWorkspace] openFile:file] == NO) {
		UnsafeShowOpenWith(filepath);
	}

	}
}

} // namespace File
} // namespace Platform
