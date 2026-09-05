#!/usr/bin/env python3
"""Generate capabilities/unix pack (definitions + enabled.json5 + CATALOG.md)."""
import json
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "capabilities" / "unix"
OUT.mkdir(parents=True, exist_ok=True)

for p in OUT.glob("unix_*.json5"):
    p.unlink()


def which(cands):
    for c in cands:
        if os.path.isfile(c) and os.access(c, os.X_OK):
            return c
    return cands[0]


WRAP = "./scripts/tools/unix-exec.sh"

# stem, bins, cat, desc, when, when_not, usage, sample, dangerous, needs_args, fixed_extra
TOOLS = []


def T(*a):
    if len(a) != 11:
        raise AssertionError(f"bad arity {len(a)} for {a[0] if a else a}: {a}")
    TOOLS.append(a)


T("true", ["/usr/bin/true", "/bin/true"], "system", "Exit successfully with no output.",
  ["Need a no-op success probe"], ["Do not use for real work"], "true", "(empty; exit 0)", False, False, [])
T("false", ["/usr/bin/false", "/bin/false"], "system", "Exit with failure status and no output.",
  ["Need a failing probe for tests"], ["Do not use for real work"], "false", "(empty; exit 1)", False, False, [])
T("hostname", ["/bin/hostname", "/usr/bin/hostname"], "system", "Print the system hostname.",
  ["Need machine hostname"], ["Do not use for DNS lookups"], "hostname", "host.local", False, False, [])
T("whoami", ["/usr/bin/whoami", "/bin/whoami"], "system", "Print the effective username.",
  ["Need current user name"], ["Do not use for authorization alone"], "whoami", "user", False, False, [])
T("id", ["/usr/bin/id", "/bin/id"], "system", "Print user and group IDs.",
  ["Need uid/gid summary"], ["Do not modify credentials"], "id", "uid=501(...)", False, False, [])
T("uname", ["/usr/bin/uname", "/bin/uname"], "system", "Print system name via uname -a.",
  ["Need kernel/OS identity"], ["Prefer local uname_info"], "uname -a", "Darwin ... arm64", False, False, ["-a"])
T("arch", ["/usr/bin/arch", "/bin/arch", "/usr/bin/uname"], "system", "Print machine architecture.",
  ["Need cpu arch string"], ["Do not confuse with OS version"], "arch", "arm64", False, False, [])
T("nproc", ["/usr/bin/nproc", "/bin/nproc"], "system", "Print number of available processing units.",
  ["Need CPU count"], ["May be missing on macOS"], "nproc", "8", False, False, [])
T("uptime", ["/usr/bin/uptime", "/bin/uptime"], "system", "Print uptime and load averages.",
  ["Need load/uptime snapshot"], ["Not a profiler"], "uptime", "up 3 days, ...", False, False, [])
T("date", ["/bin/date", "/usr/bin/date"], "system", "Print local date/time.",
  ["Need local wall clock"], ["Prefer date_iso for UTC ISO"], "date", "Sat Sep 5 ...", False, False, [])
T("cal", ["/usr/bin/cal", "/bin/cal"], "system", "Print a calendar for the current month.",
  ["Need a month calendar"], ["Not a scheduling DB"], "cal", "September 2026", False, False, [])
T("getconf", ["/usr/bin/getconf", "/bin/getconf"], "system", "Print a POSIX configuration value.",
  ["Need PAGE_SIZE or similar"], ["Only safe conf names"], "getconf PAGE_SIZE", "16384", False, True, [])
T("locale", ["/usr/bin/locale", "/bin/locale"], "system", "Print locale settings.",
  ["Need LANG/LC_* summary"], ["Do not change locales"], "locale", "LANG=...", False, False, [])
T("printenv", ["/usr/bin/printenv", "/bin/printenv"], "system", "Print environment variables.",
  ["Need a specific env var"], ["Avoid full dumps (secrets)"], "printenv PATH", "/usr/bin:/bin", True, True, [])
T("env", ["/usr/bin/env", "/bin/env"], "system", "Print the process environment.",
  ["Rare diagnostics"], ["Leaks secrets"], "env", "PATH=...", True, False, [])
