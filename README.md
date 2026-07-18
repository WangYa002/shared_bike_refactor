# shared_bike_refactor

C++17 refactor of a shared-bike backend + Qt6 desktop client.

## Layout

- `legacy/` — original C++03 source (libevent + protobuf + log4cpp), preserved for reference
- `proto/` — protobuf schema
- `common/` — shared static lib: FBEB protocol + errors
- `server/` — backend (asio + hiredis + mysql + spdlog + toml++)
- `client/` — Qt6 desktop client
- `docker/` — Dockerfile + docker-compose + mysql-init
- `docs/superpowers/` — design + plan

## Build & run

See `docs/superpowers/specs/2026-07-18-shared-bike-refactor-design.md` for the full design.

### Backend (in Docker)

    docker compose -f docker/docker-compose.yml up --build

Server listens on `0.0.0.0:8888` inside the container, mapped to host 8888.

### Client (Qt6 + CMake on Windows)

    cmake -B build -S . -DBIKE_BUILD_CLIENT=ON
    cmake --build build --config Release --target bike_client

Then launch `build/client/Release/bike_client.exe` (or platform equivalent).

## Wire protocol

    +4 bytes  magic       ASCII "FBEB"
    +2 bytes  event_id    u16 little-endian
    +4 bytes  length      i32 little-endian (= N)
    +N bytes  payload     serialized protobuf

Event IDs:
| 0x01 / 0x02 | mobile_code_req / rsp |
| 0x03 / 0x04 | login_req / rsp |
| 0x05 / 0x06 | recharge_req / rsp |
| 0x07 / 0x08 | account_balance_req / rsp |
| 0x09 / 0x10 | list_account_records_req / rsp |

See `proto/bike.proto` for message schemas.
