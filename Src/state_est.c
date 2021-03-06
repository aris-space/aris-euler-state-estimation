#include "../Inc/state_est.h"

void reset_state_est_state(float p_g, float T_g, state_est_state_t *state_est_state) {
    reset_flight_phase_detection(&state_est_state->flight_phase_detection);

    memset(&state_est_state->state_est_data, 0, sizeof(state_est_state->state_est_data));
    memset(&state_est_state->state_est_meas, 0, sizeof(state_est_state->state_est_meas));
    memset(&state_est_state->state_est_meas_prior, 0, sizeof(state_est_state->state_est_meas_prior));
    memset(&state_est_state->processed_measurements, 0, sizeof(state_est_state->processed_measurements));

    #if STATE_ESTIMATION_TYPE == 2
        init_sensor_transformation_matrix(state_est_state);
    #endif

    init_env(&state_est_state->env);
    calibrate_env(&state_est_state->env, p_g, T_g);
    update_env(&state_est_state->env, T_g);

	reset_kf_state(&state_est_state->kf_state);
    update_state_est_data(state_est_state);

    #if defined(USE_SENSOR_ELIMINATION_BY_EXTRAPOLATION) && USE_SENSOR_ELIMINATION_BY_EXTRAPOLATION == true
        memset(&state_est_state->baro_roll_mem, 0, sizeof(state_est_state->baro_roll_mem));
    #endif

    #if STATE_ESTIMATION_TYPE == 1 && USE_STATE_EST_DESCENT == false
        memset(&state_est_state->altitude_mav_mem, 0, sizeof(state_est_state->altitude_mav_mem));
    #endif

	select_noise_models(state_est_state);
}

void state_est_step(timestamp_t t, state_est_state_t *state_est_state, bool bool_detect_flight_phase) {
    /* process measurements */
	process_measurements(t, state_est_state);

	/* select noise models (dependent on detected flight phase and updated temperature in environment) */
	select_noise_models(state_est_state);
	
	kf_prediction(&state_est_state->kf_state);

    #if STATE_ESTIMATION_TYPE == 2
        float quarternion_world[4] = {0};
        for (int i = 0; i < 4; i++) {
            quarternion_world[i] = state_est_state->kf_state.x_priori[6+i];
        }
        normalize_quarternion(quarternion_world);
        for (int i = 0; i < 4; i++) {
            state_est_state->kf_state.x_priori[6+i] = quarternion_world[i];
        }
    #endif
    
	if (state_est_state->kf_state.num_z_active > 0) {
		select_kf_observation_matrices(&state_est_state->kf_state);
		kf_update(&state_est_state->kf_state);
	} else {
		memcpy(&state_est_state->kf_state.x_est, &state_est_state->kf_state.x_priori, sizeof(state_est_state->kf_state.x_priori));
	}

	update_state_est_data(state_est_state);

    #if STATE_ESTIMATION_TYPE == 1 && USE_STATE_EST_DESCENT == false
        /* during drogue and main descent, the 1D state estimation might work badly,
           thus we are computing the altitude and vertical velocity solely from the barometric data */
        
    	if ((state_est_state->flight_phase_detection.flight_phase == DROGUE_DESCENT || 
            state_est_state->flight_phase_detection.flight_phase == MAIN_DESCENT) && 
            state_est_state->state_est_data.altitude_raw_active == true){
        
            int alt_mav_mem_length = state_est_state->altitude_mav_mem.memory_length;
            float alt_mav_delta = state_est_state->altitude_mav_mem.avg_values[0] - state_est_state->altitude_mav_mem.avg_values[alt_mav_mem_length-1];
            float alt_mav_dt = (float)(state_est_state->altitude_mav_mem.timestamps[0] - state_est_state->altitude_mav_mem.timestamps[alt_mav_mem_length-1]) / 1000;

            float velocity = 0;
            if (alt_mav_mem_length > 1){
                velocity = alt_mav_delta / alt_mav_dt;
            }

			state_est_state->state_est_data.position_world[2] = state_est_state->state_est_data.altitude_raw;
			state_est_state->state_est_data.velocity_rocket[0] = (int32_t)(velocity * 1000);
			state_est_state->state_est_data.velocity_world[2] = (int32_t)(velocity * 1000);
			state_est_state->state_est_data.acceleration_rocket[0] = 0;
			state_est_state->state_est_data.acceleration_world[2] = 0;
			state_est_state->state_est_data.mach_number = (int32_t)(mach_number(&state_est_state->env, velocity) * 1000000);
    	}
    #endif

    if (bool_detect_flight_phase){
        detect_flight_phase(t, &state_est_state->flight_phase_detection, &state_est_state->state_est_data);
    }

	/* set measurement prior to measurements from completed state estimation step */
	memcpy(&state_est_state->state_est_meas_prior, &state_est_state->state_est_meas, sizeof(state_est_state->state_est_meas));
}