T("tty", ["/usr/bin/tty", "/bin/tty"], "system", "Print the terminal device name.",
  ["Need tty path"], ["May fail when not a tty"], "tty", "not a tty", False, False, [])
T("pwd", ["/bin/pwd", "/usr/bin/pwd"], "system", "Print working directory.",
  ["Confirm sandbox cwd"], ["Prefer pwd_print"], "pwd", "/path/to/root", False, False, [])
T("groups", ["/usr/bin/groups", "/bin/groups"], "system", "Print group memberships.",
  ["Need groups list"], ["Informational"], "groups", "staff everyone", False, False, [])
T("users", ["/usr/bin/users", "/bin/users"], "system", "Print users currently logged in.",
  ["Need logged-in users"], ["May be empty"], "users", "user", False, False, [])
T("who", ["/usr/bin/who", "/bin/who"], "system", "Show who is logged on.",
  ["Need who(1)"], ["Informational"], "who", "user console", False, False, [])
T("w", ["/usr/bin/w", "/bin/w"], "system", "Show who is logged on and activity.",
  ["Need w(1)"], ["Noisy"], "w", "...", False, False, [])
T("uname_s", ["/usr/bin/uname", "/bin/uname"], "system", "Print kernel name (uname -s).",
  ["Need OS family string"], ["Use unix_uname for full"], "uname -s", "Darwin", False, False, ["-s"])
T("uname_m", ["/usr/bin/uname", "/bin/uname"], "system", "Print machine hardware name (uname -m).",
  ["Need machine hardware name"], ["Similar to arch"], "uname -m", "arm64", False, False, ["-m"])
T("sw_vers", ["/usr/bin/sw_vers"], "system", "Print macOS product version.",
  ["Need macOS version"], ["Not on Linux"], "sw_vers", "ProductName: macOS", False, False, [])
T("sysctl_hw", ["/usr/sbin/sysctl", "/sbin/sysctl"], "system", "Read kernel state (e.g. hw.ncpu).",
  ["Need sysctl read"], ["Avoid privileged writes"], "sysctl hw.ncpu", "hw.ncpu: 8", False, True, [])
T("getent", ["/usr/bin/getent", "/bin/getent"], "system", "Get entries from NSS databases.",
  ["Need getent"], ["May be absent on macOS"], "getent passwd", "...", False, True, [])
T("vm_stat", ["/usr/bin/vm_stat"], "system", "mach virtual memory statistics (macOS).",
  ["Need VM stats on macOS"], ["Not on Linux"], "vm_stat", "Pages free: ...", False, False, [])
T("vmstat", ["/usr/bin/vmstat", "/bin/vmstat"], "system", "Virtual memory statistics (Linux).",
  ["Need vmstat"], ["Not on macOS typically"], "vmstat 1 1", "...", False, True, [])
T("free", ["/usr/bin/free", "/bin/free"], "system", "Display free and used memory (Linux).",
  ["Need memory summary"], ["Not on macOS"], "free -h", "...", False, True, [])

T("ls", ["/bin/ls", "/usr/bin/ls"], "fs", "List directory entries.",
  ["Need directory listing"], ["Avoid -R on large trees"], "ls -la .", "total 128", False, True, [])
T("find", ["/usr/bin/find", "/bin/find"], "fs", "Find files under a relative start path.",
  ["Need shallow file discovery"], ["Avoid unbounded find; avoid -delete"], "find . -maxdepth 1 -type f", "./README.md", False, True, [])
T("file", ["/usr/bin/file", "/bin/file"], "fs", "Guess file type of a relative path.",
  ["Need type guess"], ["Not a security scanner"], "file README.md", "UTF-8 text", False, True, [])
T("stat", ["/usr/bin/stat", "/bin/stat"], "fs", "Display file status (prefer builtin stat).",
  ["Need detailed metadata"], ["Prefer matrix builtin stat"], "stat README.md", "...", False, True, [])
T("du", ["/usr/bin/du", "/bin/du"], "fs", "Estimate file space usage.",
  ["Need size of a tree/file"], ["Avoid deep trees"], "du -sh .", "12M .", False, True, [])
T("df", ["/bin/df", "/usr/bin/df"], "fs", "Report free disk space.",
  ["Need filesystem free space"], ["Not per-directory quota"], "df -h .", "Filesystem Size...", False, True, [])
