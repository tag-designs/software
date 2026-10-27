//! Protobuf RPC framing over either transport: serialise the request into
//! the firmware's call buffer, run PROTOBUF, read back and decode the reply.
//!
//! Port of `TagMonitor::Rpc`. The wire format is plain protobuf with no
//! length prefix: the request length travels as the PROTOBUF operand and
//! the reply length as the call's result word. Request and reply share the
//! firmware's call buffer, so the reply overwrites the request. Transfers
//! are rounded up to whole words because the memory-access path is 32-bit
//! only; the padding bytes are whatever was in the host staging buffer and
//! are ignored by the decoder, which is given the exact length.

use std::time::Instant;

use log::error;
use prost::Message;

use super::abi::op;
use super::{INSTRUMENTED, TagMonitor, TargetFamily};
use crate::link::elapsed_ns;
use crate::proto::{Ack, Req};

/// Rounds a byte count up to a whole number of words.
fn round4(n: usize) -> usize {
    (n + 3) & !3
}

impl TagMonitor {
    /// Sends `req` to the firmware and returns its reply.
    ///
    /// Serialises the request into the firmware's call buffer, runs the
    /// PROTOBUF monitor call and decodes the reply, whatever its
    /// [`Ack::err`] code. Returns `None`, with the reason logged, if not
    /// attached, the request does not fit the call buffer, the link or
    /// monitor call fails, or the reply does not decode.
    ///
    /// The request must leave two bytes spare in the call buffer (as the
    /// C++ requires), and a reported reply length larger than the call
    /// buffer is rejected rather than read. A firmware refusal is not a
    /// failure here: it is an [`Ack`] with a non-OK `err`, and the caller
    /// decides. Blocks for as long as the transport's call timeout (about
    /// 120 s for a protobuf call on STM32L4, a few seconds on STM32U3).
    /// Updates [`TagMonitorStats`](super::TagMonitorStats).
    pub fn rpc(&mut self, req: &Req) -> Option<Ack> {
        let rpc_start = Instant::now();

        if !self.is_attached() {
            error!("Monitor not attached");
            return None;
        }

        let size = req.encoded_len();
        if size > self.rpcbuf.len() {
            error!("Request message too big");
            return None;
        }
        let step = Instant::now();
        let mut out: &mut [u8] = &mut self.rpcbuf[..];
        if req.encode(&mut out).is_err() {
            error!("Request message too big");
            return None;
        }
        if INSTRUMENTED {
            self.monitor_stats.serialize_ns += elapsed_ns(step);
        }

        if size + 2 > self.maxpacket {
            error!("Input buffer too long");
            return None;
        }

        // An STM32L4 tag may still hold an earlier request that a failed call
        // left pending; the buffer is its until the tag clears MON_REQ. The U3
        // mailbox has its own handshake in call_u3().
        const L4_IDLE_BEFORE_WRITE_MS: u32 = 2500;
        if self.target_family == TargetFamily::Stm32L4
            && !self.wait_l4_idle(L4_IDLE_BEFORE_WRITE_MS)
        {
            return None;
        }

        // The ST-LINK is faster for word-aligned access, and the firmware's
        // call buffer is word aligned, so whole words are transferred.
        let step = Instant::now();
        let write_ok = self
            .link
            .write_mem32(self.call_buf, &self.rpcbuf[..round4(size)]);
        if INSTRUMENTED {
            self.monitor_stats.write_request_ns += elapsed_ns(step);
        }
        if !write_ok {
            error!("RPC buffer write failed");
            return None;
        }

        let step = Instant::now();
        let retval = self.call(op::PROTOBUF, size as i32);
        if INSTRUMENTED {
            self.monitor_stats.monitor_call_ns += elapsed_ns(step);
        }
        let Some(retval) = retval else {
            error!("monitor_call failed");
            return None;
        };

        let len = retval as usize;
        if len > self.maxpacket {
            error!(
                "Reply length {len} exceeds the monitor buffer ({} bytes)",
                self.maxpacket
            );
            return None;
        }

        let step = Instant::now();
        let read_ok = len == 0
            || self
                .link
                .read_mem32(self.call_buf, &mut self.rpcbuf[..round4(len)]);
        if INSTRUMENTED {
            self.monitor_stats.read_response_ns += elapsed_ns(step);
        }
        if !read_ok {
            error!("Error reading RPC buffer");
            return None;
        }

        let step = Instant::now();
        let ack = Ack::decode(&self.rpcbuf[..len]);
        if INSTRUMENTED {
            self.monitor_stats.parse_ns += elapsed_ns(step);
            self.monitor_stats.rpc_calls += 1;
            self.monitor_stats.request_bytes += size as u64;
            self.monitor_stats.response_bytes += len as u64;
            self.monitor_stats.rpc_total_ns += elapsed_ns(rpc_start);
        }
        ack.map_err(|e| error!("Reply did not decode: {e}")).ok()
    }
}
