/* mpris.c - Linux media keys as an MPRIS player on the session bus. Polled from core_update.
   Stubs without libdbus (HAVE_DBUS). */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "mpris.h"

#ifndef HAVE_DBUS
int mpris_start(void) { return 0; }
int mpris_poll(int state, const char *title) { (void)state; (void)title; return 0; }
#else
#include <dbus/dbus.h>

#define PATH   "/org/mpris/MediaPlayer2"
#define ROOT   "org.mpris.MediaPlayer2"
#define PLAYER "org.mpris.MediaPlayer2.Player"
#define PROPS  "org.freedesktop.DBus.Properties"

static DBusConnection *bus;
static int  cur_state, sent_state = -1;
static char cur_title[512], sent_title[512];

static const char *root_props[] = { "Identity", "CanQuit", "CanRaise", "HasTrackList", "SupportedUriSchemes", "SupportedMimeTypes", 0 };
static const char *player_props[] = { "PlaybackStatus", "Metadata", "Rate", "Volume", "Position", "MinimumRate", "MaximumRate",
                                      "CanGoNext", "CanGoPrevious", "CanPlay", "CanPause", "CanSeek", "CanControl", 0 };
static const char *xml =
    "<node><interface name='" ROOT "'><method name='Raise'/><method name='Quit'/>"
    "<property name='Identity' type='s' access='read'/><property name='CanQuit' type='b' access='read'/>"
    "<property name='CanRaise' type='b' access='read'/><property name='HasTrackList' type='b' access='read'/>"
    "<property name='SupportedUriSchemes' type='as' access='read'/><property name='SupportedMimeTypes' type='as' access='read'/></interface>"
    "<interface name='" PLAYER "'><method name='Next'/><method name='Previous'/><method name='Pause'/><method name='PlayPause'/>"
    "<method name='Stop'/><method name='Play'/><method name='Seek'><arg type='x' direction='in'/></method>"
    "<method name='SetPosition'><arg type='o' direction='in'/><arg type='x' direction='in'/></method>"
    "<method name='OpenUri'><arg type='s' direction='in'/></method>"
    "<property name='PlaybackStatus' type='s' access='read'/><property name='Metadata' type='a{sv}' access='read'/>"
    "<property name='Rate' type='d' access='readwrite'/><property name='Volume' type='d' access='readwrite'/>"
    "<property name='Position' type='x' access='read'/><property name='MinimumRate' type='d' access='read'/>"
    "<property name='MaximumRate' type='d' access='read'/><property name='CanGoNext' type='b' access='read'/>"
    "<property name='CanGoPrevious' type='b' access='read'/><property name='CanPlay' type='b' access='read'/>"
    "<property name='CanPause' type='b' access='read'/><property name='CanSeek' type='b' access='read'/>"
    "<property name='CanControl' type='b' access='read'/></interface>"
    "<interface name='" PROPS "'><method name='Get'><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='v' direction='out'/></method>"
    "<method name='GetAll'><arg type='s' direction='in'/><arg type='a{sv}' direction='out'/></method>"
    "<method name='Set'><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='v' direction='in'/></method></interface>"
    "<interface name='org.freedesktop.DBus.Introspectable'><method name='Introspect'><arg type='s' direction='out'/></method></interface></node>";

/* variants */
static void var(DBusMessageIter *it, int type, const char *sig, const void *v)
{
    DBusMessageIter x;
    dbus_message_iter_open_container(it, DBUS_TYPE_VARIANT, sig, &x);
    dbus_message_iter_append_basic(&x, type, v);
    dbus_message_iter_close_container(it, &x);
}
static void var_s(DBusMessageIter *it, const char *s) { var(it, DBUS_TYPE_STRING, "s", &s); }
static void var_b(DBusMessageIter *it, int b) { dbus_bool_t x = b; var(it, DBUS_TYPE_BOOLEAN, "b", &x); }
static void var_d(DBusMessageIter *it, double d) { var(it, DBUS_TYPE_DOUBLE, "d", &d); }
static void entry(DBusMessageIter *dict, const char *key, int type, const char *sig, const void *v)
{
    DBusMessageIter e;
    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &e);
    dbus_message_iter_append_basic(&e, DBUS_TYPE_STRING, &key);
    var(&e, type, sig, v);
    dbus_message_iter_close_container(dict, &e);
}