T("basename", ["/usr/bin/basename", "/bin/basename"], "fs", "Strip directory from a path.",
  ["Need final path component"], ["Does not resolve symlinks"], "basename a/b/c", "c", False, True, [])
T("dirname", ["/usr/bin/dirname", "/bin/dirname"], "fs", "Strip last path component.",
  ["Need parent directory string"], ["Path need not exist"], "dirname a/b/c", "a/b", False, True, [])
T("realpath", ["/usr/bin/realpath", "/bin/realpath"], "fs", "Resolve a relative path to absolute.",
  ["Need canonical path"], ["OS realpath rules"], "realpath .", "/abs/path", False, True, [])
T("readlink", ["/usr/bin/readlink", "/bin/readlink"], "fs", "Display symlink value.",
  ["Need symlink target"], ["Fails on non-symlinks"], "readlink link", "../foo", False, True, [])
T("pathchk", ["/usr/bin/pathchk", "/bin/pathchk"], "fs", "Check pathnames for portability.",
  ["Need pathchk"], ["Rare"], "pathchk name", "", False, True, [])
T("touch", ["/usr/bin/touch", "/bin/touch"], "fs", "Create empty files / update mtime.",
  ["Need touch"], ["Writes; keep disabled"], "touch a", "", True, True, [])
T("mkdir_unix", ["/bin/mkdir", "/usr/bin/mkdir"], "fs", "Create directories (prefer builtin mkdir).",
  ["Need mkdir binary"], ["Prefer matrix mkdir"], "mkdir d", "", True, True, [])
T("rmdir", ["/bin/rmdir", "/usr/bin/rmdir"], "fs", "Remove empty directories.",
  ["Need rmdir"], ["Keep disabled"], "rmdir d", "", True, True, [])
T("rm", ["/bin/rm", "/usr/bin/rm"], "fs", "Remove files/directories.",
  ["Need rm"], ["Destructive; never enable lightly"], "rm file", "", True, True, [])
T("cp", ["/bin/cp", "/usr/bin/cp"], "fs", "Copy files/directories.",
  ["Need cp"], ["Writes; keep disabled"], "cp a b", "", True, True, [])
T("mv", ["/bin/mv", "/usr/bin/mv"], "fs", "Move/rename files.",
  ["Need mv"], ["Writes; keep disabled"], "mv a b", "", True, True, [])
T("ln", ["/bin/ln", "/usr/bin/ln"], "fs", "Make links.",
  ["Need ln"], ["Writes; keep disabled"], "ln -s a b", "", True, True, [])
T("install", ["/usr/bin/install", "/bin/install"], "fs", "Copy files with mode.",
  ["Need install(1)"], ["Keep disabled"], "install -m 755 a b", "", True, True, [])
T("dd", ["/bin/dd", "/usr/bin/dd"], "fs", "Convert and copy files.",
  ["Need dd"], ["Extremely dangerous"], "dd if=a of=b", "", True, True, [])
T("sync", ["/bin/sync", "/usr/bin/sync"], "fs", "Flush filesystem buffers.",
  ["Need sync"], ["Usually unnecessary"], "sync", "", False, False, [])

T("wc", ["/usr/bin/wc", "/bin/wc"], "text", "Count lines/words/bytes.",
  ["Need line/word/byte counts"], ["Not a summarizer"], "wc -l README.md", "145 README.md", False, True, [])
T("head", ["/usr/bin/head", "/bin/head"], "text", "Print first lines of a file.",
  ["Need file prefix"], ["Prefer read_file when capped"], "head -n 5 README.md", "line1...", False, True, [])
T("tail", ["/usr/bin/tail", "/bin/tail"], "text", "Print last lines of a file.",
  ["Need file suffix"], ["Avoid -f follow"], "tail -n 5 README.md", "last lines", False, True, [])
T("cat", ["/bin/cat", "/usr/bin/cat"], "text", "Concatenate and print files.",
  ["Need full small file dump"], ["Prefer read_file for limits"], "cat small.txt", "contents", False, True, [])
T("nl", ["/usr/bin/nl", "/bin/nl"], "text", "Number lines of a file.",
  ["Need numbered listing"], ["Not an editor"], "nl README.md", "1 # Neo", False, True, [])
