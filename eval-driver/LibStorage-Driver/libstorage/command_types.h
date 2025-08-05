#ifndef _COMMAND_TYPES_H_
#define _COMMAND_TYPES_H_

#include <linux/types.h>

struct nvme_completion {
	/*
     * Used by Admin and Fabrics commands to return data:
     */
	union nvme_result {
		__le16 u16;
		__le32 u32;
		__le64 u64;
	} result;
	__le16 sq_head; /* how much of this queue may be reclaimed */
	__le16 sq_id; /* submission queue that generated this entry */
	__u16 command_id; /* of the command which completed */
	__le16 status; /* did the command fail, and if so, why? */
};

enum nvme_opcode {
	nvme_cmd_flush = 0x00,
	nvme_cmd_write = 0x01,
	nvme_cmd_read = 0x02,
	nvme_cmd_write_uncor = 0x04,
	nvme_cmd_compare = 0x05,
	nvme_cmd_write_zeroes = 0x08,
	nvme_cmd_dsm = 0x09,
	nvme_cmd_resv_register = 0x0d,
	nvme_cmd_resv_report = 0x0e,
	nvme_cmd_resv_acquire = 0x11,
	nvme_cmd_resv_release = 0x15,
};

struct nvme_common_command {
	__u8 opcode;
	__u8 flags;
	__u16 command_id;
	__le32 nsid;
	__le32 cdw2[2];
	__le64 metadata;
	__le64 prp1;
	__le64 prp2;
	__le32 cdw10[6];
};

struct nvme_rw_command {
	__u8 opcode;
	__u8 flags;
	__u16 command_id;
	__le32 nsid;
	__u64 rsvd2;
	__le64 metadata;
	__le64 prp1;
	__le64 prp2;
	__le64 slba;
	__le16 length;
	__le16 control;
	__le32 dsmgmt;
	__le32 reftag;
	__le16 apptag;
	__le16 appmask;
};

struct nvme_command {
	union {
		struct nvme_common_command common;
		struct nvme_rw_command rw;
	};
};

#endif