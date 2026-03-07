# Plan: XIM Attach/Detach Semantics

## Goal

Align the C++ `xim` implementation with the intended install/remove lifecycle:

- install payload once
- attach to each subos via config/xvm
- detach current subos first on remove
- delete payload only when no other subos still references it

## Work Items

1. document target semantics and current gaps
2. refactor install flow so `config` always runs for attach
3. implement workspace reference scan and detach logic
4. add isolated E2E coverage for subos reuse and final payload GC
5. run build/tests and update CI assumptions if needed