static int put(DBusMessageIter *it, const char *name)  /* one property's value as a variant; 0 if unknown */
{
    if (!strcmp(name, "Identity")) var_s(it, "Kollapse");
    else if (!strcmp(name, "PlaybackStatus")) var_s(it, cur_state == 1 ? "Playing" : cur_state == 2 ? "Paused" : "Stopped");
    else if (!strcmp(name, "CanQuit") || !strcmp(name, "CanRaise") || !strcmp(name, "HasTrackList") || !strcmp(name, "CanSeek")) var_b(it, 0);
    else if (!strncmp(name, "Can", 3)) var_b(it, 1);
    else if (!strcmp(name, "Rate") || !strcmp(name, "Volume") || !strcmp(name, "MinimumRate") || !strcmp(name, "MaximumRate")) var_d(it, 1.0);
    else if (!strcmp(name, "Position")) { dbus_int64_t p = 0; var(it, DBUS_TYPE_INT64, "x", &p); }
    else if (!strncmp(name, "Supported", 9)) {
        DBusMessageIter v, a;
        dbus_message_iter_open_container(it, DBUS_TYPE_VARIANT, "as", &v);
        dbus_message_iter_open_container(&v, DBUS_TYPE_ARRAY, "s", &a);
        dbus_message_iter_close_container(&v, &a);
        dbus_message_iter_close_container(it, &v);
    } else if (!strcmp(name, "Metadata")) {
        DBusMessageIter v, d;
        const char *id = "/org/kollapse/track", *t = cur_title;
        dbus_message_iter_open_container(it, DBUS_TYPE_VARIANT, "a{sv}", &v);
        dbus_message_iter_open_container(&v, DBUS_TYPE_ARRAY, "{sv}", &d);
        entry(&d, "mpris:trackid", DBUS_TYPE_OBJECT_PATH, "o", &id);
        if (*t) entry(&d, "xesam:title", DBUS_TYPE_STRING, "s", &t);
        dbus_message_iter_close_container(&v, &d);
        dbus_message_iter_close_container(it, &v);
    } else return 0;
    return 1;
}

static void put_all(DBusMessageIter *it, const char **names)   /* a{sv} */
{
    DBusMessageIter d, e;
    dbus_message_iter_open_container(it, DBUS_TYPE_ARRAY, "{sv}", &d);
    for (; names && *names; names++) {
        dbus_message_iter_open_container(&d, DBUS_TYPE_DICT_ENTRY, NULL, &e);
        dbus_message_iter_append_basic(&e, DBUS_TYPE_STRING, names);
        put(&e, *names);
        dbus_message_iter_close_container(&d, &e);
    }
    dbus_message_iter_close_container(it, &d);
}

