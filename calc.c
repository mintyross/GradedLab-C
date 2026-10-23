// Rostyslav Hrabenko 260ADB166
// https://github.com/mintyross/GradedLab-C
// Compile with: gcc -O2 -Wall -Wextra -std=c17 -o calc calc.c -lm
// Resources used: <every AI tool, website and book you used; details in comments where you used them>

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <ctype.h> // provides functions for classifying and transforming individual characters
#include <string.h>
#include <sys/stat.h> // used to define data structures and functions that provide information about file attributes, such as file size, permissions, and ownership.
#include <sys/types.h> // contains inbuilt functions to handle characters
#include <dirent.h> // provides types and functions for reading directory entries

#ifdef _WIN32
    #include <direct.h>
    #define mkdir(A, B) _mkdir(A)
#endif

#define STUDENT_SUFFIX "anna_221234" // used for folder name
#define STUDENT_FULL_SUFFIX "anna_berzina_221234" // used for file name
#define MAX_PATH 1024 // maximum destination path length
#define LINE_LEN 256 // maximum line read length used for capping the reading buffer

/*
    FILE OPERATION LOGIC HEADERS
*/
// Creates output folder if it doesn't exist
int create_directory(const char *path);

// Extracts base name from path (handles directory or file extensions)
void get_base_name(const char *path, char *buffer, int is_dir);

// Builds the final required naming path structures and tracks processing parameters
int process_file(const char *input_file, const char *out_dir);



/*
    EVALUATION LOGIC HEADERS
*/
// Source - https://stackoverflow.com/a/66104290
// Posted by Steve Summit
// Retrieved 2026-10-22, License - CC BY-SA 4.0
int evalexpr(const char **); // looks for a series of "terms" being added or subtracted
int eval(const char *expr); // takes a string containing an expression, and returns the int result of evaluating it.
int evalterm(const char **); // 
int gettok(const char **, int *);
void ungettok(int, const char **);
int evalpri(const char **p);
void skipwhite(const char **);

int validate_line(const char *line);

