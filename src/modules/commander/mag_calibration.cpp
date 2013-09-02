/****************************************************************************
 *
 *   Copyright (C) 2013 PX4 Development Team. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name PX4 nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/**
 * @file mag_calibration.cpp
 * Magnetometer calibration routine
 */

#include "mag_calibration.h"
#include "commander_helper.h"
#include "calibration_routines.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <math.h>
#include <float.h>
#include <fcntl.h>
#include <drivers/drv_hrt.h>
#include <uORB/topics/sensor_combined.h>
#include <drivers/drv_mag.h>
#include <drivers/drv_gyro.h>
#include <mavlink/mavlink_log.h>
#include <systemlib/param/param.h>
#include <systemlib/err.h>
#include <mathlib/mathlib.h>

/* oddly, ERROR is not defined for c++ */
#ifdef ERROR
# undef ERROR
#endif
static const int ERROR = -1;

struct rot_lookup
{
    uint16_t roll;
    uint16_t pitch;
    uint16_t yaw;
};

void calculate_rotation_errors(float dt, math::Vector3 mag, math::Vector3 gyro,
	math::Vector3 last_mag, float errors[], const struct rot_lookup rotations[], unsigned n_rotations);

const struct rot_lookup rot_lookup[] =
{
    {  0,   0,   0 },
    {  0,   0,  45 },
    {  0,   0,  90 },
    {  0,   0, 135 },
    {  0,   0, 180 },
    {  0,   0, 225 },
    {  0,   0, 270 },
    {  0,   0, 315 },
    {180,   0,   0 },
    {180,   0,  45 },
    {180,   0,  90 },
    {180,   0, 135 },
    {  0, 180,   0 },
    {180,   0, 225 },
    {180,   0, 270 },
    {180,   0, 315 },
    { 90,   0,   0 },
    { 90,   0,  45 },
    { 90,   0,  90 },
    { 90,   0, 135 },
    {270,   0,   0 },
    {270,   0,  45 },
    {270,   0,  90 },
    {270,   0, 135 },
    {  0,  90,   0 },
    {  0, 270,   0 },
    {  0, 180,  90 },
    {  0, 180, 270 },
    { 90,  90,   0 },
    {180,  90,   0 },
    {270,  90,   0 },
    { 90, 180,   0 },
    {270, 180,   0 },
    { 90, 270,   0 },
    {180, 270,   0 },
    {270, 270,   0 },
    { 90, 180,  90 },
    { 90,   0, 270 }
};

void calculate_rotation_errors(float dt, math::Vector3 mag, math::Vector3 gyro,
	math::Vector3 last_mag, float errors[], const struct rot_lookup rotations[], unsigned n_rotations)
{
	for (unsigned i = 0; i < n_rotations; i++)
	{
		math::EulerAngles e(rotations[i].roll, rotations[i].pitch, rotations[i].yaw);
		if (static_cast<int>(e.getPhi()) % 90 != 0 ||
			static_cast<int>(e.getTheta()) % 90 != 0 ||
			static_cast<int>(e.getPsi()) % 90 != 0) {
			/* ignore all non-90 degree rotations for robustness */
			continue;
		}

		math::Dcm R(e);

		math::Vector3 gyroR = gyro * dt;

		math::Dcm Rdt(math::EulerAngles(gyroR(0), gyroR(1), gyroR(2)));

		math::Vector magR1 = R * mag;
		math::Vector magR2 = R.transpose() * R * mag;
		math::Vector errorVec = (magR1 - magR2);
		errors[i] += errorVec.norm();
	}
}

