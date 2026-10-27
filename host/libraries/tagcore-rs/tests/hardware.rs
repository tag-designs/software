//! Bench checks against a real tag on an ST-LINK base.
//!
//! Ignored by default because attaching resets the tag. They send only
//! read-only requests, plus one calibration write that every firmware
//! refuses, but do not run them against a tag in a deployment run. Run with:
//!
//! ```sh
//! cargo test --test hardware -- --ignored --test-threads=1
//! ```

use tagcore::proto::ack;
use tagcore::{MemAccess, Tag, TargetFamily, UsbDev};

/// STM32L4 96-bit unique device ID.
const STM32L4_UID_ADDR: u32 = 0x1FFF_7590;

/// Attaches, reads info, status and config, checks the SWD-read UID
/// against the reported UUID (STM32L4 only), exercises the memory-range
/// checks, confirms an empty calibration write is refused, keeps the
/// session alive and detaches twice.
#[test]
#[ignore = "needs a tag on an ST-LINK base; attaching resets it"]
fn attach_read_and_detach() {
    let tag = Tag::new();
    assert!(!tag.available().is_empty(), "no ST-LINK found");
    assert!(tag.attach(&UsbDev::default()), "attach failed");
    assert!(tag.is_attached());

    let info = tag.tag_info().expect("tag info");
    let status = tag.status().expect("status");
    assert_eq!(tag.last_error().message, "");
    assert!(tag.git_sha().unwrap().starts_with(&info.githash));
    tag.config().expect("config");
    println!(
        "{} {} state {:?}",
        info.board_desc,
        info.uuid,
        status.state()
    );

    // Live memory access between RPCs. Only the L4 UID address is known
    // here, so the UID check is skipped on other families.
    let mut uid = [0u8; 12];
    if tag.target_family() == TargetFamily::Stm32L4 {
        assert_eq!(tag.read_memory(STM32L4_UID_ADDR, &mut uid), MemAccess::Ok);
        for word in uid.chunks(4) {
            let hex = format!("{:08X}", u32::from_le_bytes(word.try_into().unwrap()));
            assert!(
                info.uuid.contains(&hex),
                "UID word {hex} not in {}",
                info.uuid
            );
        }
    }
    assert_eq!(
        tag.read_memory(STM32L4_UID_ADDR + 1, &mut uid),
        MemAccess::InvalidRange
    );

    // Constants without the magnetometer block are refused before anything
    // is stored: INVAL on a tag with a calibration store, PERM on the rest.
    let refused = tag
        .write_calibration(&Default::default())
        .expect_err("empty calibration constants must be refused");
    assert!(!refused.transport_failed);
    assert!(
        matches!(refused.err, ack::Err::Inval | ack::Err::Perm),
        "unexpected refusal {refused:?}"
    );
    println!("write_calibration(empty) refused: {refused}");

    assert!(tag.keep_alive());
    assert!(tag.status().is_ok(), "status after keep-alive");

    tag.detach();
    assert!(!tag.is_attached());
    assert_eq!(
        tag.read_memory(STM32L4_UID_ADDR, &mut uid),
        MemAccess::NotAttached
    );
    tag.detach(); // a second detach is harmless
}
