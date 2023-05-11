/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2019 - 2020 Intel Corporation.
 *
 */

#if !defined(__TRACE_NL_H) || defined(TRACE_HEADER_MULTI_READ)
#define __TRACE_NL_H

#include <linux/tracepoint.h>
#include <linux/trace_seq.h>

#include "iaf_drv.h"
#include "ops.h"
#include "netlink.h"

#undef TRACE_SYSTEM
#define TRACE_SYSTEM iaf_nl

DECLARE_EVENT_CLASS(iaf_nl_template,
		    TP_PROTO(enum cmd r_cmd, enum attr r_attr, u32 len,
			     u32 snd_seq),
		    TP_ARGS(r_cmd, r_attr, len, snd_seq),
		    TP_STRUCT__entry(__field(enum cmd, r_cmd)
				     __field(enum attr, r_attr)
				     __field(u32, len)
				     __field(u32, snd_seq)
				    ),
		    TP_fast_assign(__entry->r_cmd = r_cmd;
				   __entry->r_attr = r_attr;
				   __entry->len = len;
				   __entry->snd_seq = snd_seq;
				  ),
		    TP_printk("cmd %u attr %u len %u snd_seq %u",
			      __entry->r_cmd,
			      __entry->r_attr,
			      __entry->len,
			      __entry->snd_seq
			     )
		    );

DEFINE_EVENT(iaf_nl_template, nl_rsp,
	     TP_PROTO(enum cmd r_cmd, enum attr r_attr, u32 len, u32 snd_seq),
	     TP_ARGS(r_cmd, r_attr, len, snd_seq)
	    );

DEFINE_EVENT(iaf_nl_template, nl_req,
	     TP_PROTO(enum cmd r_cmd, enum attr r_attr, u32 len, u32 snd_seq),
	     TP_ARGS(r_cmd, r_attr, len, snd_seq)
	    );

#endif /* __TRACE_NL_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE trace_nl
#include <trace/define_trace.h>
