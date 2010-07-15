#ifndef NBODYCHANNEL_GUIDS_H_
#define NBODYCHANNEL_GUIDS_H_

#define SIMULATION_SETUP 100
#define FEEDBACK_CHANNEL 105
#define MASTER_COMPLETION_LOCK 108

#define CHANNEL_START_GUID 1000

#define REPETITIONS 1000

//Simulation parameters
#define SIMULATION_GRAVITY 1.0
#define SIMULATION_MAX_MASS 5.0
#define SIMULATION_MAX_VELOCITY 300000.0
#define SIMULATION_PARTICLE_COUNT 2500
#define SIMULATION_MIN_X 0.0
#define SIMULATION_MAX_X 600000000.0
#define SIMULATION_MIN_Y 0.0
#define SIMULATION_MAX_Y 600000000.0
#define SIMULATION_UPDATE_FREQUENCY 10

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif


//The PS3 and older Cell blades only simulate double, so we use float
#define VALUE_TYPE float

//Basic structure for a single particle
struct Particle {
	VALUE_TYPE xPos;
	VALUE_TYPE yPos;
	VALUE_TYPE xVel;
	VALUE_TYPE yVel;
	VALUE_TYPE mass;
	unsigned int expired;
};

//Structure for passing core arguments for the simulation
struct SimulationArgs
{
	unsigned int UpdateFrequency;
	unsigned int RoundCount;

	unsigned int ProcessCount;
	unsigned int ProcessId;

	VALUE_TYPE Gravity;
	VALUE_TYPE MaxMass;
	VALUE_TYPE MaxVelocity;

	VALUE_TYPE MinX;
	VALUE_TYPE MaxX;
	VALUE_TYPE MinY;
	VALUE_TYPE MaxY;
};

#endif /* NBODYCHANNEL_GUIDS_H_ */
