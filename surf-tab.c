#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    GtkWidget *notebook;
    GList *instances;
} TabManager;

/* Function to add new surf instance to tab manager */
static void
add_surf_instance(TabManager *tm, const char *uri)
{
    char *instance_id;
    char *cmd[] = {"surf", NULL, NULL};

    /* Generate unique instance ID for this tab */
    instance_id = g_strdup_printf("surf-tab-%ld-%d", (long)getpid(), g_random_int());

    /* Set up command with URI */
    if (uri && strlen(uri) > 0) {
        cmd[1] = (char *)uri;
    }

    /* Launch surf instance */
    g_spawn_async(NULL, cmd, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);

    /* Add to instance list */
    tm->instances = g_list_append(tm->instances, instance_id);
}

/* Function to create new tab */
static void
new_tab_clicked(GtkWidget *button, gpointer data)
{
    TabManager *tm = (TabManager *)data;
    GtkWidget *label;
    GtkWidget *content;
    gint page_num;

    /* Create a simple placeholder for the new tab */
    content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(content), 10);

    label = gtk_label_new("New tab will open surf instance\nClose this window to surf normally");
    gtk_box_pack_start(GTK_BOX(content), label, TRUE, TRUE, 0);

    /* Add new tab */
    page_num = gtk_notebook_append_page(GTK_NOTEBOOK(tm->notebook), content,
                                        gtk_label_new("New Tab"));
    gtk_notebook_set_current_page(GTK_NOTEBOOK(tm->notebook), page_num);

    /* Launch surf instance */
    add_surf_instance(tm, "about:blank");

    gtk_widget_show_all(content);
}

/* Function to close current tab */
static gboolean
tab_close_request(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    TabManager *tm = (TabManager *)data;
    gint page_num;
    GtkWidget *child;

    if (event->button == GDK_BUTTON_MIDDLE) {
        page_num = gtk_notebook_get_current_page(GTK_NOTEBOOK(tm->notebook));
        if (page_num >= 0) {
            child = gtk_notebook_get_nth_page(GTK_NOTEBOOK(tm->notebook), page_num);
            gtk_notebook_remove_page(GTK_NOTEBOOK(tm->notebook), page_num);

            /* If no more tabs, close the tab manager */
            if (gtk_notebook_get_n_pages(GTK_NOTEBOOK(tm->notebook)) == 0) {
                gtk_main_quit();
            }
        }
        return TRUE;
    }
    return FALSE;
}

int
main(int argc, char *argv[])
{
    TabManager *tm;
    GtkWidget *window;
    GtkWidget *vbox;
    GtkWidget *hbox;
    GtkWidget *new_tab_button;
    GtkWidget *scrolled_window;
    const char *initial_uri = "about:blank";

    gtk_init(&argc, &argv);

    /* Create tab manager structure */
    tm = g_malloc0(sizeof(TabManager));
    tm->instances = NULL;

    /* Create main window */
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Surf Tab Manager");
    gtk_window_set_default_size(GTK_WINDOW(window), 800, 600);
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);

    /* Create vertical box */
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    /* Create toolbar */
    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 5);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

    /* Create new tab button */
    new_tab_button = gtk_button_new_with_label("New Tab");
    gtk_box_pack_start(GTK_BOX(hbox), new_tab_button, FALSE, FALSE, 0);
    g_signal_connect(new_tab_button, "clicked", G_CALLBACK(new_tab_clicked), tm);

    /* Create notebook for tabs */
    scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(vbox), scrolled_window, TRUE, TRUE, 0);

    tm->notebook = gtk_notebook_new();
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(tm->notebook), TRUE);
    gtk_notebook_set_tab_pos(GTK_NOTEBOOK(tm->notebook), GTK_POS_TOP);
    gtk_container_add(GTK_CONTAINER(scrolled_window), tm->notebook);

    /* Handle initial URI if provided */
    if (argc > 1) {
        initial_uri = argv[1];
    }

    /* Add initial tab */
    add_surf_instance(tm, initial_uri);

    /* Connect signals */
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(tm->notebook, "button-press-event",
                     G_CALLBACK(tab_close_request), tm);

    /* Show everything */
    gtk_widget_show_all(window);

    /* Run main loop */
    gtk_main();

    /* Cleanup */
    g_list_free_full(tm->instances, g_free);
    g_free(tm);

    return 0;
}