#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <jsc/jsc.h>
#include <webkit2/webkit2.h>

#include <cstdio>
#include <functional>
#include <string>
#include <thread>
#include <utility>

#include "insys/editor/editor_server.h"
#include "posix_shell.h"

using insys::shell::CommandLine;
using insys::shell::Log;
using insys::shell::ShellConfig;

namespace {

struct App {
    insys::editor::EditorServer* server = nullptr;
    ShellConfig* config = nullptr;
    Log* log = nullptr;
    GtkWidget* window = nullptr;
    WebKitWebView* web = nullptr;
    std::string origin;
    bool allowClose = false;
    bool closeCheck = false;
    bool savePolling = false;
    gint64 saveStarted = 0;
};

App g_app;

std::string json_quote(const std::string& text) {
    std::string result = "\"";
    for (const unsigned char character : text) {
        switch (character) {
        case '\\':
            result += "\\\\";
            break;
        case '"':
            result += "\\\"";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        default:
            if (character < 0x20) {
                char escaped[7]{};
                std::snprintf(escaped, sizeof(escaped), "\\u%04x",
                              static_cast<unsigned int>(character));
                result += escaped;
            } else {
                result.push_back(static_cast<char>(character));
            }
            break;
        }
    }
    result += "\"";
    return result;
}

void show_message(const std::string& message) {
    GtkWidget* dialog = gtk_message_dialog_new(
        g_app.window != nullptr ? GTK_WINDOW(g_app.window) : nullptr,
        GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "%s",
        message.c_str());
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

void run_script(const std::string& script) {
    if (g_app.web == nullptr)
        return;
    webkit_web_view_run_javascript(g_app.web, script.c_str(), nullptr,
                                   nullptr, nullptr);
}

struct StateRequest {
    std::function<void(bool, bool, bool, bool)> done;
};

void state_finished(GObject* source, GAsyncResult* result, gpointer user_data) {
    auto* request = static_cast<StateRequest*>(user_data);
    GError* error = nullptr;
    WebKitJavascriptResult* javascript =
        webkit_web_view_run_javascript_finish(WEBKIT_WEB_VIEW(source), result,
                                              &error);
    std::string text;
    if (javascript != nullptr) {
        JSCValue* value = webkit_javascript_result_get_js_value(javascript);
        if (value != nullptr) {
            gchar* raw = jsc_value_to_string(value);
            if (raw != nullptr) {
                text = raw;
                g_free(raw);
            }
        }
        webkit_javascript_result_unref(javascript);
    }
    if (error != nullptr)
        g_error_free(error);
    const bool valid = text.size() == 3;
    request->done(valid, valid && text[0] == '1', valid && text[1] == '1',
                  valid && text[2] == '1');
    delete request;
}

void evaluate_state(std::function<void(bool, bool, bool, bool)> done) {
    if (g_app.web == nullptr) {
        done(false, false, false, false);
        return;
    }
    auto* request = new StateRequest{std::move(done)};
    const char* script =
        "(function(){var s=window.__insysEditorHost&&"
        "window.__insysEditorHost.state();return s?"
        "(s.dirty?'1':'0')+(s.rawDirty?'1':'0')+(s.autoSave?'1':'0'):'x';})()";
    webkit_web_view_run_javascript(g_app.web, script, nullptr, state_finished,
                                   request);
}

void close_now() {
    g_app.allowClose = true;
    if (g_app.window != nullptr)
        gtk_widget_destroy(g_app.window);
}

gboolean poll_save(gpointer) {
    if (g_app.web == nullptr)
        return G_SOURCE_REMOVE;
    evaluate_state([](bool valid, bool dirty, bool rawDirty, bool) {
        if (valid && !dirty && !rawDirty) {
            g_app.savePolling = false;
            close_now();
            return;
        }
        if (g_get_monotonic_time() - g_app.saveStarted > 5 * G_TIME_SPAN_SECOND) {
            g_app.savePolling = false;
            show_message("Saving did not finish within five seconds.");
        }
    });
    return g_app.savePolling ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
}

void save_and_close() {
    if (g_app.savePolling)
        return;
    g_app.savePolling = true;
    g_app.saveStarted = g_get_monotonic_time();
    run_script("window.__insysEditorHost && window.__insysEditorHost.save()");
    g_timeout_add(100, poll_save, nullptr);
}

void prompt_close(bool dirty, bool rawDirty) {
    const bool canSave = dirty && !rawDirty;
    GtkWidget* dialog = gtk_message_dialog_new(
        GTK_WINDOW(g_app.window), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING,
        GTK_BUTTONS_NONE, "%s",
        rawDirty ? "The Raw JSON changes have not been applied. Discard them?"
                 : "Save changes before closing?");
    gtk_window_set_title(GTK_WINDOW(dialog), "Unsaved changes");
    if (canSave)
        gtk_dialog_add_button(GTK_DIALOG(dialog), "_Save", GTK_RESPONSE_YES);
    gtk_dialog_add_button(GTK_DIALOG(dialog), "_Discard", GTK_RESPONSE_NO);
    gtk_dialog_add_button(GTK_DIALOG(dialog), "_Cancel", GTK_RESPONSE_CANCEL);
    const gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    if (canSave && response == GTK_RESPONSE_YES) {
        save_and_close();
    } else if ((canSave && response == GTK_RESPONSE_NO) ||
               (!canSave && response == GTK_RESPONSE_YES)) {
        close_now();
    }
}

gboolean on_delete_event(GtkWidget*, GdkEvent*, gpointer) {
    if (g_app.allowClose || g_app.web == nullptr)
        return FALSE;
    if (g_app.closeCheck)
        return TRUE;
    g_app.closeCheck = true;
    evaluate_state([](bool valid, bool dirty, bool rawDirty, bool autoSave) {
        g_app.closeCheck = false;
        if (!valid || (!dirty && !rawDirty)) {
            close_now();
            return;
        }
        if (autoSave && dirty && !rawDirty) {
            save_and_close();
            return;
        }
        prompt_close(dirty, rawDirty);
    });
    return TRUE;
}

void on_destroy(GtkWidget*, gpointer) {
    g_app.window = nullptr;
    g_app.web = nullptr;
    gtk_main_quit();
}

void on_open_browser(GtkMenuItem*, gpointer) {
    std::string error;
    if (!insys::shell::open_external_browser(
            g_app.config->browserPath,
            insys::shell::make_url(*g_app.server), error)) {
        show_message("Could not open the external browser: " + error);
        g_app.log->line("shell: external browser failed: " + error);
    }
}

void open_asset_response(GtkDialog* dialog, gint response, gpointer) {
    if (response == GTK_RESPONSE_ACCEPT) {
        gchar* filename =
            gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        if (filename != nullptr) {
            const std::string path(filename);
            g_free(filename);
            run_script("window.__insysEditorHost && "
                       "window.__insysEditorHost.openAsset(" +
                       json_quote(path) + ")");
            g_app.log->line("shell: selected asset " + path);
        }
    }
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

void on_open_asset(GtkMenuItem*, gpointer) {
    if (g_app.web == nullptr) {
        show_message("Open Asset is available in the app window.");
        return;
    }
    GtkWidget* dialog = gtk_file_chooser_dialog_new(
        "Open Asset", GTK_WINDOW(g_app.window), GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, nullptr);
    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Input action assets");
    gtk_file_filter_add_pattern(filter, "*.json");
    gtk_file_filter_add_pattern(filter, "*.insysb");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
    g_signal_connect(dialog, "response", G_CALLBACK(open_asset_response),
                     nullptr);
    gtk_widget_show_all(dialog);
}

void on_quit(GtkMenuItem*, gpointer) {
    if (g_app.window != nullptr)
        gtk_window_close(GTK_WINDOW(g_app.window));
    else
        gtk_main_quit();
}

gboolean on_decide_policy(WebKitWebView*, WebKitPolicyDecision* decision,
                          WebKitPolicyDecisionType type, gpointer) {
    if (type != WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION)
        return FALSE;
    WebKitNavigationAction* action =
        webkit_navigation_policy_decision_get_navigation_action(
            WEBKIT_NAVIGATION_POLICY_DECISION(decision));
    WebKitURIRequest* request =
        webkit_navigation_action_get_request(action);
    const char* uri = webkit_uri_request_get_uri(request);
    if (g_str_has_prefix(uri, g_app.origin.c_str()) ||
        g_strcmp0(uri, "about:blank") == 0) {
        webkit_policy_decision_use(decision);
        return TRUE;
    }
    g_app.log->line(std::string("shell: blocked navigation ") + uri);
    webkit_policy_decision_ignore(decision);
    return TRUE;
}

GtkWidget* on_create(WebKitWebView*, WebKitNavigationAction*, gpointer) {
    return nullptr;
}

void on_load_changed(WebKitWebView*, WebKitLoadEvent event, gpointer) {
    if (event == WEBKIT_LOAD_FINISHED)
        g_app.log->line("shell: navigation completed");
}

gboolean on_load_failed(WebKitWebView*, WebKitLoadEvent, const gchar* uri,
                        GError* error, gpointer) {
    g_app.log->line(std::string("shell: navigation failed ") + uri + ": " +
                    (error != nullptr ? error->message : "unknown error"));
    on_open_browser(nullptr, nullptr);
    return FALSE;
}

void install_menu(GtkWidget* window, GtkWidget* root) {
    GtkAccelGroup* accelerators = gtk_accel_group_new();
    gtk_window_add_accel_group(GTK_WINDOW(window), accelerators);

    GtkWidget* bar = gtk_menu_bar_new();
    GtkWidget* fileItem = gtk_menu_item_new_with_label("File");
    GtkWidget* fileMenu = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(fileItem), fileMenu);

    GtkWidget* open = gtk_menu_item_new_with_label("Open Asset...");
    g_signal_connect(open, "activate", G_CALLBACK(on_open_asset), nullptr);
    gtk_widget_add_accelerator(open, "activate", accelerators, GDK_KEY_o,
                               GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    gtk_menu_shell_append(GTK_MENU_SHELL(fileMenu), open);

    GtkWidget* browser = gtk_menu_item_new_with_label("Open in Browser");
    g_signal_connect(browser, "activate", G_CALLBACK(on_open_browser), nullptr);
    gtk_widget_add_accelerator(browser, "activate", accelerators, GDK_KEY_b,
                               GDK_CONTROL_MASK | GDK_SHIFT_MASK,
                               GTK_ACCEL_VISIBLE);
    gtk_menu_shell_append(GTK_MENU_SHELL(fileMenu), browser);

    GtkWidget* separator = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(fileMenu), separator);

    GtkWidget* quit = gtk_menu_item_new_with_label("Quit");
    g_signal_connect(quit, "activate", G_CALLBACK(on_quit), nullptr);
    gtk_widget_add_accelerator(quit, "activate", accelerators, GDK_KEY_q,
                               GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    gtk_menu_shell_append(GTK_MENU_SHELL(fileMenu), quit);

    gtk_menu_shell_append(GTK_MENU_SHELL(bar), fileItem);
    gtk_box_pack_start(GTK_BOX(root), bar, FALSE, FALSE, 0);
}

gboolean smoke_timeout(gpointer) {
    g_app.allowClose = true;
    if (g_app.window != nullptr)
        gtk_widget_destroy(g_app.window);
    else
        gtk_main_quit();
    return G_SOURCE_REMOVE;
}

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
        "  --browser-only        use an external browser instead of WebKitGTK\n"
        "  --open-browser        also open the editor in an external browser\n"
        "  --browser PATH        browser executable for external tabs\n"
        "  --url-file PATH       write the launch URL to a file\n"
        "  --log PATH            write launcher diagnostics to a file\n"
        "  --smoke-ms N          close automatically after N milliseconds\n"
        "  --help                show this help\n");
}

} // namespace

