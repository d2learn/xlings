<div align=center><img width="500" src="https://d2learn.org/xlings/xlings-install.gif"></div>

<div align="center">
  <a href="https://forum.d2learn.org/category/9/xlings" target="_blank"><img src="https://img.shields.io/badge/Forum-xlings-blue" /></a>
  <a href="https://d2learn.org" target="_blank"><img src="https://img.shields.io/badge/License-Apache2.0-success" alt="License"></a>
  <a href="https://www.bilibili.com/video/BV1d2DZYsErF" target="_blank"><img src="https://img.shields.io/badge/Video-bilibili-teal" alt="Bilibili"></a>
  <a href="https://youtu.be/uN4amaIAkZ0?si=MpZ6GfLHQoZRmNqc" target="_blank"><img src="https://img.shields.io/badge/Video-YouTube-red" alt="YouTube"></a>
</div>

<div align=center>xlings is a programming learning and course building tool 🛠️</div>
<div align=center> ⌈Software installation, One-click environment configuration, AI code suggestions, Real-time Compilation and execution, Tutorial project setup, and management⌋</div>

<div align="center">
  <a href="README.md" target="_blank">->中文<-</a>
</div>

---

## Recent Updates

- add info features and rust support
- Added Dev-C++ installation support - [Details](http://forum.d2learn.org/post/82)
- Cross-drive run command (Windows) usage - [Details](http://forum.d2learn.org/post/66)
- More updates and discussions -> [More](https://forum.d2learn.org/category/9/xlings)

## Quick Installation

> Execute the one-click installation command in terminal

### Linux

```bash
curl -fsSL https://d2learn.org/xlings-install.sh | bash
```

or

```bash
wget https://d2learn.org/xlings-install.sh -O - | bash
```

### Windows - PowerShell

```bash
Invoke-Expression (Invoke-Webrequest 'https://d2learn.org/xlings-install.ps1.txt' -UseBasicParsing).Content
```

> **Note: More installation methods -> [xlings installation](https://d2learn.github.io/docs/xlings/chapter_1.html)**

## Usage Introduction

### Run Code

> xlings automatically matches programming language and checks code changes in real-time

```bash
xlings run your_code.py
xlings run your_code.c
xlings run your_code.cpp
```

### Environment Setup and Software Installation

**Environment Setup**

> One-click C language environment setup

```bash
xlings install c
```

**Software Installation**

> One-click VSCode installation

```bash
xlings install vscode
```

### Build Interactive Tutorials or Course Labs

- [Project Setup](https://d2learn.github.io/docs/xlings/chapter_3.html)
- [d2ds Project Example](https://github.com/d2learn/d2ds)
- [More Documentation](https://d2learn.org/docs/xlings/chapter_0.html)

## Related Links

- [Homepage](https://d2learn.org/xlings): Tool updates and core feature showcase
- [Forum](https://forum.d2learn.org/category/9/xlings): Issue feedback, project development, idea exchange  
- [xmake](https://github.com/xmake-io/xmake): Provides basic environment for xlings