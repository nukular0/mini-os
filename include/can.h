#ifndef _CAN_H
#define _CAN_H


/* special address description flags for the CAN_ID */
#define CAN_EFF_FLAG 0x80000000U /* EFF/SFF is set in the MSB */
#define CAN_RTR_FLAG 0x40000000U /* remote transmission request */
#define CAN_ERR_FLAG 0x20000000U /* error message frame */

/* valid bits in CAN ID for frame formats */
#define CAN_SFF_MASK 0x000007FFU /* standard frame format (SFF) */
#define CAN_EFF_MASK 0x1FFFFFFFU /* extended frame format (EFF) */
#define CAN_ERR_MASK 0x1FFFFFFFU /* omit EFF, RTR, ERR flags */


/*
 * Controller Area Network Identifier structure
 *
 * bit 0-28	: CAN identifier (11/29 bit)
 * bit 29	: error message frame flag (0 = data frame, 1 = error message)
 * bit 30	: remote transmission request flag (1 = rtr frame)
 * bit 31	: frame format flag (0 = standard 11 bit, 1 = extended 29 bit)
 */
typedef uint32_t canid_t;

struct can_frame {
	canid_t can_id;  /* 32 bit CAN_ID + EFF/RTR/ERR flags */
	uint8_t can_dlc; /* frame payload length in byte (0 .. CAN_MAX_DLEN) */
	uint8_t __pad;   /* padding */
	uint8_t __res0;  /* reserved / padding */
	uint8_t __res1;  /* reserved / padding */
	uint8_t data[8] __attribute__((aligned(8)));
};

static inline int can_frame_error(struct can_frame* cf)
{
	return (cf->can_id & CAN_ERR_FLAG);
}

static inline int can_frame_rtr(struct can_frame* cf)
{
	return (cf->can_id & CAN_RTR_FLAG);
}

static inline int can_frame_valid_data(struct can_frame* cf)
{
	if(can_frame_error(cf) || can_frame_rtr(cf))
		return 0;
	
	return 1;
}

static inline int can_frame_is_eff(struct can_frame* cf)
{
	return (cf->can_id & CAN_EFF_FLAG);
}

static inline canid_t can_frame_remove_flags(struct can_frame* cf)
{
	if(can_frame_is_eff(cf))
		cf->can_id &= CAN_EFF_MASK;
	else
		cf->can_id &= CAN_SFF_MASK;
		
	return cf->can_id;
}





#endif