/*
    MAIN FUNCTION
*/
int main(int argc, char *argv[]) {
    char *dir_path = NULL;
    char *out_dir_arg = NULL;
    char *input_file = NULL;

    if (argc < 2) {
        fprintf(stderr, "ERROR: No arguments provided.\n");
        return 1;
    }

    // Loop parse CLI arguments to support flexible parameter ordering
    /*
        Starting from the calc command, this loop iterates through all arguments passed by the user and:
        > Checks if the argument matches -d or --dir (-o or --output-dir).
        > It checks if another argument exists after it.
        > If it exists, it increments i to grab that next argument as the dir_path string
          and skips it on the next loop cycle.
        > If no value follows, it prints an error message and exits the program with an error code.
        > If the argument starts with "-" but didn't match the valid flags above,
          the program flags it as an invalid option, prints an error, and terminates.
        > If the argument doesn't start with "-", it is treated as a file name.
        > If input_file is empty, it assigns this argument to input_file.
        > If input_file was already assigned previously, 
          it prevents the user from providing multiple target files, throwing an error and exiting.
    */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--dir") == 0) {
            if (i + 1 < argc) {
                dir_path = argv[++i];
            } else {
                fprintf(stderr, "ERROR: Missing folder value for option %s.\n", argv[i]);
                return 1;
            }
        } else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output-dir") == 0) {
            if (i + 1 < argc) {
                out_dir_arg = argv[++i];
            } else {
                fprintf(stderr, "ERROR: Missing directory value for option %s.\n", argv[i]);
                return 1;
            }
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "ERROR: Unknown option %s.\n", argv[i]);
            return 1;
        } else {
            if (input_file == NULL) {
                input_file = argv[i];
            } else {
                fprintf(stderr, "ERROR: Multiple input targets specified.\n");
                return 1;
            }
        }
    }

    // Mutually exclusive context validations
    if (dir_path && input_file) {
        fprintf(stderr, "ERROR: Cannot process both a directory (-d) and an individual file together.\n");
        return 1;
    }
    if (!dir_path && !input_file) {
        fprintf(stderr, "ERROR: Missing input file target or input directory option.\n");
        return 1;
    }

    // Resolve final destination directory name
    char final_out_dir[MAX_PATH];
    if (out_dir_arg) {
        strncpy(final_out_dir, out_dir_arg, sizeof(final_out_dir) - 1);
        final_out_dir[sizeof(final_out_dir) - 1] = '\0';
    } else {
        char input_base[MAX_PATH];
        get_base_name(dir_path ? dir_path : input_file, input_base, dir_path ? 1 : 0);
        snprintf(final_out_dir, sizeof(final_out_dir), "%s_%s", input_base, STUDENT_SUFFIX);
    }

    // Create target output destination folder if not present
    if (create_directory(final_out_dir) != 0) {
        fprintf(stderr, "ERROR: Output directory '%s' cannot be created or accessed.\n", final_out_dir);
        return 1;
    }

    // Execution routing based on modes
    if (input_file) {
        struct stat st;
        if (stat(input_file, &st) != 0 || S_ISDIR(st.st_mode)) {
            fprintf(stderr, "ERROR: Input file '%s' does not exist or is invalid.\n", input_file);
            return 1;
        }
        if (process_file(input_file, final_out_dir) != 0) {
            return 1;
        }
    } else if (dir_path) {
        DIR *dir = opendir(dir_path);
        if (!dir) {
            fprintf(stderr, "ERROR: Input directory folder source '%s' does not exist.\n", dir_path);
            return 1;
        }

        // scans a directory to find and process all regular files that end with a .txt extension.
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            char *dot = strrchr(entry->d_name, '.');
            if (dot && strcmp(dot, ".txt") == 0) {
                char full_input_path[MAX_PATH * 2];
                snprintf(full_input_path, sizeof(full_input_path), "%s/%s", dir_path, entry->d_name);
                
                struct stat entry_stat;
                if (stat(full_input_path, &entry_stat) == 0 && S_ISREG(entry_stat.st_mode)) {
                    if (process_file(full_input_path, final_out_dir) != 0) {
                        closedir(dir);
                        return 1;
                    }
                }
            }
        }
        closedir(dir);
    }

    return 0;
}



void read_file(char arr[], const FILE * input_file) {

  char line[LINE_LEN];  // Use a shared standard buffer size

  FILE* fp = fopen(input_file, "r");  // open the file with writing permissions

  if (fp == NULL) {  // throw an error if the file cannot be opened
    fprintf(stderr, "ERROR: file cannot be opened.\n");
    return -1;       // failure
  }

  while(fgets(line, sizeof(line), fp) != NULL) {
    /* TODO:
    1. Check a line for ambigous characters
    2. Evaluate the line
    3. Write the result to an output file
    */
    if (validate_line(line) != 0) {
        // TODO: write ERROR:5 to the file.
        fprintf(stderr, "ERROR: invalid character.\n");
        return -1;
    }
    eval(line);
  }

}

/*
    EVALUATION LOGIC
*/

int evalexpr(const char **p)
{
    int r = evalterm(p);
    while(1) {
        int op = gettok(p, NULL);
        switch(op) {
            case '+': r += evalterm(p); break;
            case '-': r -= evalterm(p); break;
            default: ungettok(op, p); return r;
        }
    }
}

int evalterm(const char **p)
{
    int r = evalpri(p);
    while(1) {
        int op = gettok(p, NULL);
        switch(op) {
            case '*': r *= evalpri(p); break;
            case '/': r /= evalpri(p); break;
            default: ungettok(op, p); return r;
        }
    }
}

int evalpri(const char **p)
{
    int v;
    int op = gettok(p, &v);
    switch(op) {
        case '1': return v;
        case '-': return -evalpri(p);
        case '(':
            v = evalexpr(p);
            op = gettok(p, NULL);
            if(op != ')') {
                fprintf(stderr, "missing ')'\n");
                ungettok(op, p);
            }
            return v;
    }
}

int gettok(const char **p, int *vp)
{
    skipwhite(p);
    char c = **p;
    if(isdigit(c)) {
        char *p2;
        int v = strtoul(*p, &p2, 0);
        *p = p2;
        if(vp) *vp = v;
        return '1';
    }
    (*p)++;
    return c;
}

