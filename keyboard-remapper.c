#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/input.h>

#define INPUT_KEY_DOWN 1
#define INPUT_KEY_UP 0

// Step which the mouse pointer moves by, normally, and when control is pressed
const int small_mouse_step = 5;
const int large_mouse_step = 30;

// Some pre-canned events we emit
const struct input_event
alt_down        = {.type = EV_KEY, .code = KEY_LEFTALT, .value = INPUT_KEY_DOWN},
alt_up          = {.type = EV_KEY, .code = KEY_LEFTALT, .value = INPUT_KEY_UP},
btn_left_down   = {.type = EV_KEY, .code = BTN_LEFT, .value = INPUT_KEY_DOWN},
btn_left_up     = {.type = EV_KEY, .code = BTN_LEFT, .value = INPUT_KEY_UP},
btn_right_down   = {.type = EV_KEY, .code = BTN_RIGHT, .value = INPUT_KEY_DOWN},
btn_right_up     = {.type = EV_KEY, .code = BTN_RIGHT, .value = INPUT_KEY_UP},
btn_middle_down   = {.type = EV_KEY, .code = BTN_MIDDLE, .value = INPUT_KEY_DOWN},
btn_middle_up     = {.type = EV_KEY, .code = BTN_MIDDLE, .value = INPUT_KEY_UP},
syn             = {.type = EV_SYN, .code = SYN_REPORT, .value = 0};

// Holds which modifiers are currently pressed
struct {
    int alt;
    int ctrl;
} modifiers_held;

// Mode for the program, either 'd', 'm', or 'n' for default mode, mouse mode, and noop
char program_mode = 'd';

/**
 * Updates the program mode based on what has been pushed in to the given file (a FIFO)
 */
void update_program_mode(int fd) {
    char buf[80];
    int nchars;
    while (1) {
        nchars = read(fd, &buf, 80);

        if (nchars > 0) {
            program_mode = buf[nchars - 1];
            if (!(program_mode == 'd' || program_mode == 'm' || program_mode == 'n')) {
                program_mode = 'd';
            }
        } else {
            break;
        }
    }
}

int setup_fifo(char *path) {
    int fd;

    unlink(path);
    // The umask can be set by a SystemD unit
    mkfifo(path, 0777);
    // The above one can only restrict the umask
    /* chmod(path, 0777); */

    fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        exit(EXIT_FAILURE);
    }

    return fd;
}

void update_modifiers_held(const struct input_event *event) {
    if (event->code == KEY_LEFTALT && event->value == INPUT_KEY_DOWN) {
        modifiers_held.alt = 1;
    } else if (event->code == KEY_LEFTALT && event->value == INPUT_KEY_UP) {
        modifiers_held.alt = 0;
    } else if (event->code == KEY_LEFTCTRL && event->value == INPUT_KEY_DOWN) {
        modifiers_held.ctrl = 1;
    } else if (event->code == KEY_LEFTCTRL && event->value == INPUT_KEY_UP) {
        modifiers_held.ctrl = 0;
    }
}

void write_event(const struct input_event *event) {
    // Debugging line
    /* fprintf(stderr, "e: %d %d %d\n", event->type, event->code, event->value); */
    if (fwrite(event, sizeof(struct input_event), 1, stdout) != 1) {
        exit(EXIT_FAILURE);
    }
}

int handle_mouse_move(const struct input_event *event) {
    struct input_event synthetic_event;
    int velocity = modifiers_held.ctrl ? large_mouse_step : small_mouse_step;
    int key_info[6][3] = {
        {KEY_J, REL_X, -1 * velocity},
        {KEY_L, REL_X, 1 * velocity},
        {KEY_I, REL_Y, -1 * velocity},
        {KEY_K, REL_Y, 1 * velocity},
        {KEY_O, REL_WHEEL, 1},
        {KEY_SEMICOLON, REL_WHEEL, -1}
    };

    synthetic_event.type = EV_REL;

    for (int i=0; i<6;i++) {
        if (event->code == key_info[i][0]) {
            if (event->value != INPUT_KEY_UP) {
                // Key down and key repeat should both trigger this
                synthetic_event.code = key_info[i][1];
                synthetic_event.value = key_info[i][2];

                write_event(&synthetic_event);
                write_event(&syn);
            }

            // Block the event
            return 1;
        }
    }

    // Allow the event to propagate
    return 0;
}