T("od", ["/usr/bin/od", "/bin/od"], "text", "Dump file bytes in octal/hex.",
  ["Need raw byte view"], ["Limit size"], "od -An -tx1 -N 16 README.md", "23 20 4e", False, True, [])
T("hexdump", ["/usr/bin/hexdump", "/bin/hexdump"], "text", "ASCII/hex dump.",
  ["Need hex+ASCII view"], ["Prefer od if missing"], "hexdump -C -n 32 README.md", "00000000 23...", False, True, [])
T("strings", ["/usr/bin/strings", "/bin/strings"], "text", "Extract printable strings.",
  ["Need strings from binary"], ["Not a secret scanner"], "strings bin", "Hello", False, True, [])
T("cmp", ["/usr/bin/cmp", "/bin/cmp"], "text", "Compare two files byte-wise.",
  ["Need equality check"], ["Silent if equal"], "cmp a b", "(empty if equal)", False, True, [])
T("diff", ["/usr/bin/diff", "/bin/diff"], "text", "Show line differences.",
  ["Need unified/diff view"], ["Not a merge tool"], "diff -u a b", "--- a +++ b", False, True, [])
T("sort", ["/usr/bin/sort", "/bin/sort"], "text", "Sort lines of files.",
  ["Need sorted lines"], ["Memory-bound on huge inputs"], "sort names.txt", "a b c", False, True, [])
T("uniq", ["/usr/bin/uniq", "/bin/uniq"], "text", "Filter repeated adjacent lines.",
  ["Need unique adjacent lines"], ["Not global unique alone"], "uniq sorted.txt", "a b", False, True, [])
T("cut", ["/usr/bin/cut", "/bin/cut"], "text", "Cut out fields/columns.",
  ["Need column extraction"], ["Simple delimiter split"], "cut -d, -f1 file.csv", "col1", False, True, [])
T("tr", ["/usr/bin/tr", "/bin/tr"], "text", "Translate or delete characters.",
  ["Need character mapping"], ["Pipelines limited"], "tr a-z A-Z", "HELLO", False, True, [])
T("tee", ["/usr/bin/tee", "/bin/tee"], "text", "Copy stdin to files and stdout.",
  ["Need tee write"], ["Writes; keep disabled"], "tee out.txt", "...", True, True, [])
T("fmt", ["/usr/bin/fmt", "/bin/fmt"], "text", "Simple text formatter.",
  ["Need wrapped paragraphs"], ["Not Markdown-aware"], "fmt -w 72 notes.txt", "wrapped", False, True, [])
T("fold", ["/usr/bin/fold", "/bin/fold"], "text", "Wrap lines to a width.",
  ["Need hard wraps"], ["Breaks mid-word unless -s"], "fold -w 40 file", "...", False, True, [])
T("expand", ["/usr/bin/expand", "/bin/expand"], "text", "Convert tabs to spaces.",
  ["Need detabbed text"], ["Inverse of unexpand"], "expand file", "...", False, True, [])
T("unexpand", ["/usr/bin/unexpand", "/bin/unexpand"], "text", "Convert spaces to tabs.",
  ["Need tabified text"], ["Inverse of expand"], "unexpand file", "...", False, True, [])
T("paste", ["/usr/bin/paste", "/bin/paste"], "text", "Merge lines side by side.",
  ["Need columnar merge"], ["Not SQL join"], "paste a b", "a1 b1", False, True, [])
T("join", ["/usr/bin/join", "/bin/join"], "text", "Join lines of two sorted files.",
  ["Need relational join"], ["Inputs must be sorted"], "join a b", "...", False, True, [])
T("split", ["/usr/bin/split", "/bin/split"], "text", "Split a file into pieces.",
  ["Need chunking"], ["Writes; keep disabled"], "split -l 100 big.txt part.", "part.aa ...", True, True, [])
T("base64", ["/usr/bin/base64", "/bin/base64"], "text", "Base64 encode/decode.",
  ["Need base64"], ["Watch output size"], "base64 README.md", "IyBOZW8K...", False, True, [])
