#!/usr/bin/python3

# A simple script for displaying menus of Gtk3 apps remotely.

import gi
gi.require_version('Gtk', '3.0')
gi.require_version('GLib', '2.0')
gi.require_version('GObject', '2.0')

from gi.repository import Gtk, GLib, Gio

import socket
import json as js
import os
from typing import Any, List, Dict, Optional

def get_msg_template(method: str) -> Dict[str, Any]:
    '''
    Create generic message template for the given method call.
    '''
    message = {}
    message["method"] = method
    message["data"] = {}
    return message


class WayfireGioSocket:
    '''
    Minimal adaptation of WayfireSocket to better work with GLib.MainLoop
    '''
    def __init__(self, socket_name: str | None=None, allow_manual_search=False):
        if socket_name is None:
            socket_name = os.getenv("WAYFIRE_SOCKET")

        self.socket_name = None
        self.pending_events = []

        if socket_name is None and allow_manual_search:
            # the last item is the most recent socket file
            socket_list = sorted(
                [
                    os.path.join("/tmp", i)
                    for i in os.listdir("/tmp")
                    if "wayfire-wayland" in i
                ]
            )

            for candidate in socket_list:
                try:
                    self.connect_client(candidate)
                    self.socket_name = candidate
                    break
                except Exception:
                    pass

        elif socket_name is not None:
            self.connect_client(socket_name)
            self.socket_name = socket_name

        if self.socket_name is None:
            raise Exception("Failed to find a suitable Wayfire socket!")

    def connect_client(self, socket_name):
        self.client = Gio.Socket.new(Gio.SocketFamily.UNIX, Gio.SocketType.STREAM, 0)
        # self.client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.client.connect(Gio.UnixSocketAddress.new(socket_name))
        # self.client.setblocking(False)

    def close(self):
        self.client.close()

    def read_exact(self, n: int):
        response = bytes()
        while n > 0:
            try:
                read_this_time = self.client.receive_bytes(n, -1, None).get_data()
            except BlockingIOError:
                return None
            if read_this_time is None:
                return None
            n -= len(read_this_time)
            response += read_this_time

        return response

    def read_message(self):
        tmp1 = self.read_exact(4)
        if tmp1 is None:
            return None
        rlen = int.from_bytes(tmp1, byteorder="little")
        response_message = self.read_exact(rlen)
        if response_message is None:
            raise Exception("Error reading data from socket!\n")
        response = js.loads(response_message)

        if "error" in response and response["error"] == "No such method found!":
            raise Exception(f"Method {response['method']} is not available. \
                    Please ensure that the '{self._wayfire_plugin_from_method(response['method'])}' Wayfire plugin is enabled. \
                    Once enabled, restart Wayfire to ensure that ipc was correctly loaded.")
        elif "error" in response:
            raise Exception(response["error"])
        return response

    def read_next_event(self):
        if self.pending_events:
            return self.pending_events.pop(0)
        return self.read_message()

    def send_json(self, msg):
        if 'method' not in msg:
            raise Exception("Malformed json request: missing method!")

        data = js.dumps(msg).encode("utf8")
        header = len(data).to_bytes(4, byteorder="little")
        self.client.send(header)
        self.client.send(data)

        while True:
            response = self.read_message()
            if 'event' in response:
                self.pending_events.append(response)
                continue

            return response

    def watch(self, events: List[str] | None = None):
        """
        Subscribes to specific events or all events for monitoring.

        This method sends a request to start watching for specified events. If no events are provided,
        it will subscribe to all available events. 

        Args:
            events (List[str] | None): A list of event names to watch. If `None`, subscribes to all events.

        Returns:
            The response from sending the JSON message, which confirms the subscription to the specified
            events.
        """
        method = "window-rules/events/watch"
        message = get_msg_template(method)
        if events is not None:
            message["data"]["events"] = events
        return self.send_json(message)



sock = WayfireGioSocket()
sock.watch()

known_menus = {}
active_view_id = None
entry = None
view_ids = {}
self_id = None
conn = None
menubtn = None

def sock_event(ch, cond):
    global self_id
    global active_view_id
    global conn
    msg = sock.read_next_event()
    if msg is None:
        return False
    if "event" in msg:
        print(msg["event"].ljust(25), end = ": ")
        if "view" in msg:
            if (msg["view"] is not None):
                print(msg["view"]["app-id"], end = " - ")
                print(msg["view"]["id"])
                view_ids[msg["view"]["id"]] = msg["view"]["app-id"]
                if msg["view"]["app-id"] == "wf_gtk_global_menu.py":
                    self_id = msg["view"]["id"]
            else:
                print('')
        else:
            print('')
        if msg["event"] == "view-gtk-dbus-properties-changed":
            menu_path = msg["menubar_path"]
            win_path = msg["window_object_path"]
            app_path = msg["application_object_path"]
            bus_name = msg["unique_bus_name"]
            print("app_menu_path: " + msg["app_menu_path"])
            print("menubar_path: " + menu_path)
            print("window_object_path: " + win_path)
            print("application_object_path: " + app_path)
            print("unique_bus_name: " + bus_name)
            known_menus[msg["view"]["id"]] = (bus_name, menu_path, win_path, app_path)
        elif msg["event"] == "view-focused":
            if ("view" in msg) and (msg["view"] is not None):
                id1 = msg["view"]["id"]
                print("focused view has ID: {}".format(id1))
                if not self_id or (id1 != self_id):
                    active_view_id = id1
                    have_new_menu = False
                    if active_view_id in view_ids:
                        entry.set_text(view_ids[active_view_id])
                        if active_view_id in known_menus:
                            name, path, win_path, app_path = known_menus[active_view_id]
                            if conn is None:
                                conn = Gio.bus_get_sync(Gio.BusType.SESSION, None)
                            menumodel = Gio.DBusMenuModel.get(conn, name, path)
                            menubtn.set_menu_model(menumodel)
                            app_actions = Gio.DBusActionGroup.get(conn, name, app_path)
                            win_actions = Gio.DBusActionGroup.get(conn, name, win_path)
                            menubtn.insert_action_group("app", app_actions)
                            menubtn.insert_action_group("win", win_actions)
                            have_new_menu = True
                    else:
                        entry.set_text("")
                    if not have_new_menu:
                        menubtn.insert_action_group("app", None)
                        menubtn.insert_action_group("win", None)
                        menubtn.set_menu_model(None)
    return True


Gtk.init()

win1 = Gtk.Window()
win1.set_title('App menu test')

box1 = Gtk.Box(orientation = Gtk.Orientation.VERTICAL)
box2 = Gtk.Box(orientation = Gtk.Orientation.HORIZONTAL)
lbl1 = Gtk.Label.new('Active app:  ')
entry = Gtk.Entry()
box2.add(lbl1)
box2.add(entry)
box1.add(box2)

win1.set_size_request(400, 300)
btn = Gtk.MenuButton.new()
btn.set_label('Show menu')
menubtn = btn
# btn.connect("clicked", show_menu_cb)
box1.add(btn)
win1.add(box1)

win1.connect('destroy', Gtk.main_quit)
win1.show_all()

ch = GLib.IOChannel.unix_new(sock.client.get_fd())
ch.add_watch(GLib.IO_IN, sock_event, priority = GLib.PRIORITY_HIGH)

Gtk.main()


