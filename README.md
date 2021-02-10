Idiosyncratic modifications to how my keyboard behaves.

This project uses [interception tools](https://gitlab.com/interception/linux/tools) to make some modifications to how the keyboard behaves:

* Alt+space mapped to enter
* Alt+o, Alt+; mapped to up and down arrows. Alt+[, Alt+] mapped to home and end.
* A mouse mode where the keyboard can be used to control the mouse pointer and click.

I also use [dual-function-keys](https://gitlab.com/interception/linux/plugins/dual-function-keys) (another interception tools script) to map right alt to control when held, and to escape when tapped. I'm also using the built in X sticky shift (tap or hold to make next key shifted, double tap for caps lock mode), and the Colemak keyboard layout.

# Building and usage

The main script is a low dependency C file (should just be stdlib and standard Linux libraries). It can be compiled using:

    gcc -o keyboard-remapper keyboard-remapper.c

Next, I run the keyboard-mapper (and dual-function-keys) using SystemD. My .service file is included also. The keyboard-i3-binding.py file swaps the script to and from mouse mode in response to the mode being switched in i3 (my window manager).
