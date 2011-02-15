#ifndef GUIDS_H_
#define GUIDS_H_

//The channel into which work commands are sent
#define WORK_CHANNEL 10

//The channel into which sync requests are written
#define SYNC_CHANNEL_IN 11

//The channel from which sync responses are read
#define SYNC_CHANNEL_OUT 12

//The channel base number for VRows created by the PPU
#define VROW_PPU_CHANNEL_BASE 1000

//The channel base number for VRows read by the SPUs while going down
#define VROW_SPU_CHANNEL_BASE_DOWN 5000

//The channel base number for VRows read by the SPUs while going up
#define VROW_SPU_CHANNEL_BASE_UP 9000


#endif /*GUIDS_H_*/