void update_state_est_data(state_est_state_t *state_est_state) {
    #if STATE_ESTIMATION_TYPE == 1
        state_est_state->state_est_data.position_world[2] = (int32_t)(state_est_state->kf_state.x_est[0] * 1000);
        state_est_state->state_est_data.velocity_rocket[0] = (int32_t)(state_est_state->kf_state.x_est[1] * 1000);
        state_est_state->state_est_data.velocity_world[2] = (int32_t)(state_est_state->kf_state.x_est[1] * 1000);
        state_est_state->state_est_data.acceleration_rocket[0] = (int32_t)(state_est_state->kf_state.u[0] * 1000);
        state_est_state->state_est_data.acceleration_world[2] = (int32_t)(state_est_state->kf_state.u[0] * 1000);
        state_est_state->state_est_data.mach_number = (int32_t)(mach_number(&state_est_state->env, state_est_state->kf_state.x_est[1]) * 1000000);
    #elif STATE_ESTIMATION_TYPE == 2
        float velocity_world[3] = {state_est_state->kf_state.x_est[3], state_est_state->kf_state.x_est[4], state_est_state->kf_state.x_est[5]};
        float acceleration_world[3] = {state_est_state->kf_state.u[0], state_est_state->kf_state.u[1], state_est_state->kf_state.u[2]};
        float quarternion_world[4] = {state_est_state->kf_state.x_est[6], state_est_state->kf_state.x_est[7], state_est_state->kf_state.x_est[8], state_est_state->kf_state.x_est[9]};

        float velocity_rocket[3] = {0};
        vec_world_to_body_rotation(quarternion_world, velocity_world, velocity_rocket);

        float acceleration_rocket[3] = {0};
        vec_world_to_body_rotation(quarternion_world, acceleration_world, acceleration_rocket);

        for (int i = 0; i < 3; i++) {
            state_est_state->state_est_data.position_world[i] = (int32_t)(state_est_state->kf_state.x_est[i] * 1000);
            state_est_state->state_est_data.velocity_world[i] = (int32_t)(velocity_world[i] * 1000);
            state_est_state->state_est_data.velocity_rocket[i] = (int32_t)(velocity_rocket[i] * 1000);
            state_est_state->state_est_data.acceleration_world[i] = (int32_t)(acceleration_world[i] * 1000);
            state_est_state->state_est_data.acceleration_rocket[i] = (int32_t)(acceleration_rocket[i] * 1000);
        }

        for (int i = 0; i < 4; i++) {
            state_est_state->state_est_data.quarternion_world[i] = (int32_t)(quarternion_world[i] * 1000000);
            state_est_state->state_est_data.quarternion_dot_world[i] = (int32_t)(state_est_state->kf_state.u[3+i] * 1000000);
        }        
    #endif
}

