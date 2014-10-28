/**
* @file px4_simple_app.c
* Minimal application example for PX4 autopilot.
*/
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <float.h>
#include <fcntl.h>
#include <string.h>
#include <nuttx/config.h>
#include <nuttx/sched.h>
#include <sys/prctl.h>
#include <termios.h>
#include <math.h>
#include <float.h>
#include <uORB/uORB.h>
#include <uORB/topics/parameter_update.h>
#include <uORB/topics/actuator_controls.h>
#include <uORB/topics/actuator_armed.h>
#include <uORB/topics/sensor_combined.h>
#include <uORB/topics/vehicle_attitude.h>
#include <uORB/topics/vehicle_local_position.h>
#include <uORB/topics/vehicle_global_position.h>
#include <uORB/topics/vehicle_gps_position.h>
#include <uORB/topics/vision_position_estimate.h>
#include <uORB/topics/home_position.h>
#include <uORB/topics/optical_flow.h>
#include <mavlink/mavlink_log.h>
#include <poll.h>
#include <systemlib/err.h>
#include <geo/geo.h>
#include <systemlib/visibility.h>
#include <systemlib/systemlib.h>
#include <drivers/drv_hrt.h>

#define CL "\033[K" // clear line

__EXPORT int px4_simple_app_main(int argc, char *argv[]);

int px4_simple_app_main(int argc, char *argv[])
{
		int mavlink_fd;
		mavlink_fd = open(MAVLINK_LOG_DEVICE, 0);
		mavlink_log_info(mavlink_fd, "[inav] started");


		uint16_t attitude_updates = 0;


		/* declare and safely initialize all structs */

//		struct sensor_combined_s sensor;
//		memset(&sensor, 0, sizeof(sensor));

		struct vehicle_attitude_s att;
		memset(&att, 0, sizeof(att));

		/* subscribe */
//		int parameter_update_sub = orb_subscribe(ORB_ID(parameter_update));

//		int sensor_combined_sub = orb_subscribe(ORB_ID(sensor_combined));
		int vehicle_attitude_sub = orb_subscribe(ORB_ID(vehicle_attitude));


	struct pollfd fds[2] = {
			{ .fd = vehicle_attitude_sub, .events = POLLIN },
			{ .fd = 0, .events = POLLIN }
		};


	while (1) {
		int ret = poll(fds, 1, 20); // wait maximal 20 ms = 50 Hz minimum rate

		if (ret < 0) {
		/* poll error */
		mavlink_log_info(mavlink_fd, "[inav] poll error on init");
		continue;

		} else if (ret > 0) {
		/* act on attitude updates */

		/* vehicle attitude */
		orb_copy(ORB_ID(vehicle_attitude), vehicle_attitude_sub, &att);
		attitude_updates++;
		}

		printf("\033[2J");
		printf("\033[H");

		printf(CL "Roll %8.6f\tPitch %8.6f\tYaw %8.6f\n",
				(double)att.roll,(double)att.pitch,(double)att.yaw);

		printf(CL "Vel_R %8.6f\tVel_P %8.6f\tVel_Y %8.6f\n",
				(double)att.rollspeed,(double)att.pitchspeed,(double)att.yawspeed);

		/* Sleep 200 ms waiting for user input five times ~ 1s */
		for (int k = 0; k < 5; k++) {
			char c;

			int ret2 = poll(&fds[1], 1, 0);

			if (ret2 > 0) {

				read(0, &c, 1);

				switch (c) {
				case 0x03: // ctrl-c
				case 0x1b: // esc
				case 'c':
				case 'q':
					return OK;
							/* not reached */
					}
			}

			usleep(40000);
		}
	}

	mavlink_log_info(mavlink_fd, "[inav] stopped");

	return OK;
}
