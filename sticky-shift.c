/**
 * Implements sticky shift with timeout. Use instead of XAaccess (xkbset) to just affect
 * left shift, and to automatically time out after keyboard inactivity. Also affects the
 * console as a bonus.
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <linux/input.h>
#include <pthread.h>

#define INPUT_KEY_DOWN 1
#define INPUT_KEY_UP 0
#define INPUT_KEY_REPEAT 2
#define LOCK_TIMEOUT_MS 3000

// State machine, 0 == not shifting, 1 = next key shifted, 2 = holding shift in mode 1
// 2 = caps lock mode
int shift_lock_state = 0;
pthread_mutex_t shift_lock_state_mutex;
// The time the shift key was last pressed down
struct timespec shift_down_time;
struct timespec state_changed_time;
struct timespec last_key_event_time;
pthread_t timeout_thread;
pthread_mutex_t last_key_event_time_mutex;

void write_event(const struct input_event *event) {
    // Debugging line
    /* fprintf(stderr, "e: %d %d %d\n", event->type, event->code, event->value); */
    if (fwrite(event, sizeof(struct input_event), 1, stdout) != 1) {
        exit(EXIT_FAILURE);
    }
}

void write_shift_up() {
    struct input_event synthetic_event;
    synthetic_event.type = EV_KEY;
    synthetic_event.code = KEY_LEFTSHIFT;
    synthetic_event.value = INPUT_KEY_UP;

    write_event(&synthetic_event);
}

long time_diff_ms(const struct timespec *start, const struct timespec *end) {
    long rtn = 1000*(end->tv_sec - start->tv_sec);

    rtn += (end->tv_nsec - start->tv_nsec) / 1000000;

    return rtn;
}

void change_lock_state(int new_state) {
    if(new_state == 0 && shift_lock_state != 0) {
        write_shift_up();
    }

    shift_lock_state = new_state;
    clock_gettime(CLOCK_MONOTONIC, &state_changed_time);
}

void* timeout_lock(void *arg) {
    char time_expired;
    struct timespec now;

    while(1) {
        pthread_mutex_lock(&shift_lock_state_mutex);
        if (shift_lock_state > 0) {
            clock_gettime(CLOCK_MONOTONIC, &now);
            pthread_mutex_lock(&last_key_event_time_mutex);
            time_expired = time_diff_ms(&last_key_event_time, &now) > LOCK_TIMEOUT_MS;
            pthread_mutex_unlock(&last_key_event_time_mutex);

            if (time_expired) {
                change_lock_state(0);
            }
        }
        pthread_mutex_unlock(&shift_lock_state_mutex);

        sleep(1);
    }
}

void handle_shift_event(const struct input_event *event) {
    if (event->value == INPUT_KEY_REPEAT) {
        // Block shift key repeats
        return;
    }

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    if (event->value == INPUT_KEY_DOWN) {
        memcpy(&shift_down_time, &now, sizeof(struct timespec));
    }

    if (shift_lock_state == 0) {
        if (event->value == INPUT_KEY_DOWN) {
            write_event(event);
        } else if (time_diff_ms(&shift_down_time, &now) < 150) {
            change_lock_state(1);
            // And eat the shift up event
        } else {
            // Not a shift tap, let through the event
            write_event(event);
        }
    } else if (shift_lock_state == 1 || shift_lock_state == 2) {
        if (event->value == INPUT_KEY_DOWN) {
            // Eat the event
            change_lock_state(2);
        } else if (time_diff_ms(&shift_down_time, &now) < 150 && time_diff_ms(&state_changed_time, &now) < 300) {
            // Double tap
            change_lock_state(3);
            // Eat the event
        } else {
            // Release the lock (writes event automatically)
            change_lock_state(0);
        }
    } else if (shift_lock_state == 3) {
        if (event->value == INPUT_KEY_DOWN) {
            // Eat the event
        } else {
            // Release the lock (writes event automatically)
            change_lock_state(0);
        }
    }
}

int main(int argc, char *argv[]) {
    // Pass through input and output instantly
    setbuf(stdin, NULL), setbuf(stdout, NULL);

    if (pthread_mutex_init(&last_key_event_time_mutex, NULL) != 0) {
        printf("Last key event mutex init failed\n");
        return 1;
    }

    if (pthread_mutex_init(&shift_lock_state_mutex, NULL) != 0) {
        printf("Shift lock state mutex init failed\n");
        return 1;
    }

    if (pthread_create(&timeout_thread, NULL, &timeout_lock, NULL) != 0) {
        printf("Timeout thread creation failed\n");
        return 1;
    }

    struct input_event event;
    while (fread(&event, sizeof(event), 1, stdin) == 1) {
        if (event.type != EV_KEY) {
            // Allow through any non keyboard events
            write_event(&event);
            continue;
        }

        // Record keyboard activity for an idle timeout
        pthread_mutex_lock(&last_key_event_time_mutex);
        clock_gettime(CLOCK_MONOTONIC, &last_key_event_time);
        pthread_mutex_unlock(&last_key_event_time_mutex);

        if (event.code == KEY_LEFTSHIFT) {
            pthread_mutex_lock(&shift_lock_state_mutex);
            handle_shift_event(&event);
            pthread_mutex_unlock(&shift_lock_state_mutex);
        } else {
            write_event(&event);

            pthread_mutex_lock(&shift_lock_state_mutex);
            if (shift_lock_state == 1) {
                // Single key sticky shift, release shift after writing the key
                change_lock_state(0);
            }
            pthread_mutex_unlock(&shift_lock_state_mutex);
        }
    }
}
