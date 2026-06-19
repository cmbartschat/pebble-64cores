# 64 Cores Watchface

Check up on your 64 Cores game directly from your wrist

<img alt="Preview of watchface" src="https://apps.repebble.com/og/225be1940e88497c83f73e44.png" width="300" height="157" />

Install: https://apps.repebble.com/225be1940e88497c83f73e44

## Development

### Prerequisites

1. Cargo: https://rustup.rs/
2. Pebble SDK: https://developer.repebble.com/sdk/

```sh
rustup target add thumbv8m.main-none-eabi
pebble clean
pebble build
pebble install --emulator emery
```
