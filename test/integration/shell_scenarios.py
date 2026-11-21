from common import IntegrationFailure


def check_startup(session):
    session.wait_for("MyOS v0.0.1", timeout=15)
    session.wait_for(
        "[DISK] ATA primary master and FAT32 filesystem initialized",
        timeout=10,
    )
    session.wait_for("[HEAP] calloc/realloc succeeded", timeout=5)
    session.wait_for("Beijing time (UTC+8)", timeout=5)
    session.pass_check("kernel startup, memory setup, and timer/RTC path reached")
    session.read_serial(0.3)

    session.send_line("secho qemushellcommandok")
    session.wait_for("qemushellcommandok")
    session.pass_check("keyboard-driven shell command executes")


def check_shell_history(session):
    session.send_line("secho hist-first")
    session.wait_for("hist-first")
    session.send_line("secho hist-second")
    session.wait_for("hist-second")
    # Two Up presses recall the first marker command; Enter re-runs it.
    hist_start = len(session.serial_data)
    session.send_key("up")
    session.send_key("up")
    session.send_key("ret")
    session.wait_for_regex(rb"hist-first", start=hist_start, timeout=10)
    session.pass_check("shell history recalls and re-runs a previous command")


def check_shell_line_editing(session):
    # "wrogn" -> delete 'g' before the cursor, step Right to end of line,
    # retype 'g': "wrong". Exercises Left, Backspace-at-cursor, Right.
    session.send_text("secho hist-wrogn")
    session.send_named_key("left")
    session.send_named_key("backspace")
    session.send_named_key("right")
    session.send_text("g")
    session.send_named_key("ret")
    session.wait_for("hist-wrong", timeout=10)
    # "hist-et" -> Left before 't', insert 'x' mid-line: "hist-ext".
    session.send_text("secho hist-et")
    session.send_named_key("left")
    session.send_text("x")
    session.send_named_key("ret")
    session.wait_for("hist-ext", timeout=10)
    session.pass_check("shell cursor keys and mid-line editing work")


def check_shell_quoted_args(session):
    # Double quotes group an argument: "x  yz" stays one 5-byte argument
    # instead of splitting into "x" and "yz".
    session.send_line('write qtest "x  yz"')
    session.wait_for("Wrote 5 bytes to /qtest", timeout=10)
    quoted_read = len(session.serial_data)
    session.send_line("cat qtest")
    session.wait_for_regex(rb"x  yz", start=quoted_read)
    session.send_line("rm qtest")
    session.pass_check("shell double quotes preserve spaces inside an argument")


def check_shell_escapes(session):
    # \n becomes a real newline and \\ a literal backslash inside args.
    session.send_line("write esc a\\nb")
    session.wait_for("Wrote 3 bytes to /esc", timeout=10)
    esc_read = len(session.serial_data)
    session.send_line("cat esc")
    # serial_putchar expands '\n' to CRLF on the wire
    session.wait_for_regex(b"a\r\nb", start=esc_read)
    session.send_line("rm esc")
    session.send_line("write esc2 x\\\\y")
    session.wait_for("Wrote 3 bytes to /esc2", timeout=10)
    esc2_read = len(session.serial_data)
    session.send_line("cat esc2")
    session.wait_for_regex(b"x\\\\y", start=esc2_read)
    session.send_line("rm esc2")
    # Unterminated quote stays literal; mid-word quote is literal too.
    session.send_line('secho "111')
    session.wait_for('"111', timeout=10)
    session.send_line('secho 111"')
    session.wait_for('111"', timeout=10)
    # Invalid escape warns, then the argument is kept literal.
    session.send_line('secho "111\\y222"')
    session.wait_for("unknown escape '\\y'", timeout=10)
    session.wait_for("111\\y222", timeout=10)
    # Empty quotes are literal, so """ stays verbatim; \" escapes a quote.
    session.send_line('secho """')
    session.wait_for('"""', timeout=10)
    session.send_line('secho \\"\\"\\"')
    session.wait_for('"""', timeout=10)
    # \" inside quotes does not terminate the quoted argument.
    q1 = len(session.serial_data)
    session.send_line('secho "\\"\\""')
    session.wait_for_regex(b'""\r?\n', start=q1, timeout=10)
    q2 = len(session.serial_data)
    session.send_line('secho "\\""')
    session.wait_for_regex(b'"\r?\n', start=q2, timeout=10)
    session.pass_check("shell \\n, \\\\, and \\\" escapes expand in arguments")


