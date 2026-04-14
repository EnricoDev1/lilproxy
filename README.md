# simple-tcp-proxy
A simple tcp proxy written in C.

You can add rules in `config.txt` with this format: `<action>:<pattern>[:<response>]`
- action: what to do when the pattern is matched:
  - block: drop the packet keeping connection alive
  - drop: close the connection
  - reply: send a custom response back to the client
- pattern: sequence to match, it supports escape sequences.
- response: (reply required), data to send back to the client when the pattern is matched. Supports escape sequences as well.

Examples:
```
drop:banned
block:\x00\x01\x41
reply:Hello:World
```