void process_measurements(timestamp_t t, state_est_state_t *state_est_state) {
    /* barometer */
    float temp_meas[NUM_BARO];
    bool temp_meas_active[NUM_BARO];
    for (int i = 0; i < NUM_BARO; i++){
        if (state_est_state->state_est_meas.baro_data[i].ts > state_est_state->state_est_meas_prior.baro_data[i].ts) {
            state_est_state->kf_state.z[i] = state_est_state->state_est_meas.baro_data[i].pressure;
            state_est_state->kf_state.z_active[i] = true;

            temp_meas[i] = state_est_state->state_est_meas.baro_data[i].temperature;
            temp_meas_active[i] = true;

            /* deactivate all barometer measurements if we are transsonic or supersonic */
            if (state_est_state->flight_phase_detection.mach_regime != SUBSONIC) {
                state_est_state->kf_state.z_active[i] = false;
            }

            /* deactivate all barometer measurements during control phase if required because of dynamic pressure */
            #if defined(USE_BARO_IN_CONTROL_PHASE) && USE_BARO_IN_CONTROL_PHASE == false
                if (state_est_state->flight_phase_detection.flight_phase == CONTROL || 
                    (state_est_state->flight_phase_detection.flight_phase == BIAS_RESET && 
                        state_est_state->state_est_meas.airbrake_extension > BIAS_RESET_AIRBRAKE_EXTENSION_THRESH)) {
                    state_est_state->kf_state.z_active[i] = false;
                }
            #endif
        } else {
            state_est_state->kf_state.z[i] = 0;
            state_est_state->kf_state.z_active[i] = false;

            temp_meas[i] = 0;
            temp_meas_active[i] = false;
        }
    }

    /* IMU */
    #if STATE_ESTIMATION_TYPE == 1
        float acc_x_meas[NUM_IMU];
        bool acc_x_meas_active[NUM_IMU];
        for (int i = 0; i < NUM_IMU; i++){
            if (state_est_state->state_est_meas.imu_data[i].ts > state_est_state->state_est_meas_prior.imu_data[i].ts) {
                acc_x_meas[i] = state_est_state->state_est_meas.imu_data[i].acc_x;
                acc_x_meas_active[i] = true;
            } else {
                acc_x_meas[i] = 0;
                acc_x_meas_active[i] = false;
            }
        }
    #elif STATE_ESTIMATION_TYPE == 2
        float acc_x_meas[NUM_IMU], acc_y_meas[NUM_IMU], acc_z_meas[NUM_IMU];
        bool acc_x_meas_active[NUM_IMU], acc_y_meas_active[NUM_IMU], acc_z_meas_active[NUM_IMU];

        float gyro_x_meas[NUM_IMU], gyro_y_meas[NUM_IMU], gyro_z_meas[NUM_IMU];
        bool gyro_x_meas_active[NUM_IMU], gyro_y_meas_active[NUM_IMU], gyro_z_meas_active[NUM_IMU];
        
        for (int i = 0; i < NUM_IMU; i++){
            if (state_est_state->state_est_meas.imu_data[i].ts > state_est_state->state_est_meas_prior.imu_data[i].ts) {
                /* acceleration of moving body: S = sensor coordinate system, C = rocket body coordinate system at CoG */
                /* S_a_C = C_SC * (C_a_C + C_omega_C x C_r_CS)
                   C_a_C = C_CS * S_a_S - C_omega_C x C_r_CS */

                // acceleration of sensor in sensor coordinate system
                float S_a_S[3] = {state_est_state->state_est_meas.imu_data[i].acc_x, 
                                  state_est_state->state_est_meas.imu_data[i].acc_y, 
                                  state_est_state->state_est_meas.imu_data[i].acc_z};
                float S_omega_S[3] = {state_est_state->state_est_meas.imu_data[i].gyro_x, 
                                      state_est_state->state_est_meas.imu_data[i].gyro_y, 
                                      state_est_state->state_est_meas.imu_data[i].gyro_z};
                                      
                float C_omega_C[3] = {0};
                matvecprod(3, 3, state_est_state->state_est_meas.imu_data[i].R_CS, S_omega_S, C_omega_C, true);

                float C_CS_mult_S_a_S[3] = {0};
                matvecprod(3, 3, state_est_state->state_est_meas.imu_data[i].R_CS, S_a_S, C_CS_mult_S_a_S, true);
                float C_omega_C_cross_C_r_CS[3] = {0};
                veccrossprod(C_omega_C, state_est_state->state_est_meas.imu_data[i].C_r_CS, C_omega_C_cross_C_r_CS);
                float C_omega_C_cross_C_omega_C_cross_C_r_CS[3] = {0};
                veccrossprod(C_omega_C, C_omega_C_cross_C_r_CS, C_omega_C_cross_C_omega_C_cross_C_r_CS);
                float C_a_C[3] = {0};
                vecsub(3, C_CS_mult_S_a_S, C_omega_C_cross_C_omega_C_cross_C_r_CS, C_a_C);

                acc_x_meas[i] = C_a_C[0];
                acc_y_meas[i] = C_a_C[1];
                acc_z_meas[i] = C_a_C[2];
                acc_x_meas_active[i] = true;
                acc_y_meas_active[i] = true;
                acc_z_meas_active[i] = true;

                gyro_x_meas[i] = C_omega_C[0];
                gyro_y_meas[i] = C_omega_C[1];
                gyro_z_meas[i] = C_omega_C[2];
                gyro_x_meas_active[i] = true;
                gyro_y_meas_active[i] = true;
                gyro_z_meas_active[i] = true;
            } else {
                acc_x_meas[i] = 0;
                acc_y_meas[i] = 0;
                acc_z_meas[i] = 0;
                acc_x_meas_active[i] = false;
                acc_y_meas_active[i] = false;
                acc_z_meas_active[i] = false;

                gyro_x_meas[i] = 0;
                gyro_y_meas[i] = 0;
                gyro_z_meas[i] = 0;
                gyro_x_meas_active[i] = false;
                gyro_y_meas_active[i] = false;
                gyro_z_meas_active[i] = false;
            }
        }
    #endif

    /* eliminate barometer measurements */
    #if defined(USE_SENSOR_ELIMINATION_BY_EXTRAPOLATION) && USE_SENSOR_ELIMINATION_BY_EXTRAPOLATION == true
        if (state_est_state->baro_roll_mem.memory_length < MAX_LENGTH_ROLLING_MEMORY) {
            sensor_elimination_by_stdev(NUMBER_MEASUREMENTS, state_est_state->kf_state.z, state_est_state->kf_state.z_active);
        }
        sensor_elimination_by_extrapolation(t, NUMBER_MEASUREMENTS, state_est_state->kf_state.z, state_est_state->kf_state.z_active, &state_est_state->baro_roll_mem);
    #else
        sensor_elimination_by_stdev(NUMBER_MEASUREMENTS, state_est_state->kf_state.z, state_est_state->kf_state.z_active);
    #endif

    /* eliminate temperature measurements */
    sensor_elimination_by_stdev(NUM_BARO, temp_meas, temp_meas_active);

    /* eliminate imu measurements */
    sensor_elimination_by_stdev(NUM_IMU, acc_x_meas, acc_x_meas_active);
    #if STATE_ESTIMATION_TYPE == 2
        sensor_elimination_by_stdev(NUM_IMU, acc_y_meas, acc_y_meas_active);
        sensor_elimination_by_stdev(NUM_IMU, acc_z_meas, acc_z_meas_active);
        sensor_elimination_by_stdev(NUM_IMU, gyro_x_meas, gyro_x_meas_active);
        sensor_elimination_by_stdev(NUM_IMU, gyro_y_meas, gyro_y_meas_active);
        sensor_elimination_by_stdev(NUM_IMU, gyro_z_meas, gyro_z_meas_active);
    #endif

    /* update num_z_active */
    state_est_state->kf_state.num_z_active = 0;
    /* take the average of the temperature measurement  */
    float temp_meas_mean = 0;
    int num_temp_meas_active = 0;
    
    for (int i = 0; i < NUMBER_MEASUREMENTS; i++){
        if (state_est_state->kf_state.z_active[i]){
            state_est_state->kf_state.num_z_active += 1;
        }
    }
    for (int i = 0; i < NUM_BARO; i++){
        if (temp_meas[i]) {
            temp_meas_mean += temp_meas[i];
            num_temp_meas_active += 1;
        }
    }

    /* take the average of the active accelerometers as the state estimation input */
    float u_rocket[NUMBER_INPUTS] = {0};
    float num_u_active[NUMBER_INPUTS] = {0};
    for (int i = 0; i < NUM_IMU; i++){
        if (acc_x_meas_active[i]) {
            u_rocket[0] += acc_x_meas[i];
            num_u_active[0] += 1;
        }

        #if STATE_ESTIMATION_TYPE == 2
            if (acc_y_meas_active[i]) {
                u_rocket[1] += acc_y_meas[i];
                num_u_active[1] += 1;
            }
            if (acc_z_meas_active[i]) {
                u_rocket[2] += acc_z_meas[i];
                num_u_active[2] += 1;
            }

            if (gyro_x_meas_active[i]) {
                u_rocket[3] += gyro_x_meas[i];
                num_u_active[3] += 1;
            }
            if (gyro_y_meas_active[i]) {
                u_rocket[4] += gyro_y_meas[i];
                num_u_active[4] += 1;
            }
            if (gyro_z_meas_active[i]) {
                u_rocket[5] += gyro_z_meas[i];
                num_u_active[5] += 1;
            }
        #endif
    }
    for (int i = 0; i < NUMBER_INPUTS; i++){
        if (num_u_active[i] > 0){
            u_rocket[i] /= num_u_active[i];
        }
    }

    #if STATE_ESTIMATION_TYPE == 1
        state_est_state->kf_state.u[0] = u_rocket[0] - GRAVITATION;

    #elif STATE_ESTIMATION_TYPE == 2
        float acc_rocket[3] = {u_rocket[0], u_rocket[1], u_rocket[2]};
        float gyro_rocket[3] = {u_rocket[3], u_rocket[4], u_rocket[5]};
        float acc_world[3], gyro_world[3];
        float quarternion_world[4] = {state_est_state->kf_state.x_est[6], state_est_state->kf_state.x_est[7], state_est_state->kf_state.x_est[8], state_est_state->kf_state.x_est[9]};
        vec_body_to_world_rotation(quarternion_world, acc_rocket, acc_world);
        vec_body_to_world_rotation(quarternion_world, gyro_rocket, gyro_world);

        acc_world[2] -= GRAVITATION;

        for (int i = 0; i < 3; i++) {
            state_est_state->processed_measurements.angular_velocity_world[i] = gyro_world[i];
        }

        float Qdot_world[4] = {0};
        W_to_Qdot(quarternion_world, gyro_world, Qdot_world);
        
        for (int i = 0; i < 3; i++) {
            state_est_state->kf_state.u[i] = acc_world[i];
        }
        for (int i = 0; i < 4; i++) {
            state_est_state->kf_state.u[3+i] = Qdot_world[i];
        }
    #endif

    pressure2altitudeAGL(&state_est_state->env, NUMBER_MEASUREMENTS, state_est_state->kf_state.z, state_est_state->kf_state.z_active, state_est_state->kf_state.z);

    /* compute the mean raw altitude from all barometer measurements */
    int num_alt_meas_active = 0;
    float alt_mean = 0;
    for (int i = 0; i < NUMBER_MEASUREMENTS; i++){
        if (state_est_state->kf_state.z_active[i]){
            num_alt_meas_active += 1;
            alt_mean += state_est_state->kf_state.z[i];
        }
    }
    if (num_alt_meas_active > 0) {
        alt_mean /= num_alt_meas_active;
        state_est_state->state_est_data.altitude_raw = (int32_t)(alt_mean * 1000);
        state_est_state->state_est_data.altitude_raw_active = true;
    } else {  
        state_est_state->state_est_data.altitude_raw_active = false;
    }

    #if STATE_ESTIMATION_TYPE == 1 && USE_STATE_EST_DESCENT == false
        /* during drogue and main descent, the 1D state estimation might work badly,
           thus we are computing the altitude and vertical velocity solely from the barometric data */
    	float altitude_avg = update_mav(&state_est_state->altitude_mav_mem, t, 
                                        alt_mean, state_est_state->state_est_data.altitude_raw_active);
    #endif
    
    if (num_temp_meas_active > 0){
        temp_meas_mean /= num_temp_meas_active;
        update_env(&state_est_state->env, temp_meas_mean);
    }

    /* airbrake extension tracking feedback */
    state_est_state->state_est_data.airbrake_extension = (int32_t)(state_est_state->state_est_meas.airbrake_extension * 1000000);
} 