def check_storage_and_shell(session):
    session.send_line("disk")
    session.wait_for("ATA primary master and FAT32 filesystem initialized")
    session.pass_check("ATA primary master and FAT32 filesystem initialize")
    session.send_line("write testtest hello")
    session.wait_for("Wrote 5 bytes")
    session.read_serial(1.0)
    session.send_line("cat testtest")
    session.wait_for("hello")
    session.pass_check("FAT32 file write and read commands")

    case_ls_start = len(session.serial_data)
    session.send_line("ls")
    session.wait_for_regex(rb"\[LS\] ENTRY testtest 5 bytes",
                           start=case_ls_start)
    session.pass_check("FAT32 preserves filename case in directory listings")

    session.send_line("cp testtest testcopy")
    session.wait_for("[CP] Copied /testtest to /testcopy")
    copy_read_start = len(session.serial_data)
    session.send_line("cat testcopy")
    session.wait_for_regex(rb"hello", start=copy_read_start)
    session.pass_check("cp duplicates a FAT32 file")

    session.send_line("mv testcopy moved_file.txt")
    session.wait_for("[MV] Moved /testcopy to /moved_file.txt")
    session.send_line("mv testtest testtest")
    session.wait_for("[MV] Failed to move /testtest to /testtest")
    mv_ls_start = len(session.serial_data)
    session.send_line("ls")
    session.wait_for_regex(
        rb"\[LS\] ENTRY moved_file\.txt 5 bytes", start=mv_ls_start
    )
    session.pass_check("mv renames a file with an LFN and rejects existing dst")

    session.send_line("mkdir mvdir")
    session.send_line("write mvdir/inner data")
    session.wait_for("[FAT32] Wrote 4 bytes to /mvdir/inner")
    session.send_line("mv mvdir mvdir2")
    session.wait_for("[MV] Moved /mvdir to /mvdir2")
    session.send_line("mv moved_file.txt mvdir2/inside.txt")
    session.wait_for("[MV] Moved /moved_file.txt to /mvdir2/inside.txt")
    dir_ls_start = len(session.serial_data)
    session.send_line("ls mvdir2")
    session.wait_for_regex(rb"\[LS\] ENTRY inner 4 bytes", start=dir_ls_start)
    session.wait_for_regex(
        rb"\[LS\] ENTRY inside\.txt 5 bytes", start=dir_ls_start
    )
    session.pass_check("mv moves a directory and a file into another directory")

    session.send_line("cd mvdir2")
    session.wait_for("[SHELL] CWD /mvdir2")
    session.send_line("cd ..")
    session.wait_for("[SHELL] CWD /")
    session.send_line("rmdir -f mvdir2")
    session.wait_for("[RMDIR] Removed recursively /mvdir2")
    session.send_line("rm testtest")
    session.pass_check("moved directory contents survive and clean up")

    session.send_line("mkdir cptest")
    session.send_line("cp cptest cpdest")
    session.wait_for("[CP] Failed to copy /cptest to /cpdest")
    session.send_line("write cptest/alpha.txt aaa")
    session.wait_for("[FAT32] Wrote 3 bytes to /cptest/alpha.txt")
    session.send_line("mkdir cptest/sub")
    session.send_line("write cptest/sub/beta.txt bbb")
    session.wait_for("[FAT32] Wrote 3 bytes to /cptest/sub/beta.txt")
    session.send_line("cp -r cptest cpdest")
    session.wait_for("[CP] Copied /cptest to /cpdest")
    nested_read_start = len(session.serial_data)
    session.send_line("cat cpdest/sub/beta.txt")
    session.wait_for_regex(rb"bbb", start=nested_read_start)
    recursive_ls_start = len(session.serial_data)
    session.send_line("ls cpdest")
    session.wait_for_regex(rb"\[LS\] ENTRY alpha\.txt 3 bytes",
                           start=recursive_ls_start)
    session.wait_for_regex(rb"\[LS\] ENTRY sub/ <DIR>",
                           start=recursive_ls_start)
    session.pass_check("cp -r recursively copies files and subdirectories")
    session.send_line("rmdir -f cptest")
    session.send_line("rmdir -f cpdest")

    session.send_line("mkdir cpmv")
    session.send_line("write cpmv/tool.txt tool")
    session.wait_for("[FAT32] Wrote 4 bytes to /cpmv/tool.txt")
    session.send_line("mkdir cpsrc")
    session.send_line("write cpsrc/payload.txt data")
    session.wait_for("[FAT32] Wrote 4 bytes to /cpsrc/payload.txt")
    session.send_line("cp -r cpsrc cpmv")
    session.wait_for("[CP] Copied /cpsrc to /cpmv/cpsrc")
    into_dir_read = len(session.serial_data)
    session.send_line("cat cpmv/cpsrc/payload.txt")
    session.wait_for_regex(rb"data", start=into_dir_read)
    session.send_line("cp cpsrc/payload.txt cpmv")
    session.wait_for("[CP] Copied /cpsrc/payload.txt to /cpmv/payload.txt")
    session.pass_check("cp places sources inside an existing directory")

    session.send_line("mv cpsrc cpmv")
    session.wait_for("[MV] Failed to move /cpsrc to /cpmv/cpsrc")
    session.send_line("mkdir cpdst2")
    session.send_line("mv cpsrc cpdst2")
    session.wait_for("[MV] Moved /cpsrc to /cpdst2/cpsrc")
    mv_into_ls = len(session.serial_data)
    session.send_line("ls cpdst2")
    session.wait_for_regex(rb"\[LS\] ENTRY cpsrc/ <DIR>", start=mv_into_ls)
    session.pass_check("mv places a directory inside an existing directory")

    session.send_line("write cpmv/old.txt old")
    session.wait_for("[FAT32] Wrote 3 bytes to /cpmv/old.txt")
    session.send_line("mv cpmv/tool.txt cpmv/old.txt")
    session.wait_for("[MV] Moved /cpmv/tool.txt to /cpmv/old.txt")
    overwrite_read = len(session.serial_data)
    session.send_line("cat cpmv/old.txt")
    session.wait_for_regex(rb"tool", start=overwrite_read)
    session.pass_check("mv replaces an existing destination file")

    session.send_line("rmdir -f cpmv")
    session.send_line("rmdir -f cpdst2")

    session.send_line("write help_aaaaa.1234 lfn works")
    session.wait_for("[FAT32] Wrote 9 bytes to /help_aaaaa.1234")
    lfn_ls_start = len(session.serial_data)
    session.send_line("ls")
    session.wait_for_regex(
        rb"\[LS\] ENTRY help_aaaaa\.1234 9 bytes", start=lfn_ls_start
    )
    lfn_read_start = len(session.serial_data)
    session.send_line("cat help_aaaaa.1234")
    session.wait_for_regex(rb"lfn works", start=lfn_read_start)
    lfn_delete_start = len(session.serial_data)
    session.send_line("rm help_aaaaa.1234")
    session.send_line("ls")
    session.send_line("secho lfn-removal-check")
    session.wait_for("lfn-removal-check")
    if b"[LS] ENTRY help_aaaaa.1234" in session.serial_data[lfn_delete_start:]:
        raise IntegrationFailure("FAT32 rm left the long filename in the directory")
    session.pass_check("FAT32 LFN write, list, lookup, and delete")

    session.send_line("mkdir long_directory_name")
    session.send_line("cd long_directory_name")
    session.wait_for("[SHELL] CWD /long_directory_name")
    session.send_line("write child lfn")
    session.wait_for("[FAT32] Wrote 3 bytes to /long_directory_name/child")
    session.send_line("cd ..")
    session.wait_for("[SHELL] CWD /")
    lfn_directory_delete_start = len(session.serial_data)
    session.send_line("rmdir -f long_directory_name")
    session.send_line("ls")
    session.send_line("secho lfn-directory-removal-check")
    session.wait_for("lfn-directory-removal-check")
    if b"[LS] ENTRY long_directory_name/" in \
            session.serial_data[lfn_directory_delete_start:]:
        raise IntegrationFailure("FAT32 rmdir left the long directory in place")
    session.pass_check("FAT32 LFN directory lookup and recursive deletion")

    session.send_line("cd missing")
    session.wait_for("[SHELL] Directory not found: /missing")
    session.send_line("ls missing")
    session.wait_for("[LS] Unable to list /missing")
    session.pass_check("cd rejects missing directories without creating them")

    session.send_line("mkdir folder")
    session.send_line("cd folder")
    session.wait_for("[SHELL] CWD /folder")
    session.send_line("mkdir sub")
    session.send_line("write child hello")
    session.wait_for("[FAT32] Wrote 5 bytes to /folder/child")
    session.send_line("write sub/nested content")
    session.wait_for("[FAT32] Wrote 7 bytes to /folder/sub/nested")
    session.send_line("ls")
    session.wait_for("[LS] Listing /folder")
    session.wait_for("[LS] ENTRY child 5 bytes")
    session.wait_for("[LS] ENTRY sub/ <DIR>")
    session.send_line("ls /")
    session.wait_for("[LS] ENTRY folder/ <DIR>")
    explicit_ls_start = len(session.serial_data)
    session.send_line("ls /folder")
    session.wait_for_regex(rb"\[LS\] ENTRY child 5 bytes", start=explicit_ls_start)
    session.pass_check("ls lists the current directory or its explicit argument")
    session.send_line("cd ..")
    session.wait_for("[SHELL] CWD /")
    session.pass_check("FAT32 cd parent navigation returns to root")

    session.send_line("rmdir folder")
    session.wait_for("[RMDIR] Failed to remove /folder")
    session.send_line("rmdir -f folder")
    session.wait_for("[RMDIR] Removed recursively /folder")
    session.send_line("ls folder")
    session.wait_for("[LS] Unable to list /folder")
    session.pass_check("rmdir -f recursively removes nested files and directories")


