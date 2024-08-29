# xlings
xlings is a tools about - build 'self-study、auto-exercises、tests' projects

---

## Useage

### 0.get xlings

> clone xlings to your root-dir(or other)

```bash
git clone git@github.com:Sunrisepeak/xlings.git
```

### 1.project config files

> create project config files in your project root-dir

**config.xlings.lua**

> config xlings_name, xlings_lang...

```lua
xlings_name = "clings"
xlings_lang = "c"
```

**xmake.lua**

> create xmake.lua file, then include config file and xlings's core/xmake.lua

```lua
includes("config.xlings.lua")

add_includedirs(xlings_name)
includes(xlings_name)

includes("YourLocalXlingsPath/core/xmake.lua")
```

### 2.init project

> init project dir structures and generate book, exercise and some default config file. enable 'xmake clings' command (xlings_name)

```bash
xmake xlings init
```

**dir structures**

```bash
├── book
│   ├── book
|   |    ....
│   ├── .gitignore
│   ├── book.toml
│   └── src
│       ├── chapter_1.md
│       └── SUMMARY.md
├── clings
│   ├── exercises
│   │   └── clings.h
│   ├── tests
│   │   └── clings.c
│   └── xmake.lua
├── config.xlings.lua
└── xmake.lua
```

**render book**

> render markdown file to book and open on your default browser

### 3.use project

**open book**

```bash
xmake xlings book
```

**auto-exercises**

```bash
xmake clings -- clings is xlings_name in your project
```

### 4.custom project

> modify some config file, book config - book.toml, clings config - exercises/xmake.lua....


### Examples & Cases

| examples | cases | other |
| --- | --- | --- |
| [d2c-e](examples/d2c) | | |
| [d2cpp-e](examples/d2cpp) | | |
| [d2ds](examples/d2ds) | [d2ds](https://github.com/Sunrisepeak/d2ds) | |