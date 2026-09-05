# Unix 常用工具目录（能力定义参考）

本文件与 `unix_*.json5` 同步生成。**默认不全量进矩阵**；装载名单见 [`enabled.json5`](enabled.json5)。

参数型能力经 `./scripts/tools/unix-exec.sh` + `NEO_TOOL_ARGS.argv`（相对路径沙箱）。

| 能力名 | 类别 | 危险/默认禁用 | 用法样例 | 样例输出 |
|--------|------|---------------|----------|----------|
| `unix_true` | system | no | `true` | (empty; exit 0) |
| `unix_false` | system | no | `false` | (empty; exit 1) |
| `unix_hostname` | system | no | `hostname` | host.local |
| `unix_whoami` | system | no | `whoami` | user |
| `unix_id` | system | no | `id` | uid=501(...) |
| `unix_uname` | system | no | `uname -a` | Darwin ... arm64 |
| `unix_arch` | system | no | `arch` | arm64 |
| `unix_nproc` | system | no | `nproc` | 8 |
| `unix_uptime` | system | no | `uptime` | up 3 days, ... |
| `unix_date` | system | no | `date` | Sat Sep 5 ... |
| `unix_cal` | system | no | `cal` | September 2026 |
| `unix_getconf` | system | no | `getconf PAGE_SIZE` | 16384 |
| `unix_locale` | system | no | `locale` | LANG=... |
| `unix_printenv` | system | yes | `printenv PATH` | /usr/bin:/bin |
| `unix_env` | system | yes | `env` | PATH=... |
| `unix_tty` | system | no | `tty` | not a tty |
| `unix_pwd` | system | no | `pwd` | /path/to/root |
| `unix_groups` | system | no | `groups` | staff everyone |
| `unix_users` | system | no | `users` | user |
| `unix_who` | system | no | `who` | user console |
| `unix_w` | system | no | `w` | ... |
| `unix_uname_s` | system | no | `uname -s` | Darwin |
| `unix_uname_m` | system | no | `uname -m` | arm64 |
| `unix_sw_vers` | system | no | `sw_vers` | ProductName: macOS |
| `unix_sysctl_hw` | system | no | `sysctl hw.ncpu` | hw.ncpu: 8 |
| `unix_getent` | system | no | `getent passwd` | ... |
| `unix_vm_stat` | system | no | `vm_stat` | Pages free: ... |
| `unix_vmstat` | system | no | `vmstat 1 1` | ... |
| `unix_free` | system | no | `free -h` | ... |
| `unix_ls` | fs | no | `ls -la .` | total 128 |
| `unix_find` | fs | no | `find . -maxdepth 1 -type f` | ./README.md |
| `unix_file` | fs | no | `file README.md` | UTF-8 text |
| `unix_stat` | fs | no | `stat README.md` | ... |
| `unix_du` | fs | no | `du -sh .` | 12M . |
| `unix_df` | fs | no | `df -h .` | Filesystem Size... |
| `unix_basename` | fs | no | `basename a/b/c` | c |
| `unix_dirname` | fs | no | `dirname a/b/c` | a/b |
| `unix_realpath` | fs | no | `realpath .` | /abs/path |
| `unix_readlink` | fs | no | `readlink link` | ../foo |
| `unix_pathchk` | fs | no | `pathchk name` |  |
| `unix_touch` | fs | yes | `touch a` |  |
| `unix_mkdir_unix` | fs | yes | `mkdir d` |  |
| `unix_rmdir` | fs | yes | `rmdir d` |  |
| `unix_rm` | fs | yes | `rm file` |  |
| `unix_cp` | fs | yes | `cp a b` |  |
| `unix_mv` | fs | yes | `mv a b` |  |
| `unix_ln` | fs | yes | `ln -s a b` |  |
| `unix_install` | fs | yes | `install -m 755 a b` |  |
| `unix_dd` | fs | yes | `dd if=a of=b` |  |
| `unix_sync` | fs | no | `sync` |  |
| `unix_wc` | text | no | `wc -l README.md` | 145 README.md |
| `unix_head` | text | no | `head -n 5 README.md` | line1... |
| `unix_tail` | text | no | `tail -n 5 README.md` | last lines |
| `unix_cat` | text | no | `cat small.txt` | contents |
| `unix_nl` | text | no | `nl README.md` | 1 # Neo |
| `unix_od` | text | no | `od -An -tx1 -N 16 README.md` | 23 20 4e |
| `unix_hexdump` | text | no | `hexdump -C -n 32 README.md` | 00000000 23... |
| `unix_strings` | text | no | `strings bin` | Hello |
| `unix_cmp` | text | no | `cmp a b` | (empty if equal) |
| `unix_diff` | text | no | `diff -u a b` | --- a +++ b |
| `unix_sort` | text | no | `sort names.txt` | a b c |
| `unix_uniq` | text | no | `uniq sorted.txt` | a b |
| `unix_cut` | text | no | `cut -d, -f1 file.csv` | col1 |
| `unix_tr` | text | no | `tr a-z A-Z` | HELLO |
| `unix_tee` | text | yes | `tee out.txt` | ... |
| `unix_fmt` | text | no | `fmt -w 72 notes.txt` | wrapped |
| `unix_fold` | text | no | `fold -w 40 file` | ... |
| `unix_expand` | text | no | `expand file` | ... |
| `unix_unexpand` | text | no | `unexpand file` | ... |
| `unix_paste` | text | no | `paste a b` | a1 b1 |
| `unix_join` | text | no | `join a b` | ... |
| `unix_split` | text | yes | `split -l 100 big.txt part.` | part.aa ... |
| `unix_base64` | text | no | `base64 README.md` | IyBOZW8K... |
| `unix_sed` | text | no | `sed -n 1,5p README.md` | lines... |
| `unix_awk` | text | no | `awk '{print $1}' file` | ... |
| `unix_grep_unix` | text | no | `grep pattern file` | ... |
| `unix_iconv` | text | no | `iconv -f utf-8 -t utf-8 file` | ... |
| `unix_colrm` | text | no | `colrm 1 3` | ... |
| `unix_rev` | text | no | `rev file` | ... |
| `unix_tsort` | text | no | `tsort edges.txt` | ... |
| `unix_comm` | text | no | `comm a b` | ... |
| `unix_look` | text | no | `look pre file` | ... |
| `unix_md5` | hash | no | `md5 README.md` | MD5 (...)=... |
| `unix_sha256` | hash | no | `shasum -a 256 README.md` | abcd... README.md |
| `unix_cksum` | hash | no | `cksum README.md` | 123 456 README.md |
| `unix_sum` | hash | no | `sum README.md` | ... |
| `unix_ps` | process | no | `ps -ax -o pid,comm` | PID COMM |
| `unix_sleep` | process | no | `sleep 1` | (empty) |
| `unix_time` | process | no | `time true` | real 0m0.001s |
| `unix_nice` | process | no | `nice true` |  |
| `unix_nohup` | process | yes | `nohup ...` |  |
| `unix_kill` | process | yes | `kill -0 PID` |  |
| `unix_pkill` | process | yes | `pkill -l` |  |
| `unix_timeout` | process | no | `timeout 1 true` |  |
| `unix_lsof` | process | yes | `lsof -nP` | ... |
| `unix_top` | process | yes | `top -l 1` | ... |
| `unix_tar` | archive | yes | `tar -tf a.tar` | file1 |
| `unix_gzip` | archive | yes | `gzip -c file` | ... |
| `unix_gunzip` | archive | yes | `gunzip -c file.gz` | ... |
| `unix_bzip2` | archive | yes | `bzip2 -c file` | ... |
| `unix_xz` | archive | yes | `xz -c file` | ... |
| `unix_zip` | archive | yes | `zip out.zip a b` |  |
| `unix_unzip` | archive | yes | `unzip -l a.zip` | Archive: ... |
| `unix_compress` | archive | yes | `compress -c file` |  |
| `unix_ping` | net | yes | `ping -c 1 127.0.0.1` | 1 packets... |
| `unix_curl` | net | yes | `curl -I https://example.com` | HTTP/2 200 |
| `unix_wget` | net | yes | `wget URL` |  |
| `unix_nc` | net | yes | `nc -vz host 80` |  |
| `unix_dig` | net | yes | `dig example.com` | ... |
| `unix_host` | net | yes | `host example.com` | has address ... |
| `unix_nslookup` | net | yes | `nslookup example.com` | ... |
| `unix_echo` | util | no | `echo hello` | hello |
| `unix_printf` | util | no | `printf '%s\n' hi` | hi |
| `unix_seq` | util | no | `seq 1 3` | 1 2 3 |
| `unix_yes` | util | yes | `yes` | y y y |
| `unix_test` | util | no | `test -f README.md` | (exit 0) |
| `unix_expr` | util | no | `expr 1 + 1` | 2 |
| `unix_bc` | util | no | `echo 2+2 | bc` | 4 |
| `unix_which` | util | no | `which ls` | /bin/ls |
| `unix_man` | util | yes | `man ls` | ... |
| `unix_whatis` | util | no | `whatis ls` | ls -- list ... |
| `unix_apropos` | util | no | `apropos copy` | ... |
| `unix_factor` | util | no | `factor 12` | 12: 2 2 3 |
| `unix_xargs` | util | yes | `xargs` |  |
| `unix_chmod` | perm | yes | `chmod 644 file` |  |
| `unix_chown` | perm | yes | `chown user file` |  |
| `unix_chgrp` | perm | yes | `chgrp staff file` |  |
