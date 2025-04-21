# meemo
meemo is a 1kLOC memory scanner.  
It has no third party dependencies (not even curses).

TODO add gif showing it

When I was a kid I loved playing "3D Pinball for Windows – Space Cadet" on windows xp.  
To have the highscore in the house, I started using [cheat engine](https://www.cheatengine.org/).  
meemo is a micro version of it.

## Usage
It requires `sudo` since it uses the `process_vm_readv` and `process_vm_writev` syscalls.  
More info about them [here](https://man7.org/linux/man-pages/man2/process_vm_readv.2.html) 

`sudo meemo <pid>`

## Roadmap

- Extend the search to allow for other types (int32, int64, etc)
- Narrow search with multiple iterations using (higher, lower, not, etc)
- Go back to previous searches
- Save pointers on a scratchpad

## Why

I was inspired by Salvatore Sanfilippo's [Kilo](https://github.com/antirez/kilo) a <1kLOC text editor.
