# simple-tcp-proxy
A simple tcp proxy written in C.

You can add rules in `rules.txt` with this format: `<action>:<pattern>[:<response>]`
- **action**: what to do when the pattern is matched:
  - **block**: drop the packet keeping connection alive
  - **drop**: close the connection
  - **reply**: send a custom response back to the client
- **pattern**: sequence to match, it supports escape sequences.
- **response**: (reply action required), data to send back to the client when the pattern is matched. Supports escape sequences as well.

Examples:
```
drop:banned
block:\x00\x01\x41
reply:Hello:World
```
#### Commands
- `addrule <action>:<pattern>:[<response>]` - add a new rule
- `del <rule_id>` - delete a rule
- `lsrules` - list current blacklist
- `clear` - clear screen
- `help` - show help