void select_noise_models(state_est_state_t *state_est_state) {
    float acc_stdev_rocket[3] = {0};
    float baro_stdev = 0;

    float gyro_stdev_norm = 0.01; // noise stdev which scales proportionally with the norm of the angular velocity
    /* we set a minimal angular velocity norm of 0.1 rad/s */
    float omega_norm = max(euclidean_norm(3, state_est_state->processed_measurements.angular_velocity_world), 0.1);
    float gyro_stdev_rocket[3] = {omega_norm * gyro_stdev_norm, omega_norm*gyro_stdev_norm, omega_norm*gyro_stdev_norm};

    switch (state_est_state->flight_phase_detection.flight_phase) {
        case AIRBRAKE_TEST:
        case TOUCHDOWN:
        case IDLE:
            acc_stdev_rocket[0] = 0.080442;
            acc_stdev_rocket[1] = 0.080442;
            acc_stdev_rocket[2] = 0.080442;
            baro_stdev = 1.869;
        break;
        case THRUSTING:
            if (state_est_state->flight_phase_detection.mach_regime == SUPERSONIC) {
                acc_stdev_rocket[0] = 0.576828;
                acc_stdev_rocket[1] = 0.847584;
                acc_stdev_rocket[2] = 0.847584;
            } else if (state_est_state->flight_phase_detection.mach_regime == TRANSONIC) {
                acc_stdev_rocket[0] = 1.616688;
                acc_stdev_rocket[1] = 1.616688;
                acc_stdev_rocket[2] = 1.616688;
            } else {
                acc_stdev_rocket[0] = 1.376343;
                acc_stdev_rocket[1] = 4.5126;
                acc_stdev_rocket[2] = 4.5126;
            }
            baro_stdev = 13.000;
        break;
        case BIAS_RESET:
        case APOGEE_APPROACH:
        case BALLISTIC_DESCENT:
        case COASTING:
            if (state_est_state->flight_phase_detection.mach_regime == SUPERSONIC) {
                acc_stdev_rocket[0] = 0.115758;
                acc_stdev_rocket[1] = 0.904482;
                acc_stdev_rocket[2] = 0.904482;
            } else if (state_est_state->flight_phase_detection.mach_regime == TRANSONIC) {
                acc_stdev_rocket[0] = 0.2;
                acc_stdev_rocket[1] = 0.370818;
                acc_stdev_rocket[2] = 0.370818;
            } else {
                acc_stdev_rocket[0] = 0.1;
                acc_stdev_rocket[1] = 0.805401;
                acc_stdev_rocket[2] = 0.805401;
            }
            baro_stdev = 7.380;
        break;
        case CONTROL:
            acc_stdev_rocket[0] = 1.250775;
            acc_stdev_rocket[1] = 1.8;
            acc_stdev_rocket[2] = 1.8;
            baro_stdev = 13;
        break;
        case DROGUE_DESCENT:
        case MAIN_DESCENT:
            acc_stdev_rocket[0] = 1.85409;
            acc_stdev_rocket[1] = 1.372419;
            acc_stdev_rocket[2] = 1.372419;
            baro_stdev = 3.896;
        break;
    }

    /* update process noise matrix */
    #if STATE_ESTIMATION_TYPE == 1
        state_est_state->kf_state.Q[0][0] = powf(acc_stdev_rocket[0], 2);
    #elif STATE_ESTIMATION_TYPE == 2
        float Q_upper_rocket[3][3] = {0};
        float Q_lower_rocket[3][3] = {0};

        for (int i = 0; i < 3; i++) {
            Q_upper_rocket[i][i] = powf(acc_stdev_rocket[i], 2);
            Q_lower_rocket[i][i] = powf(gyro_stdev_rocket[i], 2);
        }

        float Q_upper_world[3][3] = {0};
        float Q_lower_world[3][3] = {0};
        
        float quarternion_world[4] = {state_est_state->kf_state.x_est[6], state_est_state->kf_state.x_est[7], state_est_state->kf_state.x_est[8], state_est_state->kf_state.x_est[9]};
        
        cov_body_to_world_rotation(quarternion_world, Q_upper_rocket, Q_upper_world);
        cov_body_to_world_rotation(quarternion_world, Q_lower_rocket, Q_lower_world);

        /* transform covariance in rad in world coordinate system to quarternions */
        float Q_lower_quat[4][4] = {0};
        cov_W_to_cov_Qdot(quarternion_world, Q_lower_world, Q_lower_quat);

        for (int i = 0; i < 3; i++) {
            state_est_state->kf_state.Q[i][i] = Q_upper_world[i][i];
        }
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                state_est_state->kf_state.Q[3+i][3+j] = Q_lower_quat[i][j];
            }
        }
    #endif

    float altitude = 0;
    #if STATE_ESTIMATION_TYPE == 1
        altitude = state_est_state->kf_state.x_est[0];
    #elif STATE_ESTIMATION_TYPE == 2
        altitude = state_est_state->kf_state.x_est[2];
    #endif 

    /* update measurement noise matrix */
    float p[1];
    float h[1] = {altitude};
    bool h_active[1] = {true};
    altitudeAGL2pressure(&state_est_state->env, 1, h, h_active, p);
    float h_grad = altitude_gradient(&state_est_state->env, p[0]);
    float altitude_stdev = fabsf(baro_stdev * h_grad);

    for(int i = 0; i < NUMBER_MEASUREMENTS; i++){
        state_est_state->kf_state.R[i][i] = powf(altitude_stdev, 2);
    }

    #if defined(USE_SENSOR_ELIMINATION_BY_EXTRAPOLATION) && USE_SENSOR_ELIMINATION_BY_EXTRAPOLATION == true
        state_est_state->baro_roll_mem.noise_stdev = barometer_stdev;
    #endif
}

