#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include "insys/editor/editor_server.h"
#include "posix_shell.h"

using insys::shell::CommandLine;
using insys::shell::Log;
using insys::shell::ShellConfig;

namespace {

NSString* ns_string(const std::string& text) {
    NSString* value = [NSString stringWithUTF8String:text.c_str()];
    return value != nil ? value : @"";
}

NSString* json_literal(NSString* value) {
    NSData* data = [NSJSONSerialization dataWithJSONObject:@[value]
                                                    options:0
                                                      error:nil];
    if (data == nil)
        return @"\"\"";
    NSString* array = [[NSString alloc] initWithData:data
                                            encoding:NSUTF8StringEncoding];
    if (array.length < 2)
        return @"\"\"";
    return [array substringWithRange:NSMakeRange(1, array.length - 2)];
}

void show_error(NSString* message) {
    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = @"insys editor";
    alert.informativeText = message;
    [alert addButtonWithTitle:@"OK"];
    [alert runModal];
}

} // namespace

@interface InsysEditorDelegate : NSObject <NSApplicationDelegate,
                                          NSWindowDelegate,
                                          WKNavigationDelegate,
                                          WKUIDelegate>
@property(nonatomic, assign) insys::editor::EditorServer* server;
@property(nonatomic, assign) ShellConfig* config;
@property(nonatomic, assign) Log* log;
@property(nonatomic, strong) NSWindow* window;
@property(nonatomic, strong) WKWebView* webView;
@property(nonatomic, strong) NSString* origin;
@property(nonatomic, assign) BOOL allowClose;
@property(nonatomic, assign) BOOL closeCheckInFlight;
- (void)installMenu;
- (BOOL)createWindow;
- (void)openBrowserFromMenu:(id)sender;
- (void)openAssetFromPanel:(id)sender;
- (void)quitFromMenu:(id)sender;
- (void)scheduleSmoke;
@end

@implementation InsysEditorDelegate

- (void)installMenu {
    NSMenu* menuBar = [[NSMenu alloc] init];

    NSMenuItem* appItem = [[NSMenuItem alloc] init];
    [menuBar addItem:appItem];
    NSMenu* appMenu = [[NSMenu alloc] init];
    NSMenuItem* quit = [[NSMenuItem alloc]
        initWithTitle:@"Quit insys editor"
               action:@selector(quitFromMenu:)
        keyEquivalent:@"q"];
    quit.target = self;
    [appMenu addItem:quit];
    appItem.submenu = appMenu;

    NSMenuItem* editItem = [[NSMenuItem alloc] init];
    [menuBar addItem:editItem];
    NSMenu* editMenu = [[NSMenu alloc] initWithTitle:@"Edit"];
    NSArray* editActions = @[
        @[ @"Undo", @"undo:", @"z", @(NSEventModifierFlagCommand) ],
        @[ @"Redo", @"redo:", @"Z",
           @(NSEventModifierFlagCommand | NSEventModifierFlagShift) ],
        @[ @"Cut", @"cut:", @"x", @(NSEventModifierFlagCommand) ],
        @[ @"Copy", @"copy:", @"c", @(NSEventModifierFlagCommand) ],
        @[ @"Paste", @"paste:", @"v", @(NSEventModifierFlagCommand) ],
        @[ @"Select All", @"selectAll:", @"a",
           @(NSEventModifierFlagCommand) ],
    ];
    for (NSArray* entry in editActions) {
        NSMenuItem* item = [[NSMenuItem alloc]
            initWithTitle:entry[0]
                   action:NSSelectorFromString(entry[1])
            keyEquivalent:entry[2]];
        item.keyEquivalentModifierMask = [entry[3] unsignedIntegerValue];
        item.target = nil;
        [editMenu addItem:item];
    }
    editItem.submenu = editMenu;

    NSMenuItem* fileItem = [[NSMenuItem alloc] init];
    [menuBar addItem:fileItem];
    NSMenu* fileMenu = [[NSMenu alloc] initWithTitle:@"File"];
    NSMenuItem* open = [[NSMenuItem alloc]
        initWithTitle:@"Open Asset..."
               action:@selector(openAssetFromPanel:)
        keyEquivalent:@"o"];
    open.target = self;
    [fileMenu addItem:open];
    NSMenuItem* browser = [[NSMenuItem alloc]
        initWithTitle:@"Open in Browser"
               action:@selector(openBrowserFromMenu:)
        keyEquivalent:@"b"];
    browser.target = self;
    browser.keyEquivalentModifierMask =
        NSEventModifierFlagCommand | NSEventModifierFlagShift;
    [fileMenu addItem:browser];
    fileItem.submenu = fileMenu;

    [NSApp setMainMenu:menuBar];
}