T("sed", ["/usr/bin/sed", "/bin/sed"], "text", "Stream editor.",
  ["Need sed on a relative file"], ["Easy to get wrong"], "sed -n 1,5p README.md", "lines...", False, True, [])
T("awk", ["/usr/bin/awk", "/bin/awk"], "text", "Pattern scanning language.",
  ["Need field processing"], ["Prefer small scripts"], "awk '{print $1}' file", "...", False, True, [])
T("grep_unix", ["/usr/bin/grep", "/bin/grep"], "text", "Search with regex (prefer builtin grep).",
  ["Need grep binary features"], ["Prefer builtin grep"], "grep pattern file", "...", False, True, [])
T("iconv", ["/usr/bin/iconv", "/bin/iconv"], "text", "Convert character encodings.",
  ["Need encoding conversion"], ["Need correct from/to"], "iconv -f utf-8 -t utf-8 file", "...", False, True, [])
T("colrm", ["/usr/bin/colrm", "/bin/colrm"], "text", "Remove columns from lines.",
  ["Need column removal"], ["Rare"], "colrm 1 3", "...", False, True, [])
T("rev", ["/usr/bin/rev", "/bin/rev"], "text", "Reverse characters on each line.",
  ["Need line reverse"], ["Rare"], "rev file", "...", False, True, [])
T("tsort", ["/usr/bin/tsort", "/bin/tsort"], "text", "Topological sort.",
  ["Need tsort"], ["Specialized"], "tsort edges.txt", "...", False, True, [])
T("comm", ["/usr/bin/comm", "/bin/comm"], "text", "Compare two sorted files.",
  ["Need set-like compare"], ["Inputs must be sorted"], "comm a b", "...", False, True, [])
T("look", ["/usr/bin/look", "/bin/look"], "text", "Display lines beginning with a string.",
  ["Need dictionary lookup"], ["Needs sorted file"], "look pre file", "...", False, True, [])

T("md5", ["/sbin/md5", "/usr/bin/md5", "/usr/bin/md5sum", "/bin/md5sum"], "hash",
  "Compute MD5 digest.", ["Need quick fingerprint"], ["Not collision-resistant"], "md5 README.md", "MD5 (...)=...", False, True, [])
T("sha256", ["/usr/bin/shasum", "/usr/bin/sha256sum", "/bin/sha256sum"], "hash",
  "Compute SHA-256 digest.", ["Need stronger fingerprint"], ["Not a signature"], "shasum -a 256 README.md", "abcd... README.md", False, True, [])
T("cksum", ["/usr/bin/cksum", "/bin/cksum"], "hash", "POSIX checksum and byte count.",
  ["Need cksum"], ["Weaker than sha256"], "cksum README.md", "123 456 README.md", False, True, [])
T("sum", ["/usr/bin/sum", "/bin/sum"], "hash", "Legacy sum checksum.",
  ["Need sum(1)"], ["Prefer sha256"], "sum README.md", "...", False, True, [])

T("ps", ["/bin/ps", "/usr/bin/ps"], "process", "Snapshot of processes.",
  ["Need process list"], ["Can be noisy"], "ps -ax -o pid,comm", "PID COMM", False, True, [])
T("sleep", ["/bin/sleep", "/usr/bin/sleep"], "process", "Sleep for N seconds.",
  ["Need a short delay"], ["Do not sleep long"], "sleep 1", "(empty)", False, True, [])
T("time", ["/usr/bin/time", "/bin/time"], "process", "Time a command.",
  ["Need rough timing"], ["Prefer profilers"], "time true", "real 0m0.001s", False, True, [])
T("nice", ["/usr/bin/nice", "/bin/nice"], "process", "Run with modified niceness.",
  ["Need niceness"], ["Usually not needed"], "nice true", "", False, True, [])
T("nohup", ["/usr/bin/nohup", "/bin/nohup"], "process", "Run immune to hangups.",
  ["Rare backgrounding"], ["Writes nohup.out"], "nohup ...", "", True, True, [])
T("kill", ["/bin/kill", "/usr/bin/kill"], "process", "Send a signal to a process.",
  ["Need to signal PIDs"], ["Dangerous"], "kill -0 PID", "", True, True, [])
T("pkill", ["/usr/bin/pkill", "/bin/pkill"], "process", "Signal processes by name.",
  ["Need name-based signals"], ["Dangerous"], "pkill -l", "", True, True, [])