void sensor_elimination_by_stdev(int n, float measurements[n], bool measurement_active[n]) {
    /* calculate mean of the sample */
    int num_active = 0;
    float mean = 0;
    for (int i = 0; i < n; i++){
        if (measurement_active[i]) {
            num_active += 1;
            mean += measurements[i];
        }
    }
    if (num_active > 0){
        mean /= num_active;
    }

    /* calculate the standard deviation of the sample */
    float stdev = 0;
    for (int i = 0; i < n; ++i) {
        if (measurement_active[i]) {
            stdev += pow(measurements[i] - mean, 2);
        }
    }
    if (num_active > 0){
        stdev = sqrt(stdev / num_active);
    }

    /* deactivate measurements if they are too far off the mean */
    for (int i = 0; i < n; ++i) {
        if (measurement_active[i]) {
            if (fabsf(measurements[i] - mean) > 2.0 * stdev) {
                measurement_active[i] = false;
            }
        }
    }
}

void sensor_elimination_by_extrapolation(timestamp_t t, int n, float measurements[n], bool measurement_active[n], 
                                         extrapolation_rolling_memory_t *extrapolation_rolling_memory){
    float x_priori = 0;
    /* we only extrapolate if the memory is fully populated. Otherwise we dont eliminate sensor and fill the memory */
    if (extrapolation_rolling_memory->memory_length >= MAX_LENGTH_ROLLING_MEMORY) {
        /* calculate coefficients for fit */
        polyfit(extrapolation_rolling_memory->timestamps, extrapolation_rolling_memory->measurements, 
                MAX_LENGTH_ROLLING_MEMORY, EXTRAPOLATION_POLYFIT_DEGREE, extrapolation_rolling_memory->polyfit_coeffs);
        /* extrapolate the value of the signal type at the current timestamp */
        for (int i = 0; i <= EXTRAPOLATION_POLYFIT_DEGREE; ++i) {
            x_priori += extrapolation_rolling_memory->polyfit_coeffs[i] * pow(t, i);
        }

        /* comparing and discarding outliers */

        /* deactivate measurements if they are too far off the mean */
        for (int i = 0; i < n; ++i) {
            if (measurement_active[i]) {
                float measurement_multiple = fabsf(measurements[i] - x_priori) / extrapolation_rolling_memory->noise_stdev;
                if (measurement_multiple > 1000.0) {
                    measurement_active[i] = false;
                }
            }
        }
    }
    else {}

    int num_active = 0;
    for (int i = 0; i < n; ++i) {
        if (measurement_active[i]) {
            num_active += 1;
        }
    }

    if (num_active > 0){
        /* shift existing elements in rolling memory back by the number of active new measurements we want to insert */
        for (int i = MAX_LENGTH_ROLLING_MEMORY-1; i >= num_active; --i) {

            extrapolation_rolling_memory->timestamps[i] = extrapolation_rolling_memory->timestamps[i-num_active];
            extrapolation_rolling_memory->measurements[i] = extrapolation_rolling_memory->measurements[i-num_active];
        }

        /* insert new measurements at the beginning of the memory */
        int idx_active = 0;
        for (int i = 0; i < n; ++i) {
            if (measurement_active[i]) {
                extrapolation_rolling_memory->timestamps[idx_active] = (float) t;
                extrapolation_rolling_memory->measurements[idx_active] = measurements[i];
                idx_active += 1;
            }
        }
    }

    if (extrapolation_rolling_memory->memory_length < MAX_LENGTH_ROLLING_MEMORY) {
        extrapolation_rolling_memory->memory_length += num_active;
    }

}