- (BOOL)createWindow {
    const std::string url = insys::shell::make_url(*self.server);
    self.origin = ns_string(insys::shell::make_origin(*self.server));
    insys::shell::write_url_file(self.config->urlFile, url);

    NSRect frame = NSMakeRect(0, 0, 1280, 820);
    NSUInteger style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                       NSWindowStyleMaskMiniaturizable |
                       NSWindowStyleMaskResizable;
    self.window = [[NSWindow alloc] initWithContentRect:frame
                                              styleMask:style
                                                backing:NSBackingStoreBuffered
                                                  defer:NO];
    self.window.title = @"insys editor";
    self.window.delegate = self;
    [self.window center];

    WKWebViewConfiguration* configuration =
        [[WKWebViewConfiguration alloc] init];
    self.webView = [[WKWebView alloc]
        initWithFrame:self.window.contentView.bounds
        configuration:configuration];
    self.webView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    self.webView.navigationDelegate = self;
    self.webView.UIDelegate = self;
    [self.window.contentView addSubview:self.webView];
    [self.window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];

    if (self.config->devtools &&
        [self.webView respondsToSelector:@selector(setInspectable:)]) {
        [self.webView setValue:@YES forKey:@"inspectable"];
    }
    if (self.config->remoteDebuggingPort != 0) {
        self.log->line(
            "shell: --remote-debugging-port is not supported by WKWebView");
    }
    self.log->line("shell: loading " +
                   insys::shell::redact_url(url));
    [self.webView
        loadRequest:[NSURLRequest requestWithURL:
                                      [NSURL URLWithString:ns_string(url)]]];
    if (self.config->openBrowser)
        [self openBrowserFromMenu:nil];
    return YES;
}

- (void)openBrowserFromMenu:(id)sender {
    (void)sender;
    std::string error;
    if (!insys::shell::open_external_browser(
            self.config->browserPath,
            insys::shell::make_url(*self.server), error)) {
        show_error(ns_string("Could not open the external browser: " + error));
        self.log->line("shell: external browser failed: " + error);
    }
}

- (void)openAssetFromPanel:(id)sender {
    (void)sender;
    if (self.webView == nil) {
        show_error(@"Open Asset is available in the app window.");
        return;
    }
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = NO;
    panel.allowsMultipleSelection = NO;
    panel.allowedFileTypes = @[ @"json", @"insysb" ];
    [panel beginSheetModalForWindow:self.window
                  completionHandler:^(NSModalResponse result) {
                    if (result != NSModalResponseOK || panel.URL == nil)
                        return;
                    NSString* path = panel.URL.path;
                    NSString* script = [NSString
                        stringWithFormat:
                            @"window.__insysEditorHost && "
                             "window.__insysEditorHost.openAsset(%@)",
                            json_literal(path)];
                    [self.webView evaluateJavaScript:script
                                  completionHandler:nil];
                    self.log->line("shell: selected asset " +
                                   std::string(path.UTF8String));
                  }];
}

- (void)quitFromMenu:(id)sender {
    (void)sender;
    if (self.window != nil) {
        [self.window performClose:nil];
    } else {
        [NSApp terminate:nil];
    }
}

