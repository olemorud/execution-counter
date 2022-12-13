
# Execution-counter
This program uses `inotify` ([man page](https://man7.org/linux/man-pages/man7/inotify.7.html)) to monitor file accesses for each file in $PATH. A potential use-case is to find which files are not needed in a docker-image.


## Build
```
gcc exec_tracker.c -o exec_tracker
```


## Usage
```sh
# Track number of times executables in PATH are accessed
./exec_tracker

# Track number of times regular files in <directory> is accessed
./exec_tracker <directory>
```

## Acknowledgments
The code borrows heavily from the `inotify` manual page in the Linux Programmer's Manual.

