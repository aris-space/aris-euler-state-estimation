#ifndef STATE_EST_CONFIG_H_
#define STATE_EST_CONFIG_H_

#define EULER_REC 1

#define STATE_ESTIMATION_TYPE 1 // 1 = 1D state estimation, 2 = 3D state estimation
#define STATE_ESTIMATION_FREQUENCY 40
#define NUM_IMU 2
#define NUM_BARO 2

/* flight phase detection config */
#define FPD_SAFETY_COUNTER_THRESH 4 // how many (non-consecutive) measurements are required to detect an event and switch the flight phase
#define FPD_LIFTOFF_ACC_THRESH 20 // acceleration threshold to detect lift-off and enter thrusting flight phase [m/s^2]
#define FPD_LIFTOFF_ALT_THRESH 150 // altitude above ground level to detect lift-off and enter thrusting flight phase [m]
#define FPD_MAIN_DESCENT_ALT_THRESH 400 // altitude above ground level to enter main descent and activate the main parachute in [m]
#define FPD_BALLISTIC_ACTIVE false // wether to use ballistic flight phase
#define FPD_BALLISTIC_VEL_THRESH_HIGH 75 // upper velocity threshold to enter ballistic flight phase in [m/s]
#define FPD_BALLISTIC_VEL_THRESH_LOW 60 // lower velocity threshold to leave ballistic flight phase in [m/s]
#define FPD_TOUCHDOWN_SAFETY_COUNTER_THRESH 20 // how many (non-consecutive) measurements are required to detect touchdown
#define FPD_TOUCHDOWN_ALT_THRESH 400 // altitude above ground level to assume touchdown (together with velocity threshold) in [m]
#define FPD_TOUCHDOWN_VEL_THRESH 2 // velocity threshold to assume touchdown (together with altitude threshold) in [m/s]

/* use moving average to determine velocity instead of state estimation during main and drogue descent */
#define USE_STATE_EST_DESCENT false // wether to use the state estimation during drogue and main descent
#define MAX_LENGTH_MOVING_AVERAGE 10

/* sensor elimination by extrapolation config */
#define USE_SENSOR_ELIMINATION_BY_EXTRAPOLATION false // set to true to activate sensor elimination by extrapolation for barometer and temperature [m]
#define MAX_LENGTH_ROLLING_MEMORY 18
#define EXTRAPOLATION_POLYFIT_DEGREE 2

#endif