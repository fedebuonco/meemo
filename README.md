# meemo

<p align="center">
  <img src="meemo_logo.png" alt="meemo logo" width="200"/>
</p>

meemo is a 1kLOC memory scanner.  
It has no third party dependencies (not even curses).

---

When I was a kid I loved playing "3D Pinball for Windows – Space Cadet" on Windows XP.  
To have the highscore in the house, I started using [cheat engine](https://www.cheatengine.org/).  
meemo is a micro (bad) version of it.

## 🚀 Usage

It requires `sudo` since it uses the `process_vm_readv` and `process_vm_writev` syscalls.  
More info about them [here](https://man7.org/linux/man-pages/man2/process_vm_readv.2.html).

`sudo meemo <pid>`

## 🗺️ Roadmap

[TODOs](TODO.md)

## 💡 Why

I was inspired by Salvatore Sanfilippo's [Kilo](https://github.com/antirez/kilo), a <1kLOC text editor.
