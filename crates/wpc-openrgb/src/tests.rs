//! Tests against an in-process fake SDK server that encodes controllers like OpenLinkHub does.

use super::*;
use std::net::TcpListener;
use std::sync::mpsc;
use std::thread;

struct FakeController {
    name: &'static str,
    zones: Vec<(&'static str, u32)>,
}

fn string(buf: &mut Vec<u8>, s: &str) {
    buf.extend_from_slice(&((s.len() + 1) as u16).to_le_bytes());
    buf.extend_from_slice(s.as_bytes());
    buf.push(0);
}

/// Controller data for protocol 4, same layout as OpenLinkHub's `buildDeviceDataPayload`.
fn encode(c: &FakeController) -> Vec<u8> {
    let u32le = |b: &mut Vec<u8>, v: u32| b.extend_from_slice(&v.to_le_bytes());
    let u16le = |b: &mut Vec<u8>, v: u16| b.extend_from_slice(&v.to_le_bytes());
    let mut b = Vec::new();
    u32le(&mut b, 7); // device type
    string(&mut b, c.name);
    for s in ["Corsair", "desc", "1.0", "SERIAL", "HID: /dev/hidraw8"] {
        string(&mut b, s);
    }
    u16le(&mut b, 1); // one mode
    u32le(&mut b, 0); // active mode
    string(&mut b, "Direct");
    for v in [0u32, 1 << 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1] {
        u32le(&mut b, v); // value, flags, speed min/max, brightness min/max, colors min/max, speed, brightness, direction, color_mode
    }
    u16le(&mut b, 0); // mode colours
    u16le(&mut b, c.zones.len() as u16);
    for (name, leds) in &c.zones {
        string(&mut b, name);
        u32le(&mut b, 1); // zone type
        for _ in 0..3 {
            u32le(&mut b, *leds); // min, max, count
        }
        u16le(&mut b, 12); // matrix: 1x1
        u32le(&mut b, 1);
        u32le(&mut b, 1);
        u32le(&mut b, 0);
        u16le(&mut b, 1); // one segment
        string(&mut b, "seg");
        for v in [0u32, 0, *leds] {
            u32le(&mut b, v);
        }
    }
    let total: u32 = c.zones.iter().map(|z| z.1).sum();
    u16le(&mut b, total as u16);
    for i in 0..total {
        string(&mut b, &format!("LED {i}"));
        u32le(&mut b, i);
    }
    u16le(&mut b, total as u16);
    for _ in 0..total {
        b.extend_from_slice(&[0, 255, 255, 0]);
    }
    let mut out = ((b.len() + 4) as u32).to_le_bytes().to_vec();
    out.extend(b);
    out
}

fn reply(stream: &mut TcpStream, device: u32, id: u32, payload: &[u8]) {
    let mut buf = b"ORGB".to_vec();
    for v in [device, id, payload.len() as u32] {
        buf.extend_from_slice(&v.to_le_bytes());
    }
    buf.extend_from_slice(payload);
    stream.write_all(&buf).unwrap();
}

/// Starts a fake server; returns its address and a channel with every (device, id, payload) received.
fn serve(
    controllers: Vec<FakeController>,
    server_proto: u32,
) -> (String, mpsc::Receiver<(u32, u32, Vec<u8>)>) {
    let listener = TcpListener::bind("127.0.0.1:0").unwrap();
    let addr = listener.local_addr().unwrap().to_string();
    let (tx, rx) = mpsc::channel();
    thread::spawn(move || {
        let (mut s, _) = listener.accept().unwrap();
        loop {
            let mut h = [0u8; 16];
            if s.read_exact(&mut h).is_err() {
                return;
            }
            let device = u32::from_le_bytes(h[4..8].try_into().unwrap());
            let id = u32::from_le_bytes(h[8..12].try_into().unwrap());
            let mut p = vec![0u8; u32::from_le_bytes(h[12..16].try_into().unwrap()) as usize];
            s.read_exact(&mut p).unwrap();
            match id {
                packet::REQUEST_PROTOCOL_VERSION => {
                    // an unsolicited notification first, which the client must skip
                    reply(&mut s, 0, packet::DEVICE_LIST_UPDATED, &[]);
                    reply(&mut s, 0, id, &server_proto.to_le_bytes());
                }
                packet::REQUEST_CONTROLLER_COUNT => {
                    reply(&mut s, 0, id, &(controllers.len() as u32).to_le_bytes())
                }
                packet::REQUEST_CONTROLLER_DATA => match controllers.get(device as usize) {
                    Some(c) => reply(&mut s, device, id, &encode(c)),
                    None => reply(&mut s, device, id, &[]),
                },
                _ => {}
            }
            tx.send((device, id, p)).unwrap();
        }
    });
    (addr, rx)
}

fn devices() -> Vec<FakeController> {
    vec![
        FakeController {
            name: "VENGEANCE RGB",
            zones: vec![("DIMM", 10)],
        },
        FakeController {
            name: "iCUE COMMANDER Core",
            zones: vec![("AIO", 29), ("Fan 1", 8), ("Fan 2", 24)],
        },
    ]
}

#[test]
fn lists_controllers() {
    let (addr, _rx) = serve(devices(), 4);
    let mut c = Client::connect(&addr, "test").unwrap();
    assert_eq!(c.protocol(), 4);
    let ctrls = c.controllers().unwrap();
    assert_eq!(ctrls.len(), 2);
    assert_eq!(ctrls[0].name, "VENGEANCE RGB");
    assert_eq!(ctrls[0].vendor, "Corsair");
    assert_eq!(ctrls[0].led_count(), 10);
    assert_eq!(ctrls[0].colors[0], Rgb::new(0, 255, 255));
    assert_eq!(ctrls[1].name, "iCUE COMMANDER Core");
    assert_eq!(ctrls[1].led_count(), 61);
    let zones: Vec<_> = ctrls[1]
        .zones
        .iter()
        .map(|z| (z.name.as_str(), z.leds))
        .collect();
    assert_eq!(zones, [("AIO", 29), ("Fan 1", 8), ("Fan 2", 24)]);
    assert_eq!(ctrls[1].index, 1);
}

#[test]
fn negotiates_down_to_newest_common_version() {
    let (addr, _rx) = serve(vec![], 6);
    assert_eq!(Client::connect(&addr, "test").unwrap().protocol(), 4);
}

#[test]
fn sends_client_name_and_led_colours() {
    let (addr, rx) = serve(devices(), 4);
    let mut c = Client::connect(&addr, "WallpaperColor").unwrap();
    let ctrl = c.controller(0).unwrap();
    c.set_all(&ctrl, Rgb::new(255, 0, 0)).unwrap();
    drop(c);

    let packets: Vec<_> = rx.iter().collect();
    let (_, _, name) = packets
        .iter()
        .find(|p| p.1 == packet::SET_CLIENT_NAME)
        .unwrap();
    assert_eq!(name.as_slice(), b"WallpaperColor\0");

    let (device, _, p) = packets.iter().find(|p| p.1 == packet::UPDATE_LEDS).unwrap();
    assert_eq!(*device, 0);
    assert_eq!(
        u32::from_le_bytes(p[0..4].try_into().unwrap()) as usize,
        p.len()
    );
    assert_eq!(u16::from_le_bytes(p[4..6].try_into().unwrap()), 10);
    assert_eq!(p.len(), 6 + 10 * 4);
    assert!(p[6..].chunks(4).all(|c| c == [255, 0, 0, 0]));
}

#[test]
fn missing_controller_is_an_error() {
    let (addr, _rx) = serve(devices(), 4);
    let mut c = Client::connect(&addr, "test").unwrap();
    assert!(matches!(c.controller(5), Err(Error::Protocol(_))));
}

#[test]
fn truncated_data_is_an_error_not_a_panic() {
    let data = encode(&devices()[1]);
    for cut in [0, 10, 40, data.len() / 2, data.len() - 1] {
        assert!(
            wire::parse_controller(&data[..cut], 0, 4).is_err(),
            "cut at {cut}"
        );
    }
    assert!(wire::parse_controller(&data, 0, 4).is_ok());
}

#[test]
fn connection_refused_is_io_error() {
    let port = TcpListener::bind("127.0.0.1:0")
        .unwrap()
        .local_addr()
        .unwrap()
        .port();
    assert!(matches!(
        Client::connect(("127.0.0.1", port), "test"),
        Err(Error::Io(_))
    ));
}