void skipwhite(const char **p)
{
    while(isspace(**p))
        (*p)++;
}

void ungettok(int op, const char **p)
{
    (*p)--;
}

int eval(const char *expr) // takes a string containing an expression, and returns the int result of evaluating it.
{
    return evalexpr(&expr);
}



/*
    FILE OPERATION LOGIC
*/
int create_directory(const char *path) {
    struct stat st = {0};
    if (stat(path, &st) == -1) {
        if (mkdir(path, 0777) != 0) { // creates directory with specified path and read, write and execute permissions
            return -1;
        }
    }
    return 0;
}

void get_base_name(const char *path, char *buffer, int is_dir) {
    const char *base = strrchr(path, '/');
#ifdef _WIN32
    const char *base_win = strrchr(path, '\\');
    if (!base || (base_win && base_win > base)) base = base_win;
#endif
    base = (base == NULL) ? path : (base + 1);

    if (is_dir) {
        strcpy(buffer, base);
    } else {
        const char *dot = strrchr(base, '.');
        if (dot && strcmp(dot, ".txt") == 0) {
            size_t len = dot - base;
            strncpy(buffer, base, len);
            buffer[len] = '\0';
        } else {
            strcpy(buffer, base);
        }
    }
}

int process_file(const char *input_file, const char *out_dir) {
    char base_name[MAX_PATH];
    get_base_name(input_file, base_name, 0);

    char out_file_path[MAX_PATH * 2];
    snprintf(out_file_path, sizeof(out_file_path), "%s/%s_%s.txt", out_dir, base_name, STUDENT_FULL_SUFFIX);

    // Verify output target can be written to
    FILE *out = fopen(out_file_path, "w");
    if (!out) {
        fprintf(stderr, "ERROR: Cannot write output file to target '%s'.\n", out_file_path);
        return -1;
    }
    fclose(out);

    return 0;
}

/*
    ----
*/

// AI (Claude Haiku 4.5): prompt "how to efficiently check if a char is arithmetic (0-9, +, -, /, *) in C? Intermediate user is trying to validate expressions from a file, on success, write an answer to that expression and on error, write an error code to another file." 
// Link: https://duck.ai/share/17adb4cc-e97d-4d85-a6e3-680497212a24#ZFlq4VX5J11Gp2hJt1vgEDlLn70pIZKZpAHHL-KmA0s
//   Took the line validation loop.

// Helper to separate numbers from operators
bool is_operator(char c) {
    return (c == '+' || c == '-' || c == '*' || c == '/');
}

int validate_line(const char *line) {
    bool expecting_number = true; // True if we need a number next, false if we expect an operator
    bool found_any_content = false;

    for (int i = 0; line[i] != '\0'; i++) {
        char c = line[i];

        // skip spaces but don't break the structural logic
        if (isspace((unsigned char)c)) {
            continue; 
        }

        // enforce allowed characters
        if (!isdigit((unsigned char)c) && !is_operator(c)) {
            //TODO: Write error codes to the files
            return -1; // Invalid character found
        }
        
        found_any_content = true;

        // Structural Syntax Check
        if (isdigit((unsigned char)c)) {
            if (!expecting_number) {
                // Caught something like "5 6" (two adjacent numbers separated by space)
                return -2; // Structural Error: Missing operator
            }
            // Skip the rest of this multi-digit number so '4' and '2' in "42" are not treated as separate things
            while (isdigit((unsigned char)line[i + 1])) {
                i++;
            }
            expecting_number = false; // Next, we must see an operator
        } 
        else if (is_operator(c)) {
            if (expecting_number) {
                // Caught something like "+ 5" or "5 * + 3"
                return -3; // Structural Error: Missing operand / double operator
            }
            expecting_number = true; // Next, we must see a number
        }
    }

    // A valid line cannot end on an operator
    if (!found_any_content || expecting_number) {
        return -4; // Structural Error: Ends with trailing operator or line is empty
    }

    return 0; // Completely valid arithmetic syntax
}
