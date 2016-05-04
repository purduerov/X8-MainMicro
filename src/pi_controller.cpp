#include "pi_controller.h"

// CLASS METHODS:

// Constructor:
PIController::PIController(void)
{
	data.rot_ref = vect3Make(0,0,0);
	data.rot_error = vect3Make(0,0,0);
	//data.rot_error_vel = vect3Make(0,0,0);

	data.Z_ref = 0;
	data.Z_error = 0;

	data.lastForce = vect3Make(0,0,0);
	data.Z_lastForce = 0;
	data.CObias = vect3Make(0,0,0);
	data.integralSum = vect3Make(0,0,0);
	data.Z_integralSum = 0;
	data.lastTime = 0;
	data.timeDiff = 0;

	consts.P_rot = P_INIT_VALUE;
	consts.I_rot = I_INIT_VALUE;
	consts.P_Z = P_INIT_VALUE;
	consts.I_Z = I_INIT_VALUE;
	ON_OFF = OFF;
}

/*
// Call this every time that the PI controller should stop affecting the force vector.
void PIController::stop(void)
{
	ON_OFF = OFF;
}

// Call every time the PI Controller will be started or restarted.
// This method ensures that all variables are set to what they should be to be restarted.
void PIController::start(void)
{
	data.rot_ref = data.rot_est;
	data.rot_error = vect3Make(0,0,0);
	data.rot_error_vel = vect3Make(0,0,0);
	data.CObias = data.lastForce;
	data.integralSum.x = 0;
	data.integralSum.y = 0;
	data.integralSum.z = 0;
	data.lastTime = 0;
	ON_OFF = OFF;
}
*/

void PIController::set_PI(int16_t* PIDTuning, uint8_t state)
{

	// Sets new PI gains for the controller
	// Proportional constant
	consts.P_rot = PIDTuning[0];
	consts.P_Z = PIDTuning[1];
	// Integral Constant = controller gain / reset time
	consts.I_rot = PIDTuning[2];
	consts.I_Z = PIDTuning[3];

	// Check for changes in the bits. If something is turned on, reset the reference to the current value = "LOCK"
	if((ON_OFF ^ state) & 0x01)
		if(state & 0x01) //turned ON?
			data.rot_ref.x = data.rot_est.x; // Set it to the latest estimate

	if((ON_OFF ^ state) & 0x02)
		if(state & 0x02) //turned ON?
			data.rot_ref.y = data.rot_est.y; // Set it to the latest estimate

	if((ON_OFF ^ state) & 0x04)
		if(state & 0x04) //turned ON?
			data.rot_ref.z = data.rot_est.z; // Set it to the latest estimate

	if((ON_OFF ^ state) & 0x20)
		if(state & 0x20) //turned ON?
			data.Z_ref = data.Z_est; // Set it to the latest estimate

	// Finnaly apply the full state
	ON_OFF = state;
}

// Sets new target rotation orientation.
// Target is in the force_input vector from the battle station, only read the parts that actually are stabilized
void PIController::set_ref(vect6 user_input)
{

	if(ON_OFF == 0x01){
		data.rot_ref.x += user_input.R.x/455;  //450 deg/s

		if(data.rot_ref.x >= 1120) // if tilting more than 1120 degrees
			data.rot_ref.x == 1120;
		else if(data.rot_ref.x <= -1120)
			data.rot_ref.x == -1120;
	}

	if(ON_OFF == 0x02){
		data.rot_ref.y += user_input.R.y/455;

		if(data.rot_ref.y >= 1120) // if tilting more than 1120 degrees
			data.rot_ref.y == 1120;
		else if(data.rot_ref.y <= -1120)
			data.rot_ref.y == -1120;
	}

	if(ON_OFF == 0x04){
		data.rot_ref.z += user_input.R.z/455;

		if(data.rot_ref.z >= 5760) // if tilting more than 1120 degrees
			data.rot_ref.z -= 5760;
		else if(data.rot_ref.z <= 0)
			data.rot_ref.z += 5760;
	}

	if(ON_OFF == 0x20){
		data.Z_ref += user_input.L.z/3276.8; // 1 m/s as maximum speed
	}
}