T("timeout", ["/usr/bin/timeout", "/bin/timeout"], "process", "Run with a time limit.",
  ["Need bounded subprocess"], ["Nested exec limited"], "timeout 1 true", "", False, True, [])
T("lsof", ["/usr/sbin/lsof", "/usr/bin/lsof"], "process", "List open files.",
  ["Need open-file listing"], ["Noisy; keep disabled"], "lsof -nP", "...", True, True, [])
T("top", ["/usr/bin/top", "/bin/top"], "process", "Interactive process viewer.",
  ["Need top"], ["Interactive; keep disabled"], "top -l 1", "...", True, True, [])

T("tar", ["/usr/bin/tar", "/bin/tar"], "archive", "Tape archive create/extract.",
  ["Need archive ops"], ["Can overwrite trees"], "tar -tf a.tar", "file1", True, True, [])
T("gzip", ["/usr/bin/gzip", "/bin/gzip"], "archive", "gzip compress.",
  ["Need gzip"], ["Mutates files"], "gzip -c file", "...", True, True, [])
T("gunzip", ["/usr/bin/gunzip", "/bin/gunzip"], "archive", "gunzip decompress.",
  ["Need gunzip"], ["Mutates files"], "gunzip -c file.gz", "...", True, True, [])
T("bzip2", ["/usr/bin/bzip2", "/bin/bzip2"], "archive", "bzip2 compress.",
  ["Need bzip2"], ["Keep disabled"], "bzip2 -c file", "...", True, True, [])
T("xz", ["/usr/bin/xz", "/bin/xz"], "archive", "xz compress/decompress.",
  ["Need xz"], ["Keep disabled"], "xz -c file", "...", True, True, [])
T("zip", ["/usr/bin/zip", "/bin/zip"], "archive", "Create zip archives.",
  ["Need zip"], ["Keep disabled"], "zip out.zip a b", "", True, True, [])
T("unzip", ["/usr/bin/unzip", "/bin/unzip"], "archive", "List/extract zip archives.",
  ["Need zip listing/extract"], ["Extract writes"], "unzip -l a.zip", "Archive: ...", True, True, [])
T("compress", ["/usr/bin/compress", "/bin/compress"], "archive", "Legacy compress(1).",
  ["Rare legacy"], ["Keep disabled"], "compress -c file", "", True, True, [])

T("ping", ["/sbin/ping", "/bin/ping", "/usr/bin/ping"], "net", "ICMP echo.",
  ["Need reachability"], ["Network side effects"], "ping -c 1 127.0.0.1", "1 packets...", True, True, [])
T("curl", ["/usr/bin/curl", "/bin/curl"], "net", "Transfer URLs (prefer http_get).",
  ["Need curl options"], ["Prefer http_get"], "curl -I https://example.com", "HTTP/2 200", True, True, [])
T("wget", ["/usr/bin/wget", "/bin/wget"], "net", "Non-interactive downloader.",
  ["Need wget"], ["Keep disabled"], "wget URL", "", True, True, [])
T("nc", ["/usr/bin/nc", "/bin/nc"], "net", "TCP/UDP swiss army knife.",
  ["Need raw sockets"], ["Dangerous"], "nc -vz host 80", "", True, True, [])
T("dig", ["/usr/bin/dig", "/bin/dig"], "net", "DNS lookup utility.",
  ["Need DNS records"], ["Network dependency"], "dig example.com", "...", True, True, [])
T("host", ["/usr/bin/host", "/bin/host"], "net", "Simple DNS lookup.",
  ["Need A/AAAA lookup"], ["Network dependency"], "host example.com", "has address ...", True, True, [])
T("nslookup", ["/usr/bin/nslookup", "/bin/nslookup"], "net", "DNS query utility.",
  ["Need nslookup"], ["Prefer dig/host"], "nslookup example.com", "...", True, True, [])

T("echo", ["/bin/echo", "/usr/bin/echo"], "util", "Write arguments to stdout.",
  ["Need literal echo"], ["Not write_file"], "echo hello", "hello", False, True, [])