int do_mag_calibration(int mavlink_fd)
{
	mavlink_log_info(mavlink_fd, "please put the system in a rest position and wait.");

	int sub_mag = orb_subscribe(ORB_ID(sensor_mag));
	struct mag_report mag;
	int sub_gyro = orb_subscribe(ORB_ID(sensor_gyro));
	struct gyro_report gyro;

	/* 45 seconds */
	uint64_t calibration_interval = 20 * 1000 * 1000;

	/* maximum 2000 values */
	const unsigned int calibration_maxcount = 1000;
	unsigned int calibration_counter = 0;

	/* limit update rate to get equally spaced measurements over time (in ms) */
	orb_set_interval(sub_mag, (calibration_interval / 1000) / calibration_maxcount);

	int fd = open(MAG_DEVICE_PATH, O_RDONLY);

	/* erase old calibration */
	struct mag_scale mscale_null = {
		0.0f,
		1.0f,
		0.0f,
		1.0f,
		0.0f,
		1.0f,
	};

	if (OK != ioctl(fd, MAGIOCSSCALE, (long unsigned int)&mscale_null)) {
		warn("WARNING: failed to set scale / offsets for mag");
		mavlink_log_info(mavlink_fd, "failed to set scale / offsets for mag");
	}

	/* calibrate range */
	if (OK != ioctl(fd, MAGIOCCALIBRATE, fd)) {
		warnx("failed to calibrate scale");
	}

	close(fd);

	mavlink_log_info(mavlink_fd, "mag cal progress <20> percent");

	/* calibrate offsets */

	// uint64_t calibration_start = hrt_absolute_time();

	uint64_t axis_deadline = hrt_absolute_time();
	uint64_t calibration_deadline = hrt_absolute_time() + calibration_interval;

	const char axislabels[3] = { 'X', 'Y', 'Z'};
	int axis_index = -1;

	float *x = (float *)malloc(sizeof(float) * calibration_maxcount);
	float *y = (float *)malloc(sizeof(float) * calibration_maxcount);
	float *z = (float *)malloc(sizeof(float) * calibration_maxcount);

	/* build rotations list */
	float errors[sizeof(rot_lookup)/sizeof(rot_lookup[0])];
	orb_copy(ORB_ID(sensor_mag), sub_mag, &mag);
	math::Vector3 magVLast(mag.x, mag.y, mag.z);

	if (x == NULL || y == NULL || z == NULL) {
		warnx("mag cal failed: out of memory");
		mavlink_log_info(mavlink_fd, "mag cal failed: out of memory");
		warnx("x:%p y:%p z:%p\n", x, y, z);
		return ERROR;
	}

	mavlink_log_info(mavlink_fd, "scale calibration completed, dynamic calibration starting..");

	unsigned poll_errcount = 0;
	uint64_t last_time = hrt_absolute_time();

	while (hrt_absolute_time() < calibration_deadline &&
	       calibration_counter < calibration_maxcount) {

		/* wait blocking for new data */
		struct pollfd fds[1];
		fds[0].fd = sub_mag;
		fds[0].events = POLLIN;

		/* user guidance */
		if (hrt_absolute_time() >= axis_deadline &&
		    axis_index < 3) {

			axis_index++;

			mavlink_log_info(mavlink_fd, "please rotate in a figure 8 or around %c axis.", axislabels[axis_index]);
			tune_neutral();

			axis_deadline += calibration_interval / 3;
		}

		if (!(axis_index < 3)) {
			break;
		}

		int poll_ret = poll(fds, 1, 1000);

		if (poll_ret > 0) {
			orb_copy(ORB_ID(sensor_mag), sub_mag, &mag);
			orb_copy(ORB_ID(sensor_gyro), sub_gyro, &gyro);

			x[calibration_counter] = mag.x;
			y[calibration_counter] = mag.y;
			z[calibration_counter] = mag.z;

			float dt = (hrt_absolute_time() - last_time) / 1e3f / 1e3f;
			last_time = hrt_absolute_time();

			math::Vector3 magV(mag.x, mag.y, mag.z);
			math::Vector3 gyroV(gyro.x, gyro.y, gyro.z);

			calculate_rotation_errors(dt, magV, gyroV, magVLast, errors, rot_lookup, sizeof(rot_lookup)/sizeof(rot_lookup[0]));

			calibration_counter++;
			if (calibration_counter % (calibration_maxcount / 20) == 0)
				mavlink_log_info(mavlink_fd, "mag cal progress <%u> percent", 20 + (calibration_counter * 50) / calibration_maxcount);


		} else {
			poll_errcount++;
		}

		if (poll_errcount > 1000) {
			mavlink_log_emergency(mavlink_fd, "ERROR: Failed reading mag sensor");
			close(sub_mag);
			close(sub_gyro);
			free(x);
			free(y);
			free(z);

			return ERROR;
		}


	}

	float sphere_x;
	float sphere_y;
	float sphere_z;
	float sphere_radius;

	mavlink_log_info(mavlink_fd, "mag cal progress <70> percent");
	sphere_fit_least_squares(x, y, z, calibration_counter, 100, 0.0f, &sphere_x, &sphere_y, &sphere_z, &sphere_radius);
	mavlink_log_info(mavlink_fd, "mag cal progress <80> percent");

	free(x);
	free(y);
	free(z);

	/* find the smallest error */
	float min_error = FLT_MAX;
	int min_error_index = 0;

	for (unsigned i = 0; i < sizeof(rot_lookup)/sizeof(rot_lookup[0]); i++) {
		if (errors[i] < min_error) {
			min_error_index = i;
			min_error = errors[i];
		}
	}
	warnx("detected autopilot to mag rotation: #%u", min_error_index);
	mavlink_log_info(mavlink_fd, "detected autopilot to mag rotation: #%u", min_error_index);

	if (isfinite(sphere_x) && isfinite(sphere_y) && isfinite(sphere_z)) {

		fd = open(MAG_DEVICE_PATH, 0);

		struct mag_scale mscale;

		if (OK != ioctl(fd, MAGIOCGSCALE, (long unsigned int)&mscale))
			warn("WARNING: failed to get scale / offsets for mag");

		mscale.x_offset = sphere_x;
		mscale.y_offset = sphere_y;
		mscale.z_offset = sphere_z;

		if (OK != ioctl(fd, MAGIOCSSCALE, (long unsigned int)&mscale))
			warn("WARNING: failed to set scale / offsets for mag");

		close(fd);

		/* announce and set rotation */
		if (param_set(param_find("SENS_MAG_EXT_ROT"), &(min_error_index))) {
			warnx("Setting ext mag rotation failed!\n");
		}

		/* announce and set new offset */

		if (param_set(param_find("SENS_MAG_XOFF"), &(mscale.x_offset))) {
			warnx("Setting X mag offset failed!\n");
		}

		if (param_set(param_find("SENS_MAG_YOFF"), &(mscale.y_offset))) {
			warnx("Setting Y mag offset failed!\n");
		}

		if (param_set(param_find("SENS_MAG_ZOFF"), &(mscale.z_offset))) {
			warnx("Setting Z mag offset failed!\n");
		}

		if (param_set(param_find("SENS_MAG_XSCALE"), &(mscale.x_scale))) {
			warnx("Setting X mag scale failed!\n");
		}

		if (param_set(param_find("SENS_MAG_YSCALE"), &(mscale.y_scale))) {
			warnx("Setting Y mag scale failed!\n");
		}

		if (param_set(param_find("SENS_MAG_ZSCALE"), &(mscale.z_scale))) {
			warnx("Setting Z mag scale failed!\n");
		}

		mavlink_log_info(mavlink_fd, "mag cal progress <90> percent");

		/* auto-save to EEPROM */
		int save_ret = param_save_default();

		if (save_ret != 0) {
			warn("WARNING: auto-save of params to storage failed");
			mavlink_log_info(mavlink_fd, "FAILED storing calibration");
			close(sub_mag);
			return ERROR;
		}

		warnx("\tscale: %.6f %.6f %.6f\n         \toffset: %.6f %.6f %.6f\nradius: %.6f GA\n",
		       (double)mscale.x_scale, (double)mscale.y_scale, (double)mscale.z_scale,
		       (double)mscale.x_offset, (double)mscale.y_offset, (double)mscale.z_offset, (double)sphere_radius);

		char buf[52];
		sprintf(buf, "mag off: x:%.2f y:%.2f z:%.2f Ga", (double)mscale.x_offset,
			(double)mscale.y_offset, (double)mscale.z_offset);
		mavlink_log_info(mavlink_fd, buf);

		sprintf(buf, "mag scale: x:%.2f y:%.2f z:%.2f", (double)mscale.x_scale,
			(double)mscale.y_scale, (double)mscale.z_scale);
		mavlink_log_info(mavlink_fd, buf);

		mavlink_log_info(mavlink_fd, "magnetometer calibration completed");
		mavlink_log_info(mavlink_fd, "mag cal progress <100> percent");

		close(sub_mag);
		return OK;
		/* third beep by cal end routine */

	} else {
		mavlink_log_info(mavlink_fd, "mag calibration FAILED (NaN in sphere fit)");
		close(sub_mag);
		return ERROR;
	}
}
