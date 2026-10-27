# Arithmetic Parser in C

A fast, robust math calculator (arithmetic parser) written in standard C17. It reads equations from text files, executes them with precise operator precedence, and writes the results using an all-or-nothing error transaction system.

---

## Usage & CLI Options

```bash
calc [-d DIR | --dir DIR] [-o OUTDIR | --output-dir OUTDIR] [input.txt]
```

### Execution Modes
* **Single File Mode:** `calc -o OUTDIR input.txt` evaluates one file.
* **Directory Mode:** `calc -d DIR -o OUTDIR` evaluates every `*.txt` file directly inside `DIR`. Subfolders and non-txt files (like `.md`) are ignored.

### Output Folder Behavior
* If `OUTDIR` does not exist, the program automatically creates it. If it already exists, it writes directly into it.
* **Without `-o`:** It generates a default output folder in your current directory named `<input_base>_<name>_<studentid>`. 
  * For single files, `<input_base>` is the filename without `.txt`.
  * For directory mode, `<input_base>` is the input folder's name.

### Output File Naming
For any given input file (e.g., `task1.txt`), the evaluated results will be saved exactly as:
```text
<OUTDIR>/task1_<name>_<lastname>_<studentid>.txt
```

---

## How It Works (Under the Hood)

* **1-Based Positioning:** The tokenizer counts *every single character* sequentially (including tabs, spaces, comments, and `\n`). When a syntax or math failure occurs, it reports the error at the exact 1-based index where the problem started.
* **Smart Precedence Logic:** Built using a **Pratt Parser**. Left-associative operators (`+`, `-`, `*`, `/`) evaluate left-to-right. The exponent operator (`**`) evaluates right-to-left (right-associative) by keeping its right-hand binding threshold low.
* **Instant Error Isolation:** If a single mistake occurs anywhere in an input file, the program drops all partial answers for that file and outputs **only** `ERROR:<pos>`.
* **Dynamic Math Traversal:** The engine operates on pure 64-bit integers (`long long`) to prevent rounding errors. If a float is introduced, both operands are promoted to `double`.
* **Zero-Copy Performance:** Instead of reading line-by-line via slow buffer loops, the app reads the entire file into memory at startup. Tokens are analyzed in-place, allowing multi-megabyte files to run instantly without performance slowdowns.

---

## Compilation

Build using standard C17 libraries and flags:
```bash
gcc -O2 -Wall -Wextra -std=c17 -o calc calc.c -lm
```
```bash
# Debug & Memory Sanity Testing
gcc -O1 -g -Wall -Wextra -std=c17 -fsanitize=address,undefined -o calc calc.c -lm
```

## Project Credits & Research:
- [Claude Haiku 4.5](<https://duck.ai/share/17adb4cc-e97d-4d85-a6e3-680497212a24#ZFlq4VX5J11Gp2hJt1vgEDlLn70pIZKZpAHHL-KmA0s>)
- [How to get and evaluate the Expressions from a string in C](https://stackoverflow.com/questions/14472680/how-to-get-and-evaluate-the-expressions-from-a-string-in-c)
- [Effective methods for reading and writing large files in C](https://stackoverflow.com/questions/13498233/effective-methods-for-reading-and-writing-large-files-in-c)
- [File Paths in C](https://learnxbyexample.com/c/file-paths/)