- (void)scheduleSmoke {
    if (self.config->smokeMs == 0)
        return;
    const auto milliseconds = self.config->smokeMs;
    dispatch_after(
        dispatch_time(DISPATCH_TIME_NOW,
                      (int64_t)milliseconds * NSEC_PER_MSEC),
        dispatch_get_main_queue(), ^{
          self.allowClose = YES;
          if (self.window != nil)
              [self.window close];
          [NSApp terminate:nil];
        });
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:
    (NSApplication*)sender {
    (void)sender;
    return YES;
}

- (BOOL)windowShouldClose:(NSWindow*)sender {
    (void)sender;
    if (self.allowClose || self.webView == nil)
        return YES;
    if (self.closeCheckInFlight)
        return NO;
    self.closeCheckInFlight = YES;
    [self evaluateEditorState:^(NSDictionary* state) {
      self.closeCheckInFlight = NO;
      if (state == nil) {
          self.allowClose = YES;
          [self.window close];
          return;
      }
      const BOOL dirty = [state[@"dirty"] boolValue];
      const BOOL rawDirty = [state[@"rawDirty"] boolValue];
      const BOOL autoSave = [state[@"autoSave"] boolValue];
      if (!dirty && !rawDirty) {
          self.allowClose = YES;
          [self.window close];
          return;
      }
      if (autoSave && dirty && !rawDirty) {
          [self saveAndClose];
          return;
      }
      [self promptCloseWithDirty:dirty rawDirty:rawDirty];
    }];
    return NO;
}

- (void)evaluateEditorState:(void (^)(NSDictionary*))completion {
    NSString* script =
        @"JSON.stringify(window.__insysEditorHost ? "
         "window.__insysEditorHost.state() : null)";
    [self.webView evaluateJavaScript:script
                  completionHandler:^(id result, NSError* error) {
                    if (error != nil ||
                        ![result isKindOfClass:[NSString class]]) {
                        completion(nil);
                        return;
                    }
                    NSData* data = [result
                        dataUsingEncoding:NSUTF8StringEncoding];
                    id value = [NSJSONSerialization JSONObjectWithData:data
                                                               options:0
                                                                 error:nil];
                    completion([value isKindOfClass:[NSDictionary class]]
                                   ? value
                                   : nil);
                  }];
}

- (void)saveAndClose {
    NSString* script =
        @"window.__insysEditorHost && window.__insysEditorHost.save()";
    [self.webView evaluateJavaScript:script
                  completionHandler:^(id result, NSError* error) {
                    (void)result;
                    if (error != nil) {
                        show_error(
                            @"Could not save the asset before closing.");
                        return;
                    }
                    [self pollSaveAndClose:[NSDate date]];
                  }];
}

- (void)pollSaveAndClose:(NSDate*)started {
    if ([[NSDate date] timeIntervalSinceDate:started] > 5.0) {
        show_error(@"Saving did not finish within five seconds.");
        return;
    }
    [self evaluateEditorState:^(NSDictionary* state) {
      if (state != nil && ![state[@"dirty"] boolValue] &&
              ![state[@"rawDirty"] boolValue]) {
          self.allowClose = YES;
          [self.window close];
          return;
      }
      dispatch_after(
          dispatch_time(DISPATCH_TIME_NOW, 100 * NSEC_PER_MSEC),
          dispatch_get_main_queue(), ^{
            [self pollSaveAndClose:started];
          });
    }];
}

- (void)promptCloseWithDirty:(BOOL)dirty rawDirty:(BOOL)rawDirty {
    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = @"Unsaved changes";
    alert.informativeText =
        rawDirty
            ? @"The Raw JSON changes have not been applied. Discard them?"
            : @"Save changes before closing?";
    const BOOL canSave = dirty && !rawDirty;
    if (canSave)
        [alert addButtonWithTitle:@"Save"];
    [alert addButtonWithTitle:@"Discard"];
    [alert addButtonWithTitle:@"Cancel"];
    [alert beginSheetModalForWindow:self.window
                  completionHandler:^(NSModalResponse response) {
                    if (canSave && response == NSAlertFirstButtonReturn) {
                        [self saveAndClose];
                    } else if ((canSave &&
                                response == NSAlertSecondButtonReturn) ||
                               (!canSave &&
                                response == NSAlertFirstButtonReturn)) {
                        self.allowClose = YES;
                        [self.window close];
                    }
                  }];
}

- (void)webView:(WKWebView*)webView
    runJavaScriptAlertPanelWithMessage:(NSString*)message
                     initiatedByFrame:(WKFrameInfo*)frame
                    completionHandler:(void (^)(void))completionHandler {
    (void)webView;
    (void)frame;
    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = @"insys editor";
    alert.informativeText = message;
    [alert addButtonWithTitle:@"OK"];
    [alert beginSheetModalForWindow:self.window
                  completionHandler:^(NSModalResponse response) {
                    (void)response;
                    completionHandler();
                  }];
}

- (void)webView:(WKWebView*)webView
    runJavaScriptConfirmPanelWithMessage:(NSString*)message
                       initiatedByFrame:(WKFrameInfo*)frame
                      completionHandler:(void (^)(BOOL))completionHandler {
    (void)webView;
    (void)frame;
    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = @"insys editor";
    alert.informativeText = message;
    [alert addButtonWithTitle:@"OK"];
    [alert addButtonWithTitle:@"Cancel"];
    [alert beginSheetModalForWindow:self.window
                  completionHandler:^(NSModalResponse response) {
                    completionHandler(response == NSAlertFirstButtonReturn);
                  }];
}

- (void)webView:(WKWebView*)webView
    runJavaScriptTextInputPanelWithPrompt:(NSString*)prompt
                              defaultText:(NSString*)defaultText
                         initiatedByFrame:(WKFrameInfo*)frame
                        completionHandler:(void (^)(NSString*))completionHandler {
    (void)webView;
    (void)frame;
    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = @"insys editor";
    alert.informativeText = prompt;
    [alert addButtonWithTitle:@"OK"];
    [alert addButtonWithTitle:@"Cancel"];
    NSTextField* input = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 360, 24)];
    input.stringValue = defaultText != nil ? defaultText : @"";
    alert.accessoryView = input;
    [alert beginSheetModalForWindow:self.window
                  completionHandler:^(NSModalResponse response) {
                    completionHandler(response == NSAlertFirstButtonReturn
                                          ? input.stringValue
                                          : nil);
                  }];
}

