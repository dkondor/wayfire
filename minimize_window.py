#!/usr/bin/env python3

import gi

gi.require_version('Gtk', '3.0')
gi.require_version('GObject', '2.0')

from gi.repository import Gtk
from gi.repository import GObject

def minimize_timeout(win):
	if win.btn.get_active():
		win.iconify()
		return True
	else:
		win.timeout_id = 0
		return False


class MinimizeWindow(Gtk.Window):
	def __init__(self):
		self.timeout_id = 0
		
		Gtk.Window.__init__(self)

		self.set_size_request(400, 300)
		self.btn = Gtk.CheckButton.new_with_label('Auto-minimize')
		self.btn.connect("toggled", self.on_toggled)
		self.add(self.btn)

		self.connect('destroy', Gtk.main_quit)

		self.show_all()

	def on_toggled(self, btn):
		if self.btn.get_active():
			if self.timeout_id == 0:
				self.timeout_id = GObject.timeout_add(5000, minimize_timeout, self)
	


MinimizeWindow()
Gtk.main()
