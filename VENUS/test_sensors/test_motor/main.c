#include <libpynq.h>
#include <stepper.h>

#define STEPS_PER_ROTATION 1600
#define DEFAULT_SPEED      3072

void wait_for_move(void) {
    while (stepper_steps_done());
    while (!stepper_steps_done());
}

void move_forward(int rotations) {
    stepper_set_speed(DEFAULT_SPEED, DEFAULT_SPEED);
    stepper_steps(rotations * STEPS_PER_ROTATION, rotations * STEPS_PER_ROTATION);
    wait_for_move();
}

void move_backward(int rotations) {
    stepper_set_speed(DEFAULT_SPEED, DEFAULT_SPEED);
    stepper_steps(-rotations * STEPS_PER_ROTATION, -rotations * STEPS_PER_ROTATION);
    wait_for_move();
}

void turn_left(int rotations) {
    stepper_set_speed(DEFAULT_SPEED, DEFAULT_SPEED);
    stepper_steps(-rotations * STEPS_PER_ROTATION, rotations * STEPS_PER_ROTATION);
    wait_for_move();
}

void turn_right(int rotations) {
    stepper_set_speed(DEFAULT_SPEED, DEFAULT_SPEED);
    stepper_steps(rotations * STEPS_PER_ROTATION, -rotations * STEPS_PER_ROTATION);
    wait_for_move();
}

void stop(void) {
    stepper_reset();
}

int main(void) {
    pynq_init();
    stepper_init();
    stepper_enable();

    turn_left(100);

    stepper_destroy();
    pynq_destroy();
    return 0;
}