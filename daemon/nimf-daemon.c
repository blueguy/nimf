/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 2; tab-width: 2 -*-  */
/*
 * nimf-daemon.c
 * This file is part of Nimf.
 *
 * Copyright (C) 2015-2017 Hodong Kim <cogniti@gmail.com>
 *
 * Nimf is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Nimf is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program;  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"
#include <libintl.h>
#include "nimf-server.h"
#include <glib-unix.h>
#include <syslog.h>
#include "nimf-private.h"
#include <glib/gi18n.h>
#include <unistd.h>
#include <libaudit.h>
#include <gio/gunixsocketaddress.h>

gboolean syslog_initialized = FALSE;

gboolean start_indicator_service (gchar *addr)
{
  GSocketAddress    *address;
  GSocketClient     *client;
  GSocketConnection *connection;
  gboolean           retval = FALSE;

  address = g_unix_socket_address_new_with_type (addr, -1,
                                                 G_UNIX_SOCKET_ADDRESS_ABSTRACT);

  client = g_socket_client_new ();

  connection = g_socket_client_connect (client, G_SOCKET_CONNECTABLE (address),
                                        NULL, NULL);

  if (connection)
  {
    NimfMessage *message;

    GSocket *socket = g_socket_connection_get_socket (connection);
    if (socket && !g_socket_is_closed (socket))
      nimf_send_message (socket, 0, NIMF_MESSAGE_START_INDICATOR,
                         NULL, 0, NULL);

    message = nimf_recv_message (socket);
    retval = *(gboolean *) message->data;

    nimf_message_unref (message);
    g_object_unref (connection);
  }

  g_object_unref (client);
  g_object_unref (address);

  return retval;
}

int
main (int argc, char **argv)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  NimfServer *server;
  GMainLoop  *loop;
  gchar      *addr;
  GError     *error = NULL;
  uid_t       uid;
  gboolean    retval = FALSE;

  gboolean is_no_daemon = FALSE;
  gboolean is_debug     = FALSE;
  gboolean is_version   = FALSE;
  gboolean start_indicator = FALSE;

  GOptionContext *context;
  GOptionEntry    entries[] = {
    {"no-daemon", 0, 0, G_OPTION_ARG_NONE, &is_no_daemon, N_("Do not daemonize"), NULL},
    {"debug", 0, 0, G_OPTION_ARG_NONE, &is_debug, N_("Log debugging message"), NULL},
    {"version", 0, 0, G_OPTION_ARG_NONE, &is_version, N_("Version"), NULL},
    {"start-indicator", 0, 0, G_OPTION_ARG_NONE, &start_indicator, N_("Start indicator"), NULL},
    {NULL}
  };

  context = g_option_context_new ("- Nimf Input Method Daemon");
  g_option_context_add_main_entries (context, entries, GETTEXT_PACKAGE);
  g_option_context_parse (context, &argc, &argv, &error);
  g_option_context_free (context);

  if (error != NULL)
  {
    g_warning ("%s", error->message);
    g_error_free (error);
    return EXIT_FAILURE;
  }

  g_setenv ("GTK_IM_MODULE", "gtk-im-context-simple", TRUE);
  g_setenv ("GDK_BACKEND", "x11", TRUE);

#if ENABLE_NLS
  bindtextdomain (GETTEXT_PACKAGE, NIMF_LOCALE_DIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);
#endif

  if (is_debug)
    g_setenv ("G_MESSAGES_DEBUG", "nimf", TRUE);

  if (is_version)
  {
    g_print ("%s %s\n", argv[0], VERSION);
    exit (EXIT_SUCCESS);
  }

  if (is_no_daemon == FALSE)
  {
    openlog (g_get_prgname (), LOG_PID | LOG_PERROR, LOG_DAEMON);
    syslog_initialized = TRUE;
    g_log_set_default_handler ((GLogFunc) nimf_log_default_handler, &is_debug);

    if (daemon (0, 0) != 0)
    {
      g_critical ("Couldn't daemonize.");
      return EXIT_FAILURE;
    }
  }

  uid = audit_getloginuid ();
  if (uid == (uid_t) -1)
    uid = getuid ();

  addr = g_strdup_printf (NIMF_BASE_ADDRESS"%d", uid);

  if (start_indicator)
    retval = start_indicator_service (addr);

  if (retval)
  {
    g_free (addr);
    return EXIT_SUCCESS;
  }

  server = nimf_server_new (addr, &error);
  g_free (addr);

  if (server == NULL)
  {
    g_critical ("%s", error->message);
    g_clear_error (&error);

    if (syslog_initialized)
      closelog ();

    return EXIT_FAILURE;
  }

  nimf_server_start (server, start_indicator);

  loop = g_main_loop_new (NULL, FALSE);

  g_unix_signal_add (SIGINT,  (GSourceFunc) g_main_loop_quit, loop);
  g_unix_signal_add (SIGTERM, (GSourceFunc) g_main_loop_quit, loop);

  g_main_loop_run (loop);

  g_main_loop_unref (loop);
  g_object_unref (server);

  if (syslog_initialized)
    closelog ();

  return EXIT_SUCCESS;
}