def check_run_from_disk(session):
    run_start = len(session.serial_data)
    session.send_line("run demo.elf")
    session.wait_for_regex(rb"\[ELF\] Loaded ELF32 entry=", start=run_start)
    session.wait_for_regex(rb"Hello from ELF32", start=run_start)
    session.pass_check("run loads and executes ELF32 from the FAT32 disk")
    ps_start = len(session.serial_data)
    session.send_line("ps")
    session.wait_for_regex(rb"\[PS\] 4 DEAD", start=ps_start, timeout=5)


def check_fs_syscalls(session):
    fs_start = len(session.serial_data)
    session.send_line("run fsdemo.elf")
    session.wait_for_regex(rb"FSDEMO-START", start=fs_start)
    session.wait_for_regex(rb"FSDEMO-DONE", start=fs_start)
    if b"FSDEMO-FAIL" in session.serial_data[fs_start:]:
        raise IntegrationFailure("fsdemo.elf reported FSDEMO-FAIL")
    session.pass_check("filesystem syscalls open/write/stat/read/cwd/exit")
    verify_start = len(session.serial_data)
    session.send_line("cat fsdemo.txt")
    session.wait_for_regex(rb"fs-hello-1234", start=verify_start)
    session.pass_check("fsdemo.elf file contents persisted on FAT32")
    session.send_line("rm fsdemo.txt")
