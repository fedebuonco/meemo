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

## Demo

<div>
    <a href="https://www.loom.com/share/c8c17f935bec46959ede0cbcf5281d23">
      <p>Watch Video</p>
    </a>
    <a href="https://www.loom.com/share/c8c17f935bec46959ede0cbcf5281d23">
      <img style="max-width:300px;" src="https://cdn.loom.com/sessions/thumbnails/c8c17f935bec46959ede0cbcf5281d23-6e31a33a07cc5c2d-full-play.gif">
    </a>
  </div>

## 🗺️ Roadmap

[TODOs](TODO.md)

## 💡 Why

I was inspired by Salvatore Sanfilippo's [Kilo](https://github.com/antirez/kilo), a <1kLOC text editor.
