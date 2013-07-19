/// -*- tab-width: 4; Mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*-
/*
 *   auto_calibration.cpp - airspeed auto calibration

 * Algorithm by Paul Riseborough
 *
 */

#include <AP_HAL.h>
#include <AP_Math.h>
#include <AP_Common.h>
#include <AP_Airspeed.h>

extern const AP_HAL::HAL& hal;

// constructor - fill in all the initial values
Airspeed_Calibration::Airspeed_Calibration() :
    P(100,   0,         0,
      0,   100,         0,
      0,     0,  0.000001f),
    Q0(0.01f),
    Q1(0.000001f),
    state(0, 0, 0),
    DT(1)
{
}

/*
  initialise the ratio
 */
void Airspeed_Calibration::init(float initial_ratio)
{
    state.z = 1.0 / sqrtf(initial_ratio);
}

/*
  update the state of the airspeed calibration - needs to be called
  once a second

  On an AVR2560 this costs 1.9 milliseconds per call
 */
float Airspeed_Calibration::update(float airspeed, const Vector3f &vg)
{
    // Perform the covariance prediction
    // Q is a diagonal matrix so only need to add three terms in
    // C code implementation
    // P = P + Q;
    P.a.x += Q0;
    P.b.y += Q0;
    P.c.z += Q1;
    
    // Perform the predicted measurement using the current state estimates
    // No state prediction required because states are assumed to be time
    // invariant plus process noise
    // Ignore vertical wind component
    float TAS_pred = state.z * sqrtf(sq(vg.x - state.x) + sq(vg.y - state.y) + sq(vg.z));
    float TAS_mea  = airspeed;
    
    // Calculate the observation Jacobian H_TAS
    float SH1 = sq(vg.y - state.y) + sq(vg.x - state.x);
    if (SH1 < 0.000001f) {
        // avoid division by a small number
        return state.z;
    }
    float SH2 = 1/sqrt(SH1);

    // observation Jacobian
    Vector3f H_TAS(
        -(state.z*SH2*(2*vg.x - 2*state.x))/2,
        -(state.z*SH2*(2*vg.y - 2*state.y))/2,
        1/SH2);
    
    // Calculate the fusion innovaton covariance assuming a TAS measurement
    // noise of 1.0 m/s
    // S = H_TAS*P*H_TAS' + 1.0; % [1 x 3] * [3 x 3] * [3 x 1] + [1 x 1]
    Vector3f PH = P * H_TAS;
    float S = H_TAS * PH + 1.0f;
    
    // Calculate the Kalman gain
    // [3 x 3] * [3 x 1] / [1 x 1]
    Vector3f KG = PH / S; 
    
    // Update the states
    state += KG*(TAS_mea - TAS_pred); // [3 x 1] + [3 x 1] * [1 x 1]
    
    // Update the covariance matrix
    Vector3f HP2 = H_TAS * P;
    P -= KG.mul_rowcol(HP2);
    
    // force symmetry on the covariance matrix - necessary due to rounding
    // errors
    // Implementation will also need a further check to prevent diagonal 
    // terms becoming negative due to rounding errors
    // This step can be made more efficient by excluding diagonal terms 
    // (would reduce processing by 1/3)
    P = (P + P.transpose()) * 0.5f; // [1 x 1] * ( [3 x 3] + [3 x 3])

    return state.z;
}


/*
  called once a second to do calibration update
 */
void AP_Airspeed::update_calibration(Vector3f vground, float EAS2TAS)
{
    if (!_autocal) {
        // auto-calibration not enabled
        return;
    }
    // calculate true airspeed, assuming a ratio of 1.0
    float airspeed = sqrtf(get_differential_pressure()) * EAS2TAS;
    float ratio = _calibration.update(airspeed, vground);
    if (isnan(ratio) || isinf(ratio)) {
        return;
    }
    // this constrains the resulting ratio to between 1.5 and 3
    ratio = constrain_float(ratio, 0.577f, 0.816f);
    _ratio.set(1/sq(ratio));
    if (_counter > 60) {
        if (_last_saved_ratio < 1.05f*_ratio || 
            _last_saved_ratio < 0.95f*_ratio) {
            _ratio.save();
            _last_saved_ratio = _ratio;
            _counter = 0;
        }
    } else {
        _counter++;
    }
}
