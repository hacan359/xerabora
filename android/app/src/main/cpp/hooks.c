/*
  The two desktop behaviours that make no sense on a phone, replaced at
  link time (-Wl,--wrap in CMakeLists.txt) so webui.c stays untouched.
*/
#include "glue.h"
#include "log.h"

/* The desktop client exits 15 s after its last page closes. Here the page
   is a WebView the system pauses whenever the screen goes off, and the
   service decides when the client stops (Stop in the notification, or
   QUIT on the page). */
int __wrap_webui_page_gone(int grace_seconds)
{
    (void)grace_seconds;
    return 0;
}

/* The desktop client opens a browser here; the app shows the page in its
   own WebView once it knows the port. */
void __wrap_webui_open_browser(int port)
{
    log_info("interface at http://127.0.0.1:%d/", port);
    glue_ui_ready(port);
}