static void reply(DBusMessage *m, int *cmd)
{
    DBusMessage *r = NULL;
    DBusMessageIter it;
    const char *iface = "", *name = "";
    if (dbus_message_get_type(m) != DBUS_MESSAGE_TYPE_METHOD_CALL) return;
    if (dbus_message_is_method_call(m, "org.freedesktop.DBus.Introspectable", "Introspect")) {
        r = dbus_message_new_method_return(m);
        dbus_message_append_args(r, DBUS_TYPE_STRING, &xml, DBUS_TYPE_INVALID);
    } else if (dbus_message_is_method_call(m, PROPS, "GetAll")) {
        dbus_message_get_args(m, NULL, DBUS_TYPE_STRING, &iface, DBUS_TYPE_INVALID);
        r = dbus_message_new_method_return(m);
        dbus_message_iter_init_append(r, &it);
        put_all(&it, !strcmp(iface, ROOT) ? root_props : !strcmp(iface, PLAYER) ? player_props : NULL);
    } else if (dbus_message_is_method_call(m, PROPS, "Get")) {
        dbus_message_get_args(m, NULL, DBUS_TYPE_STRING, &iface, DBUS_TYPE_STRING, &name, DBUS_TYPE_INVALID);
        r = dbus_message_new_method_return(m);
        dbus_message_iter_init_append(r, &it);
        if (!put(&it, name)) { dbus_message_unref(r); r = dbus_message_new_error(m, DBUS_ERROR_UNKNOWN_PROPERTY, name); }
    } else if (!strcmp(dbus_message_get_interface(m) ? dbus_message_get_interface(m) : "", PLAYER)) {
        const char *mb = dbus_message_get_member(m);
        if (!strcmp(mb, "PlayPause")) *cmd = 1;
        else if (!strcmp(mb, "Play")) *cmd = cur_state != 1 ? 1 : 0;
        else if (!strcmp(mb, "Pause")) *cmd = cur_state == 1 ? 1 : 0;
        else if (!strcmp(mb, "Next")) *cmd = 2;
        else if (!strcmp(mb, "Previous")) *cmd = 3;
        else if (!strcmp(mb, "Stop")) *cmd = 4;
        r = dbus_message_new_method_return(m);        /* Seek, SetPosition, OpenUri: accepted, ignored */
    } else if (dbus_message_is_method_call(m, PROPS, "Set") || dbus_message_has_interface(m, ROOT))
        r = dbus_message_new_method_return(m);        /* Raise, Quit, setting Volume/Rate: ignored */
    else r = dbus_message_new_error(m, DBUS_ERROR_UNKNOWN_METHOD, "unknown method");
    dbus_connection_send(bus, r, NULL);
    dbus_message_unref(r);
}

int mpris_start(void)
{
    DBusError err;
    dbus_error_init(&err);
    bus = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (!bus) { dbus_error_free(&err); return 0; }
    dbus_connection_set_exit_on_disconnect(bus, FALSE);
    char name[96] = "org.mpris.MediaPlayer2.kollapse";       /* a second Kollapse gets .instance<pid> */
    if (dbus_bus_request_name(bus, name, DBUS_NAME_FLAG_DO_NOT_QUEUE, &err) != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        dbus_error_free(&err);
        snprintf(name, sizeof name, "org.mpris.MediaPlayer2.kollapse.instance%d", (int)getpid());
        if (dbus_bus_request_name(bus, name, DBUS_NAME_FLAG_DO_NOT_QUEUE, &err) != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
            dbus_error_free(&err); bus = NULL; return 0;
        }
    }
    return 1;
}

int mpris_poll(int state, const char *title)
{
    if (!bus) return 0;
    char *o = cur_title;                               /* module names are Latin-1 / CP437: D-Bus wants UTF-8 */
    for (const unsigned char *s = (const unsigned char *)title; *s && o < cur_title + sizeof cur_title - 3; s++)
        if (*s < 0x80) *o++ = (char)(*s < 32 ? ' ' : *s);
        else { *o++ = (char)(0xc0 | *s >> 6); *o++ = (char)(0x80 | (*s & 0x3f)); }
    *o = 0;
    cur_state = state;
    if (!dbus_connection_read_write(bus, 0)) { bus = NULL; return 0; }   /* the bus went away */
    int cmd = 0;
    DBusMessage *m;
    while ((m = dbus_connection_pop_message(bus))) { reply(m, &cmd); dbus_message_unref(m); }
    if (state != sent_state || strcmp(cur_title, sent_title)) {   /* tell panels what changed */
        static const char *changed[] = { "PlaybackStatus", "Metadata", 0 };
        DBusMessage *s = dbus_message_new_signal(PATH, PROPS, "PropertiesChanged");
        DBusMessageIter it, a;
        const char *ifc = PLAYER;
        dbus_message_iter_init_append(s, &it);
        dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &ifc);
        put_all(&it, changed);
        dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "s", &a);
        dbus_message_iter_close_container(&it, &a);
        dbus_connection_send(bus, s, NULL);
        dbus_message_unref(s);
        sent_state = state; strcpy(sent_title, cur_title);
    }
    dbus_connection_flush(bus);
    return cmd;
}
#endif