float update_mav(mav_memory_t *mav_memory, timestamp_t t, float measurement, bool measurement_active) {
    if (measurement_active == true) {
        if (mav_memory->memory_length < MAX_LENGTH_MOVING_AVERAGE) {
            mav_memory->memory_length += 1;
        }

        for (int i=(mav_memory->memory_length-1); i > 0; i--) {
		    mav_memory->timestamps[i] = mav_memory->timestamps[i-1];
            mav_memory->values[i] = mav_memory->values[i-1];
            mav_memory->avg_values[i] = mav_memory->avg_values[i-1];
        }
        
        mav_memory->timestamps[0] = t;
        mav_memory->values[0] = measurement;

        float values_sum = 0;
        for (int i=0; i < mav_memory->memory_length; i++) {
            values_sum += mav_memory->values[i];
        }

        mav_memory->avg_values[0] = values_sum / (float)mav_memory->memory_length;
    } 

    return mav_memory->avg_values[0];
}

#if STATE_ESTIMATION_TYPE == 2
void init_sensor_transformation_matrix(state_est_state_t *state_est_state) {
    /* we assume two IMUs per sensorboard */
    for (int i = 0; i < (NUM_IMU / 2); i++) {
        /* planar yaw offset of the sensorboard */
        float planar_angle = 2 * M_PI * i / (NUM_IMU / 2);

        // vector from the CoG of the rocket to the sensor in the rocket coordinate system
        // we are assuming now that both IMUs on the sensorboards are in the same location without any tangent offset
        float C_r_CS[3] = {X_COG - X_IMU, cos(planar_angle) * R_IMU, sin(planar_angle) * R_IMU};

        /* rotation matrix from the rocket coordinate system to the sensor coordinate system */
        float R_CS[3][3] = {{0, 0, -1}, {-cos(planar_angle), sin(planar_angle), 0}, {sin(planar_angle), +cos(planar_angle), 0}};

        /* TODO: add proper rotation matrix */

        float T_CS[4][4] = {0};
        T_CS[3][3] = 1;
        for (int j = 0; j < 3; j++) {
            for (int k = 0; k < 3; k++) {
                T_CS[j][k] = R_CS[j][k];
            }
            T_CS[j][3] = C_r_CS[j];
        }

        memcpy(&state_est_state->state_est_meas.imu_data[2*i].C_r_CS, &C_r_CS, sizeof(C_r_CS));
        memcpy(&state_est_state->state_est_meas.imu_data[2*i+1].C_r_CS, &C_r_CS, sizeof(C_r_CS));
        memcpy(&state_est_state->state_est_meas_prior.imu_data[2*i].C_r_CS, &C_r_CS, sizeof(C_r_CS));
        memcpy(&state_est_state->state_est_meas_prior.imu_data[2*i+1].C_r_CS, &C_r_CS, sizeof(C_r_CS));

        memcpy(&state_est_state->state_est_meas.imu_data[2*i].R_CS, &R_CS, sizeof(R_CS));
        memcpy(&state_est_state->state_est_meas.imu_data[2*i+1].R_CS, &R_CS, sizeof(R_CS));
        memcpy(&state_est_state->state_est_meas_prior.imu_data[2*i].R_CS, &R_CS, sizeof(R_CS));
        memcpy(&state_est_state->state_est_meas_prior.imu_data[2*i+1].R_CS, &R_CS, sizeof(R_CS));

        memcpy(&state_est_state->state_est_meas.imu_data[2*i].T_CS, &T_CS, sizeof(T_CS));
        memcpy(&state_est_state->state_est_meas.imu_data[2*i+1].T_CS, &T_CS, sizeof(T_CS));
        memcpy(&state_est_state->state_est_meas_prior.imu_data[2*i].T_CS, &T_CS, sizeof(T_CS));
        memcpy(&state_est_state->state_est_meas_prior.imu_data[2*i+1].T_CS, &T_CS, sizeof(T_CS));
    }
}
#endif