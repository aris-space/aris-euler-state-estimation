#include "../Util/math_utils.h"
#include "env.h"
#include "state_est_const.h"
#include "kf.h"
#include "flight_phase_detection.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdbool.h>

#ifndef STATE_EST_H_
#define STATE_EST_H_

typedef struct extrapolation_rolling_memory_t {
    int memory_length;
    float timestamps[MAX_LENGTH_ROLLING_MEMORY];
    float measurements[MAX_LENGTH_ROLLING_MEMORY];
    float noise_stdev;
    double polyfit_coeffs[EXTRAPOLATION_POLYFIT_DEGREE+1]; /* array size needs to be the degree of the polyfit plus 1 */
} extrapolation_rolling_memory_t;

typedef struct state_est_state_t {
    state_est_data_t state_est_data;
    state_est_meas_t state_est_meas;
    state_est_meas_t state_est_meas_prior;
    kf_state_t kf_state;
    env_t env;
    flight_phase_detection_t flight_phase_detection;
    extrapolation_rolling_memory_t baro_roll_mem;
} state_est_state_t;

void reset_state_est_state(float p_g, float T_g, state_est_state_t *state_est_state);

void state_est_step(timestamp_t t, state_est_state_t *state_est_state, bool bool_detect_flight_phase);

void update_state_est_data(state_est_data_t *state_est_data, kf_state_t *kf_state);

void process_measurements(timestamp_t t, kf_state_t *kf_state, state_est_meas_t *state_est_meas, state_est_meas_t *state_est_meas_prior,
                        env_t *env, extrapolation_rolling_memory_t *extrapolation_rolling_memory);

void select_noise_models(kf_state_t *kf_state, flight_phase_detection_t *flight_phase_detection, env_t *env,
						extrapolation_rolling_memory_t *extrapolation_rolling_memory);

void sensor_elimination_by_stdev(int n, float measurements[n], bool measurement_active[n]);
void sensor_elimination_by_extrapolation(timestamp_t t, int n, float measurements[n], bool measurement_active[n], 
						extrapolation_rolling_memory_t *extrapolation_rolling_memory);

#endif