- (void)webView:(WKWebView*)webView
    decidePolicyForNavigationAction:(WKNavigationAction*)action
                    decisionHandler:
                        (void (^)(WKNavigationActionPolicy))decisionHandler {
    (void)webView;
    NSString* url = action.request.URL.absoluteString;
    if ([url hasPrefix:self.origin] || [url isEqualToString:@"about:blank"]) {
        decisionHandler(WKNavigationActionPolicyAllow);
        return;
    }
    self.log->line("shell: blocked navigation " +
                   std::string(url.UTF8String));
    decisionHandler(WKNavigationActionPolicyCancel);
}

- (WKWebView*)webView:(WKWebView*)webView
    createWebViewWithConfiguration:(WKWebViewConfiguration*)configuration
               forNavigationAction:(WKNavigationAction*)action
                    windowFeatures:(WKWindowFeatures*)features {
    (void)webView;
    (void)configuration;
    (void)action;
    (void)features;
    return nil;
}

- (void)webView:(WKWebView*)webView
    didFinishNavigation:(WKNavigation*)navigation {
    (void)webView;
    (void)navigation;
    self.log->line("shell: navigation completed");
}

- (void)webView:(WKWebView*)webView
    didFailProvisionalNavigation:(WKNavigation*)navigation
                           error:(NSError*)error {
    (void)webView;
    (void)navigation;
    self.log->line("shell: navigation failed: " +
                   std::string(error.localizedDescription.UTF8String));
    [self openBrowserFromMenu:nil];
}

@end

namespace {

void print_usage() {
    std::fprintf(
        stderr,
        "usage: insys_editor [options]\n\n"
        "  --port N              listen port (default: ephemeral)\n"
        "  --static PATH         web frontend dist directory\n"
        "  --asset-root PATH     allowed asset root (repeatable)\n"
        "  --token TOKEN         fixed editor token (development only)\n"
        "  --user-data-dir PATH  web view data directory\n"
        "  --devtools            enable WebKit inspector\n"
        "  --browser-only        use an external browser instead of WKWebView\n"
        "  --open-browser        also open the editor in an external browser\n"
        "  --browser PATH        browser application for external tabs\n"
        "  --url-file PATH       write the launch URL to a file\n"
        "  --log PATH            write launcher diagnostics to a file\n"
        "  --smoke-ms N          close automatically after N milliseconds\n"
        "  --help                show this help\n");
}

} // namespace

int main(int argc, char** argv) {
    @autoreleasepool {
        CommandLine config;
        std::string error;
        if (!insys::shell::parse_command_line(argc, argv, config, error)) {
            std::fprintf(stderr, "insys editor: %s\n", error.c_str());
            return 2;
        }
        if (config.help) {
            print_usage();
            return 0;
        }

        Log log;
        if (!log.open(config.shell.logPath)) {
            std::fprintf(stderr, "insys editor: could not open --log file\n");
            return 2;
        }

        insys::editor::EditorServer server(std::move(config.server));
        if (!server.start(&error)) {
            log.line("shell: editor server failed: " + error);
            std::fprintf(stderr, "insys editor: %s\n", error.c_str());
            return 1;
        }
        std::thread serverThread([&server]() { server.run(); });
        log.line(std::string("shell: editor server listening on 127.0.0.1:") +
                 std::to_string(server.port()));

        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        InsysEditorDelegate* delegate = [[InsysEditorDelegate alloc] init];
        delegate.server = &server;
        delegate.config = &config.shell;
        delegate.log = &log;
        [NSApp setDelegate:delegate];
        [delegate installMenu];

        int result = 0;
        if (config.shell.browserOnly) {
            [delegate openBrowserFromMenu:nil];
            log.line("shell: browser-only mode");
        } else if (![delegate createWindow]) {
            result = 1;
        }
        if (result == 0)
            [delegate scheduleSmoke];
        [NSApp run];

        delegate.allowClose = YES;
        if (delegate.window != nil)
            [delegate.window close];
        server.stop();
        if (serverThread.joinable())
            serverThread.join();
        log.line("shell: exit");
        return result;
    }
}