int main(int argc, char** argv) {
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

    g_app.server = &server;
    g_app.config = &config.shell;
    g_app.log = &log;
    g_app.origin = insys::shell::make_origin(server);
    const std::string url = insys::shell::make_url(server);
    insys::shell::write_url_file(config.shell.urlFile, url);

    gtk_init(nullptr, nullptr);
    g_app.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(g_app.window), "insys editor");
    gtk_window_set_default_size(GTK_WINDOW(g_app.window), 1280, 820);
    g_signal_connect(g_app.window, "delete-event", G_CALLBACK(on_delete_event),
                     nullptr);
    g_signal_connect(g_app.window, "destroy", G_CALLBACK(on_destroy), nullptr);

    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(g_app.window), root);
    install_menu(g_app.window, root);

    if (config.shell.browserOnly) {
        GtkWidget* label = gtk_label_new(
            "insys editor is open in an external browser.\n"
            "Close this window to stop the local editor server.");
        gtk_box_pack_start(GTK_BOX(root), label, TRUE, TRUE, 0);
        g_app.log->line("shell: browser-only mode");
    } else {
        g_app.web = WEBKIT_WEB_VIEW(webkit_web_view_new());
        WebKitSettings* settings = webkit_web_view_get_settings(g_app.web);
        webkit_settings_set_enable_developer_extras(settings,
                                                    config.shell.devtools);
        g_signal_connect(g_app.web, "decide-policy",
                         G_CALLBACK(on_decide_policy), nullptr);
        g_signal_connect(g_app.web, "create", G_CALLBACK(on_create), nullptr);
        g_signal_connect(g_app.web, "load-changed",
                         G_CALLBACK(on_load_changed), nullptr);
        g_signal_connect(g_app.web, "load-failed",
                         G_CALLBACK(on_load_failed), nullptr);
        gtk_box_pack_start(GTK_BOX(root), GTK_WIDGET(g_app.web), TRUE, TRUE, 0);
        if (config.shell.devtools)
            webkit_settings_set_enable_write_console_messages_to_stdout(
                settings, TRUE);
        if (config.shell.remoteDebuggingPort != 0) {
            log.line("shell: --remote-debugging-port is not supported by "
                     "WebKitGTK");
        }
        log.line("shell: loading " + insys::shell::redact_url(url));
        webkit_web_view_load_uri(g_app.web, url.c_str());
        if (config.shell.openBrowser)
            on_open_browser(nullptr, nullptr);
    }

    gtk_widget_show_all(g_app.window);
    if (config.shell.browserOnly)
        on_open_browser(nullptr, nullptr);
    if (config.shell.smokeMs != 0)
        g_timeout_add(config.shell.smokeMs, smoke_timeout, nullptr);

    gtk_main();

    g_app.allowClose = true;
    if (g_app.window != nullptr)
        gtk_widget_destroy(g_app.window);
    server.stop();
    if (serverThread.joinable())
        serverThread.join();
    log.line("shell: exit");
    return 0;
}