T("printf", ["/usr/bin/printf", "/bin/printf"], "util", "Format and print data.",
  ["Need formatted string"], ["Watch format pitfalls"], "printf '%s\\n' hi", "hi", False, True, [])
T("seq", ["/usr/bin/seq", "/bin/seq"], "util", "Print a sequence of numbers.",
  ["Need numeric sequences"], ["Avoid huge ranges"], "seq 1 3", "1 2 3", False, True, [])
T("yes", ["/usr/bin/yes", "/bin/yes"], "util", "Repeat a string until killed.",
  ["Rare generators"], ["Floods output"], "yes", "y y y", True, True, [])
T("test", ["/bin/test", "/usr/bin/test"], "util", "Evaluate conditional expressions.",
  ["Need shell-like tests"], ["Prefer DAG/LLM logic"], "test -f README.md", "(exit 0)", False, True, [])
T("expr", ["/bin/expr", "/usr/bin/expr"], "util", "Evaluate expressions.",
  ["Need simple arithmetic"], ["Prefer host language"], "expr 1 + 1", "2", False, True, [])
T("bc", ["/usr/bin/bc", "/bin/bc"], "util", "Arbitrary-precision calculator.",
  ["Need calculator"], ["Interactive awkward"], "echo 2+2 | bc", "4", False, True, [])
T("which", ["/usr/bin/which", "/bin/which"], "util", "Locate a program in PATH.",
  ["Need binary location"], ["Informational only"], "which ls", "/bin/ls", False, True, [])
T("man", ["/usr/bin/man", "/bin/man"], "util", "Display manuals (pager).",
  ["Need man page"], ["Interactive; keep disabled"], "man ls", "...", True, True, [])
T("whatis", ["/usr/bin/whatis", "/bin/whatis"], "util", "One-line manual descriptions.",
  ["Need short man summary"], ["Depends on db"], "whatis ls", "ls -- list ...", False, True, [])
T("apropos", ["/usr/bin/apropos", "/bin/apropos"], "util", "Search manual descriptions.",
  ["Need man keyword search"], ["Depends on db"], "apropos copy", "...", False, True, [])
T("factor", ["/usr/bin/factor", "/bin/factor"], "util", "Factor numbers.",
  ["Need factoring"], ["Rare"], "factor 12", "12: 2 2 3", False, True, [])
T("xargs", ["/usr/bin/xargs", "/bin/xargs"], "util", "Build command lines from stdin.",
  ["Need xargs"], ["Injection risk"], "xargs", "", True, True, [])

T("chmod", ["/bin/chmod", "/usr/bin/chmod"], "perm", "Change file modes.",
  ["Need chmod"], ["Mutates permissions"], "chmod 644 file", "", True, True, [])
T("chown", ["/usr/bin/chown", "/bin/chown"], "perm", "Change file owner.",
  ["Need chown"], ["Usually root"], "chown user file", "", True, True, [])
T("chgrp", ["/usr/bin/chgrp", "/bin/chgrp"], "perm", "Change file group.",
  ["Need chgrp"], ["Keep disabled"], "chgrp staff file", "", True, True, [])

ENABLED = [
    "unix_ls", "unix_wc", "unix_head", "unix_tail", "unix_file", "unix_du", "unix_df",
    "unix_find", "unix_basename", "unix_dirname", "unix_realpath", "unix_md5", "unix_sha256",
    "unix_sort", "unix_uniq", "unix_cut", "unix_base64", "unix_hostname", "unix_whoami",
    "unix_id", "unix_uptime", "unix_cal", "unix_true", "unix_false", "unix_seq",
    "unix_printf", "unix_od", "unix_sed", "unix_cksum", "unix_uname_s",
]

PARAMS = {
    "type": "object",
    "properties": {
        "argv": {
            "type": "array",
            "items": {"type": "string"},
            "description": "Extra argv after the binary. Relative paths only; no ..; simple -x/--long flags.",
        }
    },
}


