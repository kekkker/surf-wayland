#pragma once

#include <wpe/webkit.h>

/* Connects "run-file-chooser" on the WebView. The handler spawns the
 * configured external picker (filepicker_cmd in config.h), reads the
 * selected paths from a temp file, and feeds them back to WebKit. */
void filepicker_install(WebKitWebView *wv);
