#!/bin/env python
from i3ipc import Connection, Event

i3 = Connection()

def on_mode_change(i3, e):
    mode_code = {
        'default': 'd',
        'mouse': 'm'
    }.get(e.change, 'd')

    with open('/run/keyboard-remapper.sock', 'w') as fd:
        fd.write(mode_code)

i3.on(Event.MODE, on_mode_change)

i3.main()