def main():
    catalog = [
        "# Unix 常用工具目录（能力定义参考）\n\n",
        "本文件与 `unix_*.json5` 同步生成。**默认不全量进矩阵**；装载名单见 [`enabled.json5`](enabled.json5)。\n\n",
        "参数型能力经 `./scripts/tools/unix-exec.sh` + `NEO_TOOL_ARGS.argv`（相对路径沙箱）。\n\n",
        "| 能力名 | 类别 | 危险/默认禁用 | 用法样例 | 样例输出 |\n|--------|------|---------------|----------|----------|\n",
    ]
    seen = set()
    missing = []
    for stem, bins, cat, desc, when, when_not, usage, sample, dangerous, needs_args, fixed_extra in TOOLS:
        name = f"unix_{stem}"
        if name in seen:
            continue
        seen.add(name)
        bin_path = which(bins)
        if stem == "arch" and not (os.path.isfile(bin_path) and os.access(bin_path, os.X_OK)):
            bin_path = which(["/usr/bin/uname", "/bin/uname"])
            fixed_extra = ["-m"]
            needs_args = False
        if not any(os.path.isfile(b) and os.access(b, os.X_OK) for b in bins):
            missing.append(stem)

        if needs_args:
            argv = [WRAP, bin_path]
            parameters = PARAMS
        else:
            argv = [bin_path] + list(fixed_extra)
            parameters = {"type": "object", "properties": {}}

        tags = ["unix", cat, "dangerous" if dangerous else "safeish"]
        wn = list(when_not)
        if dangerous:
            wn.append("Not in default enabled.json5; enable only after Policy review")
        outcome = f"Usage: {usage} | Sample: {sample}"
        text = "\n".join(
            [
                "{",
                f'  name: "{name}",',
                f"  description: {json.dumps(desc)},",
                f"  when: {json.dumps(when, ensure_ascii=False)},",
                f"  when_not: {json.dumps(wn, ensure_ascii=False)},",
                f"  tags: {json.dumps(tags)},",
                f"  outcome: {json.dumps(outcome, ensure_ascii=False)},",
                f"  sample_usage: {json.dumps(usage, ensure_ascii=False)},",
                f"  sample_output: {json.dumps(sample, ensure_ascii=False)},",
                f"  argv: {json.dumps(argv)},",
                f"  timeout_sec: {10 if not dangerous else 15},",
                f"  max_output_bytes: {65536 if not dangerous else 32768},",
                '  pass_args: "env",',
                f"  parameters: {json.dumps(parameters)},",
                "}\n",
            ]
        )
        (OUT / f"{name}.json5").write_text(text, encoding="utf-8")
        catalog.append(
            f"| `{name}` | {cat} | {'yes' if dangerous else 'no'} | `{usage}` | {sample.replace('|', '/')} |\n"
        )

    en = [
        "{\n",
        "  // 仅装载下列能力名（与文件名茎一致）。删除本文件则尝试装载全部 unix_*.json5。\n",
        "  load: [\n",
    ]
    for e in ENABLED:
        en.append(f'    "{e}",\n')
    en.append("  ],\n}\n")
    (OUT / "enabled.json5").write_text("".join(en), encoding="utf-8")
    (OUT / "CATALOG.md").write_text("".join(catalog), encoding="utf-8")
    (OUT / "README.md").write_text(
        f"""# capabilities/unix/

Unix 常用工具能力包：约 **{len(seen)}** 个命令定义（用法、选型元数据、样例输出）。

## 加载策略

| 文件 | 作用 |
|------|------|
| `enabled.json5` | 白名单：仅 `load` 中的能力进入矩阵（当前 {len(ENABLED)} 个） |
| `unix_*.json5` | 全量定义；未列入白名单者不进矩阵 |
| `CATALOG.md` | 用法与样例输出总表 |

存在 `enabled.json5` 时只装载名单；否则装载全部（受命令数上限约束，不推荐）。

## 执行约定

- 参数型：`./scripts/tools/unix-exec.sh` + 绝对二进制；`parameters.argv` → `NEO_TOOL_ARGS`。
- 无参探针：直接 exec 绝对路径。
- 危险命令默认不在白名单；与 builtin 重叠时优先 builtin。

编辑 `enabled.json5` 后须重启 `neo`。本目录无子目录。关联：[`docs/applications.md`](../../docs/applications.md)、[`docs/tool.md`](../../docs/tool.md)。
""",
        encoding="utf-8",
    )
    print(f"generated {len(seen)} tools; enabled {len(ENABLED)}; missing_bins={missing}")


if __name__ == "__main__":
    main()
