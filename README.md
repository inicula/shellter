## shellter
Minimal Unix shell written in C++.

#### Dependencies:
+ libboost-regex-dev;
+ libfmt-dev;
+ libreadline-dev;
+ c++20-compatible compiler (at least `gcc version 10.1` or `clang version 11.0.0`).

### Building:

Clone the repo, go in its root directory and run the makefile:

```bash
$ git clone https://github.com/niculaionut/shellter.git
$ cd shellter
$ make shellter
```

### Features:
* multi-line commands;
* command history:

```sh
[user@host:~]% history
 1  ls -1
 2  echo 2
 3  history
```

* builtin commands;
* piping:

```sh
[user@host:~]% find /usr/include/ -type d | grep boost | wc -l
500
```
* logical operators:

```sh
[user@host:~]% ls | grep -q cpp$ && echo found || echo not found
found
```

* environment variables:


```sh
[user@host:~]% addenv $MYSTR hello
[user@host:~]% ls | grep $MYSTR
helloword.c
```

* stdin/stdout/stderr redirection:

```sh
[user@host:~]% find / -type f 2>no-perms.txt 1>/dev/null || head -n3 < no-perms.txt
find: ‘/.cache’: Permission denied
find: ‘/boot/efi’: Permission denied
find: ‘/run/exim4’: Permission denied
```
