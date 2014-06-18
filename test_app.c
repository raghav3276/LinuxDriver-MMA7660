#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <linux/input.h>

/* Delay in ms to read the next value */
#define DELAY_MILLIS 50

/* Receives 6 events from the driver : ABS_X, ABS_Y, ABS_Z
 * ABS_MT_ORIENTATION & MSC_GESTURE
 */
#define NUM_EVENTS 6

void get_shake_buf(__s8 val, char *tilt_buf)
{
	switch (val) {
	case -1:
//		strcat(tilt_buf, "Shake detection disabled");
		break;
	case 0:
//		strcat(tilt_buf, "No shake detected");
		break;
	case 1:
		strcat(tilt_buf, "Shake");
		break;
	}
}

void get_tilt_buf(__s8 tilt_stat, char *tilt_buf)
{
	strcpy(tilt_buf, "Facing-");
	switch (tilt_stat & 0x03) {
	case 0 : strcat(tilt_buf, "unknown\t");
			  break;
	case 1 : strcat(tilt_buf, "front\t");
			  break;
	case 2 : strcat(tilt_buf, "back \t");
			  break;
	}

	switch ((tilt_stat & 0x1c) >> 2) {
	case 0 : strcat(tilt_buf, "Unknown PoLa    \t");
			  break;
	case 1 : strcat(tilt_buf, "Landscape-left  \t");
			  break;
	case 2 : strcat(tilt_buf, "Landscape-right \t");
			  break;
	case 5 : strcat(tilt_buf, "Potrait-inverted\t");
		      break;
	case 6 : strcat(tilt_buf, "Potrait-normal  \t");
			  break;

	}
}

int main(int argc, char *argv[])
{
	int fd, i;
	int retval;
	__s8 xout, yout, zout;
	__u8 tilt_stat;
	char tilt_buf[128];
	struct input_event accel_event[NUM_EVENTS];

	if (argc != 2) {
		printf("Usage : app_accel /dev/input/eventXX\n");
		return 1;
	}

	fd = open(argv[1], O_RDONLY);
	if (fd < 0) {
		perror("Failed to open the device node : ");
		return errno;
	}

	while (1) {
		retval = read(fd, accel_event, sizeof(struct input_event) * NUM_EVENTS);
		if (retval < sizeof(struct input_event)) {
			perror("Error reading the accelerometer events\n");
			return errno;
		}

		for (i = 0; i < retval / sizeof(struct input_event); i++) {

			if (accel_event[i].type == EV_ABS) {
				switch (accel_event[i].code) {
				case ABS_X:
					xout = accel_event[i].value;
					break;
				case ABS_Y:
					yout = accel_event[i].value;
					break;
				case ABS_Z:
					zout = accel_event[i].value;
					break;
				case ABS_MT_ORIENTATION:
					get_tilt_buf(accel_event[i].value, tilt_buf);
					break;
				}
			}

			else if (accel_event[i].type == EV_MSC &&
						accel_event[i].code == MSC_GESTURE)
				get_shake_buf(accel_event[i].value, tilt_buf);
		}

		printf("X : %3d\t\tY : %3d\t\t Z : %3d\t%s\n", xout, yout, zout, tilt_buf);
		usleep(DELAY_MILLIS * 1000);
	}

	return 0;
}