// When driving in stabilized mode, add the sent rotation
// to the current one in local space thus rotate it first
void PIController::updateRotation(vect3 rot_ref)
{
	float old_rot[3];
	float d_rot[3];
	float c, s; // One cosine and sine per rotation, reused.
	
	// Convert to radians
	old_rot[0] = data.rot_ref.x/1000.0;
 	old_rot[1] = data.rot_ref.y/1000.0;
 	old_rot[2] = data.rot_ref.z/1000.0;
	
	// Convert to radians per second (max 45deg/s) 
	d_rot[0] = rot_ref.x/4172200.0;
 	d_rot[0] = rot_ref.x/4172200.0;
	d_rot[0] = rot_ref.x/4172200.0;
 
	// Rotate around X
	c = cos(old_rot[0]);
	s = sin(old_rot[0]);
	d_rot[1] = c*d_rot[1] - s*d_rot[2];
	d_rot[2] = s*d_rot[1] + c*d_rot[2];

	// Rotate around Y
	c = cos(old_rot[1]);
	s = sin(old_rot[1]);
	d_rot[0] =  c*d_rot[0] + s*d_rot[2];
	d_rot[2] = -s*d_rot[0] + c*d_rot[2];

	// Rotate around Z
	c = cos(old_rot[2]);
	s = sin(old_rot[2]);
	d_rot[1] =  c*d_rot[0] - s*d_rot[1];
	d_rot[2] =  s*d_rot[0] + c*d_rot[1];

	// Back to 1000 per radian
	data.rot_ref.x += d_rot[0]*1000;
	data.rot_ref.y += d_rot[1]*1000;
	data.rot_ref.z += d_rot[2]*1000;
}



// Sets the fusion data from the sensors to the data structure.
// Perform this method even when PI is off.
void PIController::sensorInput(vect3 rot_est, float Z_est, int32_t timems)
{
	data.timeDiff = timems - data.lastTime;
	data.lastTime = timems;
	// if lastTime is 0, then this is the first update since the PI controller has been turned on.

	data.rot_est = rot_est;
	data.rot_error = sub(data.rot_ref, rot_est);
	//data.rot_error_vel = rot_est_vel;

	data.Z_est = Z_est;
	data.Z_error = data.Z_ref - Z_est;

	if (data.lastTime != 0)
	{
		data.integralSum.x += data.rot_error.x * data.timeDiff;
		data.integralSum.y += data.rot_error.y * data.timeDiff;
		data.integralSum.z += data.rot_error.z * data.timeDiff;

		data.Z_integralSum += data.Z_error * data.timeDiff;
	}

}

// Computes the resultant force vector to achieve the desired rotational orientation using the PI Controller.
// 
// @returns - the force vector computed from the PI Controller, or just the incoming force vector set by
//			Control.
// @note - May want to shift these computations to the sensorInput function... If data needs to be 
//			accumulated even when the controller is off.
vect6 PIController::getOutput(vect6 FORCE)
{
	if (ON_OFF == OFF)
		return FORCE;

	if(ON_OFF == 0x01){
		data.lastForce.x = data.CObias.x + consts.P_rot * data.rot_error.x + consts.I_rot * data.integralSum.x;

		FORCE.R.x = data.lastForce.x;
	}

	if(ON_OFF == 0x02){
		data.lastForce.y = data.CObias.y + consts.P_rot * data.rot_error.y + consts.I_rot * data.integralSum.y;

		FORCE.R.y = data.lastForce.y;
	}
	if(ON_OFF == 0x04){
		data.lastForce.z = data.CObias.z + consts.P_rot * data.rot_error.z + consts.I_rot * data.integralSum.z;

		FORCE.R.z = data.lastForce.z;
	}

	if(ON_OFF == 0x20){
		data.Z_lastForce = consts.P_Z * data.Z_error + consts.I_Z * data.Z_integralSum;

		// Rotate this vector from earth frame up to bodyframe
	}


	return FORCE;
}
