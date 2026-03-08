## Synopsis
`rasdial2` is a drop‑in replacement for `rasdial.exe` that, unlike `rasdial.exe`, uses saved credentials associated with RAS phonebook entries when no explicit credentials are supplied on the command line.

It otherwise behaves identically to `rasdial.exe`.

## Usage
```
rasdial2 entryname [username [password|*]] [/DOMAIN:domain]
        [/PHONE:phonenumber] [/CALLBACK:callbacknumber]
        [/PHONEBOOK:phonebookfile]

rasdial2 [entryname] /DISCONNECT

rasdial2
```

## Build
```
# Visual Studio / MSVC
cl /W3 /O2 /MT rasdial2.c

# MinGW
gcc rasdial2.c -o rasdial2.exe -lrasapi32 -municode -O2 -static -s
```

## License
This project is licensed under the GNU General Public License. See [LICENSE](LICENSE) for details.