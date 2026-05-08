# lilproxy

A lightweight TCP proxy written in C.

## Usage

```sh
./lilproxy -l <port> -a <target-addr> -t <target-port> [-r <rules-file>]
```

## Rules

Format: `<action>:<pattern>[:<response>]`

- **block** - drop matching packet, keep connection alive
- **drop** - close the connection
- **reply** - send custom response back to the client

Escape sequences: `\xBB`, `\n`, `\r`, `\t`, `\\`

File-based replies: `reply:pattern:@file=/path` (hot-reloads on change)

Examples:
```
drop:banned
block:\x00\x01\x41
reply:Hello:World
reply:data:@file=/etc/response.bin
```

## Commands

- `add <action>:<pattern>[:<response>]` - add a new rule
- `del <rule_id>` - delete a rule
- `lsrules` - list current blacklist
- `clear` - clear screen
- `exit` - terminate proxy
- `help` - show help
