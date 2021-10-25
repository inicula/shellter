## shellter
minimal unix shell written in C++

### features:
* multi-line commands;
* command history;
* builtin commands;
* piping;
* logical operators;
* environment variables;
* stdin/stdout/stderr redirection.

#### sample command:
```bash
addenv $grep_str DEBUG && cat < Makefile | grep $grep_str --color=always -F && sudo df -h 2>/dev/null 1>>file.txt && cat file.txt
```
