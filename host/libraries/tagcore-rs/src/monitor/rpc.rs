//! Protobuf RPC framing over either transport: serialise the request into
//! the firmware's call buffer, run PROTOBUF, read back and decode the reply.

use std::time::Instant;

use log::error;
use prost::Message;

use super::abi::op;
use super::{INSTRUMENTED, TagMonitor};
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