int handle_mouse_button(const struct input_event *event) {
    if (event->code == KEY_M) {
        // Middle button
        write_event(event->value == INPUT_KEY_UP ? &btn_middle_up : &btn_middle_down);
    } else if (event->code == KEY_SPACE && modifiers_held.alt == 0) {
        // Left click
        write_event(event->value == INPUT_KEY_UP ? &btn_left_up : &btn_left_down);
    } else if (event->code == KEY_SPACE && modifiers_held.alt == 1) {
        // Right click
        if (event->value == INPUT_KEY_DOWN) {
            write_event(event->value == INPUT_KEY_UP ? &alt_down : &alt_up);
        }
        write_event(event->value == INPUT_KEY_UP ? &btn_right_up : &btn_right_down);
    } else if (event->code == KEY_H) {
        // Left button down
        if (event->value == INPUT_KEY_UP) {
            write_event(&btn_left_down);
        }
    } else if (event->code == KEY_N) {
        // Left button up
        if (event->value == INPUT_KEY_UP) {
            write_event(&btn_left_up);
        }
    } else {
        // Allow the event to propagate
        return 0;
    }

    // Block the event
    return 1;
}

void handle_mouse_mode(const struct input_event *event) {
    if (handle_mouse_move(event)) {
        // Block the event
    } else if (handle_mouse_button(event)) {
        // Block the event
    } else {
        // Pass the event through
        write_event(event);
    }
}

void handle_default_mode(const struct input_event *event) {
    static int last_code;
    struct input_event synthetic_event;
    synthetic_event.type = EV_KEY;
    synthetic_event.value = event->value;

    if (modifiers_held.alt) {
        switch (event->code) {
            case KEY_SPACE:
                synthetic_event.code = KEY_ENTER;
                break;
            case KEY_O:
                synthetic_event.code = KEY_UP;
                break;
            case KEY_SEMICOLON:
                synthetic_event.code = KEY_DOWN;
                break;
            case KEY_LEFTBRACE:
                synthetic_event.code = KEY_HOME;
                break;
            case KEY_RIGHTBRACE:
                synthetic_event.code = KEY_END;
                break;
            default:
                synthetic_event.code = 0;
        }
        if (synthetic_event.code != 0) {
            write_event(&alt_up);
            write_event(&synthetic_event);
            if (event->value == INPUT_KEY_UP) {
                write_event(&alt_down);
                last_code = 0;
            } else if (event->value == INPUT_KEY_DOWN) {
                last_code = synthetic_event.code;
            }
        } else {
            write_event(event);
        }
    } else {
        if (last_code != 0) {
            // Release any synthetic keys that were held down. This
            // avoids infinite enters in the case of:
            // alt_down, space_down, alt_up, space_up.
            synthetic_event.code = last_code;
            write_event(&synthetic_event);
            last_code = 0;
        }

        write_event(event);
    }
}

int main(int argc, char *argv[]) {
    int mode_fd = -1;

    // Pass through input and output instantly
    setbuf(stdin, NULL), setbuf(stdout, NULL);

    if (argc > 1) {
        if (strcmp(argv[1], "--help") == 0) {
            fprintf(stdout, "keyboard-mapper /path/to/mode-switcher.fifo\n");
            exit(EXIT_SUCCESS);
        } else {
            mode_fd = setup_fifo(argv[1]);
        }
    }

    struct input_event event;
    while (fread(&event, sizeof(event), 1, stdin) == 1) {
        if (event.type == EV_MSC && event.code == MSC_SCAN) {
            // These are some kind of device specific scancode. I copied this from
            // other interception tools scripts. Perhaps we block this to avoid other programs from
            // bypassing our modifications?
            continue;
        }
        if (event.type != EV_KEY) {
            // Allow through any non keyboard events
            write_event(&event);
            continue;
        }

        if (mode_fd != -1) {
            update_program_mode(mode_fd);
        }

        update_modifiers_held(&event);

        // Debugging line
        /* fprintf(stderr, "%d %d %d %d\n", modifiers_held.alt, event.type, event.code, event.value); */
        if (program_mode == 'm') {
            handle_mouse_mode(&event);
        } else if (program_mode == 'd') {
            handle_default_mode(&event);
        } else {
            // noop mode
            // Don't modify the input at all (aside from blocking MSC_SCAN)
            write_event(&event);
        }
    }
}
