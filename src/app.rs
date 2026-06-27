use alloc::{rc::Rc, vec::Vec};
use core::{cell::RefCell, time::Duration};

use pebble_rust_2026::{
    APP, Bitmap, BitmapLayer, GRect, InboxSize, SystemFont, TextLayer, Time, Timer, Value, Window,
    color, log_c_str, message_keys, resource_ids,
    sys::{GTextAlignment_GTextAlignmentRight, TimeUnits_MINUTE_UNIT},
};

resource_ids!(resource_ids);

message_keys!(message_keys);

struct Core {
    level: u8,
    broken: bool,
    layer: BitmapLayer,
}

#[derive(PartialEq)]
enum Status {
    Loading,
    MissingConfig,
    Loaded,
}

struct State {
    status: Status,
    cycles: u32,
    cores: [Core; 64],
}

fn write_digit(data: &mut Vec<u8>, d: u32) {
    data.push(b'0' + d as u8);
}

fn write_number(data: &mut Vec<u8>, mut d: u32) {
    let initial_length = data.len();
    loop {
        let digit = d % 10;
        write_digit(data, digit);
        d /= 10;
        if d == 0 {
            break;
        }
    }
    let (_, new_digits) = data.split_at_mut(initial_length);
    new_digits.reverse();
}

fn write_number_to_layer(layer: &mut TextLayer, v: u32) {
    let mut str = Vec::with_capacity(16);

    if v >= 10_000_000 {
        str.extend_from_slice(b">10M");
    } else if v >= 1000 * 1000 {
        write_number(&mut str, v / 1_000_000);
        str.push(b'.');
        write_number(&mut str, (v % 1_000_000) / 100_1000);
        str.push(b'M');
    } else if v >= 10_000 {
        write_number(&mut str, v / 1_000);
        str.push(b'K');
    } else if v >= 1000 {
        write_number(&mut str, v / 1_000);
        str.push(b'.');
        write_number(&mut str, (v % 1_000) / 100);
        str.push(b'K');
    } else {
        write_number(&mut str, v)
    }

    layer.set_text_bytes(&str);
}

pub fn run_cores() {
    log_c_str(c"starting app");

    let sprites = Bitmap::from_resource(resource_ids::SPRITES).unwrap();
    let core_sprites = (0..8)
        .map(|i| {
            (
                sprites.extract(GRect::new(i * 25, 0, 25, 25)).unwrap(),
                sprites.extract(GRect::new(i * 25, 25, 25, 25)).unwrap(),
            )
        })
        .collect::<Vec<_>>();

    let mut window = Window::new().unwrap();
    window.set_background_color(color::GCOLOR_OXFORD_BLUE);

    let state = {
        let mut core_index: i16 = 0;
        let mut state = State {
            status: Status::Loading,
            cycles: 0,
            cores: [(); 64].map(|_| {
                let y = core_index.div_euclid(8);
                let x = core_index.rem_euclid(8);
                core_index += 1;
                let mut layer = BitmapLayer::new(GRect::new(x * 25, y * 25, 25, 25)).unwrap();
                layer.set_bitmap(&core_sprites[7].1);
                window.add_child(&mut layer);
                Core {
                    level: 0,
                    broken: false,
                    layer,
                }
            }),
        };

        let mut level_buffer = [0u8; 64];
        if let Ok(Some(level_data)) = APP
            .persist
            .read_bytes(message_keys::PERSISTED_CORES, &mut level_buffer)
        {
            level_data.iter().enumerate().for_each(|(i, l)| {
                state.cores[i].level = *l;
            });
            state.status = Status::Loaded;
        }

        if let Some(saved_cycles) = APP.persist.read_int(message_keys::PERSISTED_CYCLES) {
            state.cycles = saved_cycles as u32;
        }
        Rc::new(RefCell::new(state))
    };

    let font = Rc::new(SystemFont::Gothic28Bold.load().unwrap());

    let mut time_layer = TextLayer::new(GRect::new(4, 194, 192, 40)).unwrap();
    time_layer.set_font(&font);
    time_layer.set_text_color(color::GCOLOR_WHITE);
    time_layer.set_background_color(color::GCOLOR_CLEAR);
    window.add_child(&mut time_layer);

    let mut cycle_layer = TextLayer::new(GRect::new(4, 194, 192, 40)).unwrap();
    cycle_layer.set_font(&font);
    cycle_layer.set_text_color(color::GCOLOR_WHITE);
    cycle_layer.set_background_color(color::GCOLOR_CLEAR);
    cycle_layer.set_alignment(GTextAlignment_GTextAlignmentRight);
    window.add_child(&mut cycle_layer);

    let mut update = move |state: &mut State| {
        time_layer.set_text_bytes(Time::now().to_local().format_hh_mm().as_bytes());

        match state.status {
            Status::Loading => {
                cycle_layer.set_text_c_str(c"");
            }
            Status::MissingConfig => cycle_layer.set_text_c_str(c"Needs config"),
            Status::Loaded => {
                write_number_to_layer(&mut cycle_layer, state.cycles);
            }
        }

        state.cores.iter_mut().for_each(|c| {
            let sprite = if state.status == Status::Loaded {
                if c.level < 7 && c.broken {
                    &core_sprites[c.level as usize].1
                } else {
                    &core_sprites[c.level as usize].0
                }
            } else {
                &core_sprites[7].1
            };
            c.layer.set_bitmap(sprite);
        });
    };

    if true {
        update(&mut state.borrow_mut());
        let mut update = update.clone();
        let state = state.clone();
        APP.set_tick_handler(TimeUnits_MINUTE_UNIT, move || {
            update(&mut state.borrow_mut());
        });
    }

    APP.set_message_handler(move |d| {
        match d.get(message_keys::TYPE) {
            Some(Value::CStr(t)) if *t == c"RESET" => {
                let mut state = state.borrow_mut();
                state.status = Status::MissingConfig;
                update(&mut state);
            }
            Some(Value::CStr(t)) if *t == c"STATE" => {
                if let Some((level_data, broken_data)) =
                    match (d.get(message_keys::LEVELS), d.get(message_keys::BROKEN)) {
                        (Some(Value::Bytes(level_value)), Some(Value::Bytes(broken_value))) => {
                            Some((level_value, broken_value))
                        }
                        _ => None,
                    }
                {
                    log_c_str(c"update received");
                    let mut state = state.borrow_mut();
                    state.status = Status::Loaded;
                    level_data.iter().enumerate().for_each(|(i, level)| {
                        state.cores[i].level = *level;
                    });

                    broken_data.iter().enumerate().for_each(|(i, broken)| {
                        state.cores[i].broken = *broken != 0;
                    });

                    if let Some(cycles) = d.get(message_keys::CYCLES).and_then(|f| f.as_u32()) {
                        state.cycles = cycles;
                    }

                    update(&mut state);

                    APP.persist
                        .write_int(message_keys::PERSISTED_CYCLES, state.cycles as i32);
                    APP.persist
                        .write_bytes(message_keys::PERSISTED_CORES, level_data)
                        .unwrap();
                } else {
                    log_c_str(c"received invalid state");
                }
            }
            _ => {}
        };
    });

    APP.open_inbox(InboxSize::Half).unwrap();

    let request_update = || {
        log_c_str(c"requesting update");
        APP.send_message(|b| {
            b.write_cstr(message_keys::TYPE, c"REFRESH")?;
            Ok(())
        })
        .is_ok()
    };

    request_update();

    APP.show(window);

    Timer::repeat(Duration::from_mins(10), request_update);

    log_c_str(c"starting loop");

    APP.event_loop();

    log_c_str(c"finished loop");
}
