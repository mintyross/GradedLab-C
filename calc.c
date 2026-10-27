// Rostyslav Hrabenko 260ADB166
// https://github.com/mintyross/GradedLab-C
// Compile with: gcc -O2 -Wall -Wextra -std=c17 -o calc calc.c -lm
// Resources used: <https://duck.ai/share/17adb4cc-e97d-4d85-a6e3-680497212a24#ZFlq4VX5J11Gp2hJt1vgEDlLn70pIZKZpAHHL-KmA0s>
//                 <https://stackoverflow.com/questions/14472680/how-to-get-and-evaluate-the-expressions-from-a-string-in-c>
//                 <https://stackoverflow.com/questions/13498233/effective-methods-for-reading-and-writing-large-files-in-c>
//                 <https://learnxbyexample.com/c/file-paths/>

// 101 on Arithmetic Parsers in C
/*
This project made me pop.
Every time people pass me by at the university, they constantly ask me how this Parser works.
I've got tired explaining every condition of my tokenizer to all them people,
that's why I wrote this cachy guide. Let's get to it.

    How your tokenizer tracks positions?
    > My tokenizer tracks characters using absolute linear positioning rather than
      tracking lines, what I was trying to achieve before (poor boy).

      - Before scanning a token, the tokenizer sets t.pos = global_idx + 1
        to make it more convenient for humans to read (and obeys the given rules).

      - When skipping spaces, tabs, or comments, global_idx++ advances
        character-by-character. Every \n preserves its position in the index count.

      - When a multi-character token (like an integer or **) is parsed,
        global_idx moves to the end of the text match.
        The starting point (t.pos) is locked onto the token structure,
        while t.end_pos marks where it finishes.

    Why your grammar gives the right precedence, and why ** is right-associative?
    > The code uses a Pratt Parser (Top-Down Operator Precedence), which determines
      how expressions bind together by assigning precedence numbers to tokens.

      - The parser takes operators and operands to the right only if the next
        operator's precedence is greater than or equal to min_prec.

      - For left-associative operations, the parser increments the binding threshold
        by calling parse_expression(prec + 1) for the right-hand operand.
        This forces an expression like 4 + 5 + 6 to evaluate as (4 + 5) + 6,
        because the second + cannot bind to 5 before the first one finishes.

      - To make the power operator evaluate from right to left ((2**3**2 = 2**9 = 512),
        the parser calls parse_expression(prec) without adding 1. This keeps the
        binding threshold low, allowing the right-hand side to take subsequent
        operators first before evaluating the left side.

    Why each error is reported at the position you chose?
    > Errors are reported exactly at the start of the token the parser was
      analyzing when validation failed. The logic is extremely simple:
      you see a mistake - you report it.

    How you decide between integer and float arithmetic?
    > The evaluation engine checks the types of the operands at runtime using a dynamic type system:
      - If both operands have a type of VAL_INT, the calculation uses long long. This ensures
        that integer operations have no floating-point rounding errors.
      - If either operand is a VAL_FLOAT, the program turns both operands to double.
      - Integer powers use bitwise squaring loop to maintain integer precision.
        Float powers defer safely to the standard pow() function.

    How you handle large files without slowing down (no repeated copying)?

    > Instead of reading the file line-by-line using fgets, which as I now know requires repeatedly
      copying data into small string arrays and risks cutting off long lines, the program loads
      the entire file into a memory buffer at startup.

    > The tokenizer does not allocate new string copies for every single token it encounters.
      It maintains a single global Token structure, updating its type, numeric value,
      and character coordinates in place as it moves through the buffer.

    > The parser moves forward through the file sequentially without backtracking.
      It calculates mathematical results on the fly, keeping memory overhead
      minimal regardless of file size.

That's it. You're on your own now.

May the force of code comments be with you.

*/

// Used libraries =======

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <ctype.h>     // Character classification functions (isdigit, isalpha, etc.)
#include <sys/stat.h>  // File system attributes, sizing, and directory creation verification
#include <dirent.h>    // Directory stream opening and reading utilities (DIR, readdir)

#ifdef _WIN32
#include <direct.h>
#define mkdir(A, B) _mkdir(A)
#endif

// Default values =======

#define STUDENT_SUFFIX "rostyslav_260166"               // used for folder name
#define STUDENT_FULL_SUFFIX "rostyslav_hrabenko_260166" // used for file name
#define MAX_PATH 1024                                   // maximum destination path length

// AI (Claude Haiku 4.5): prompt "I don't fully understand how expression tokenizers really work. I need to write a C program to analyze expression from a file and evaluate it. I'm really interested into making one of my own. Explain in-deep with examples."
// Link: https://duck.ai/share/17adb4cc-e97d-4d85-a6e3-680497212a24#ZFlq4VX5J11Gp2hJt1vgEDlLn70pIZKZpAHHL-KmA0s
//   Took the base idea with some snippets. Modified them for floating-point numbers and error handling.

// Data types =======

// Defining all the token types
typedef enum
{
    TOKEN_EOF,
    TOKEN_INT,
    TOKEN_FLOAT,
    TOKEN_PLUS,
    TOKEN_MINUS,
    TOKEN_MUL,
    TOKEN_DIV,
    TOKEN_POW,
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_EOL
} TokenType;

// Evaluation Val Types
typedef enum
{
    VAL_INT,
    VAL_FLOAT
} ValType;

typedef struct
{
    ValType type;
    union
    {
        long long i_val;
        double f_val;
    };
} Value;

typedef struct
{
    TokenType type;
    Value val;
    long long pos;     // 1-based file position where token starts
    long long end_pos; // position immediately after token
} Token;

// Global parsing/tracking states =======

// Used for file storage
const char *file_buf = NULL;

// File length
long long file_len = 0;

// Determines which character is being processed
long long global_idx = 0; // 0-based offset in file_buf

// Output buffering structures to satisfy "All-or-nothing" error requirement
typedef struct
{
    Value val;
} OutputLine;

OutputLine *recorded_outputs = NULL;
int output_count = 0;
int output_capacity = 0;

// dynamically grows a resizable array by doubling its capacity whenever it fills up
void record_output(Value v)
{
    if (output_count >= output_capacity)
    {
        output_capacity = output_capacity == 0 ? 1024 : output_capacity * 2;
        recorded_outputs = realloc(recorded_outputs, output_capacity * sizeof(OutputLine));
    }
    recorded_outputs[output_count++].val = v;
}

// Forward Declarations for Recursive Descent Parser =======

// Returns the next parsed syntax token from the input stream
Token get_next_token(void);

// Parses an expression using operator precedence climbing
Value parse_expression(int min_prec);

// Parses the highest-precedence basic building blocks of an expression
Value parse_primary(void);

// Global token lookahead
Token current_token;
int has_error = 0;
long long error_pos = 0;

// Flags the very first error encountered and logs its code position
void trigger_error(long long pos)
{
    if (!has_error)
    {
        has_error = 1;
        error_pos = pos;
    }
}

// Tokenizer Implementation
/*
    Tokenizers read buffers character-by-character to extract mathematical tokens,
    filter out whitespace or comments, and enforce strict syntax formatting rules
*/
Token get_next_token(void)
{
    Token t;
    t.type = TOKEN_EOF;
    t.pos = global_idx + 1;

    // Handle spaces and tabs, skip comments
    while (global_idx < file_len)
    {
        char c = file_buf[global_idx];
        if (c == ' ' || c == '\t')
        {
            global_idx++;
            continue;
        }
        if (c == '#')
        {
            // Comment: check if it's the first non-blank char of the line
            long int scan = global_idx - 1;
            int first_on_line = 1;
            while (scan >= 0 && file_buf[scan] != '\n')
            {
                if (file_buf[scan] != ' ' && file_buf[scan] != '\t')
                {
                    first_on_line = 0;
                    break;
                }
                scan--;
            }
            if (!first_on_line)
            {
                trigger_error(global_idx + 1); // # anywhere else is invalid
                global_idx++;
                t.type = TOKEN_EOF;
                return t;
            }
            // Skip until EOL or EOF
            while (global_idx < file_len && file_buf[global_idx] != '\n')
            {
                global_idx++;
            }
            continue;
        }
        break;
    }

    t.pos = global_idx + 1;

    if (global_idx >= file_len)
    {
        t.type = TOKEN_EOF; // Hit the End Of File
        t.end_pos = t.pos;
        return t;
    }

    char c = file_buf[global_idx]; // writes character to the buffer

    // Determining the character type
    if (c == '\n')
    {
        t.type = TOKEN_EOL;
        global_idx++;
        t.end_pos = global_idx + 1;
        return t;
    }
    if (c == '+')
    {
        t.type = TOKEN_PLUS;
        global_idx++;
        t.end_pos = global_idx + 1;
        return t;
    }
    if (c == '-')
    {
        t.type = TOKEN_MINUS;
        global_idx++;
        t.end_pos = global_idx + 1;
        return t;
    }
    if (c == '*')
    {
        global_idx++;
        if (global_idx < file_len && file_buf[global_idx] == '*')
        {
            t.type = TOKEN_POW; // If there are two asterics, token is power
            global_idx++;
        }
        else
        {
            t.type = TOKEN_MUL; // Otherwise, token is multiplication
        }
        t.end_pos = global_idx + 1;
        return t;
    }

    if (c == '/')
    {
        t.type = TOKEN_DIV;
        global_idx++;
        t.end_pos = global_idx + 1;
        return t;
    }
    if (c == '(')
    {
        t.type = TOKEN_LPAREN;
        global_idx++;
        t.end_pos = global_idx + 1;
        return t;
    }
    if (c == ')')
    {
        t.type = TOKEN_RPAREN;
        global_idx++;
        t.end_pos = global_idx + 1;
        return t;
    }

    // Parse Numbers (Ints and Floats)
    if (isdigit((unsigned char)c) || c == '.')
    {
        long long start = global_idx;
        int has_dot = 0;

        if (c == '.')
        {
            // Floats cannot start with a dot (.5 is an error)
            trigger_error(global_idx + 1);
            global_idx++;
            t.end_pos = global_idx + 1;
            return t;
        }

        while (global_idx < file_len && isdigit((unsigned char)file_buf[global_idx]))
        {
            global_idx++;
        }

        if (global_idx < file_len && file_buf[global_idx] == '.')
        {
            has_dot = 1;
            global_idx++;
            // Floats cannot end with a dot (5. is an error)
            if (global_idx >= file_len || !isdigit((unsigned char)file_buf[global_idx]))
            {
                trigger_error(global_idx); // points to the '.'
                t.end_pos = global_idx + 1;
                return t;
            }
            while (global_idx < file_len && isdigit((unsigned char)file_buf[global_idx]))
            {
                global_idx++;
            }
        }

        // Catch exponential formatting error (e.g. 1.5e3)
        if (global_idx < file_len && (file_buf[global_idx] == 'e' || file_buf[global_idx] == 'E'))
        {
            trigger_error(global_idx + 1); // error at 'e'
            global_idx++;
            t.end_pos = global_idx + 1;
            return t;
        }

        // Extra sanity check for invalid letters glued to a number
        if (global_idx < file_len && isalpha((unsigned char)file_buf[global_idx]))
        {
            trigger_error(global_idx + 1);
            global_idx++;
            t.end_pos = global_idx + 1;
            return t;
        }

        // Calculates the total length of the extracted number string in bytes
        long long len = global_idx - start;

        // Allocates a temporary heap buffer
        char *num_str = malloc(len + 1);

        // Copies len bytes from the source file buffer into the newly allocated memory.
        memcpy(num_str, &file_buf[start], len);

        // End the buffer with null-terminator
        num_str[len] = '\0';

        // Determining token value type for memory optimization and formatting purposes
        if (has_dot)
        {
            t.type = TOKEN_FLOAT;
            t.val.type = VAL_FLOAT;
            t.val.f_val = strtod(num_str, NULL);
        }
        else
        {
            t.type = TOKEN_INT;
            t.val.type = VAL_INT;
            t.val.i_val = strtoll(num_str, NULL, 10);
        }
        free(num_str);
        t.end_pos = global_idx + 1;
        return t;
    }

    // Default: Invalid character
    trigger_error(global_idx + 1);
    global_idx++;
    t.end_pos = global_idx + 1;
    return t;
}

// Parser Precedence Bindings

/*
    Sources:
    "https://stackoverflow.com/questions/14472680/how-to-get-and-evaluate-the-expressions-from-a-string-in-c"
    "https://stackoverflow.com/questions/13498233/effective-methods-for-reading-and-writing-large-files-in-c"
    Learned more about parsers, tokenizers and how to make them work with large files in C.
*/

// Defines the order of operations for mathematical operators
// by assigning them numerical priority levels
int get_tok_precedence(TokenType type)
{
    switch (type)
    {
    case TOKEN_PLUS:
    case TOKEN_MINUS:
        return 1;
    case TOKEN_MUL:
    case TOKEN_DIV:
        return 2;
    case TOKEN_POW:
        return 3;
    default:
        return -1;
    }
}

// Pratt Parser / Recursive Descent Engine
/*
    Parses the base components of an expression, handling standalone values,
    parenthesized groupings, and missing or misplaced operands.
*/
Value parse_primary()
{
    Value res = {VAL_INT, {0}};
    if (has_error) // Exit on global error
        return res;

    if (current_token.type == TOKEN_INT || current_token.type == TOKEN_FLOAT)
    {
        res = current_token.val;
        current_token = get_next_token();
        return res;
    }

    if (current_token.type == TOKEN_LPAREN)
    {
        current_token = get_next_token();
        res = parse_expression(0);
        if (current_token.type == TOKEN_RPAREN)
        {
            current_token = get_next_token();
        }
        else
        {
            // Missing right parenthesis error
            trigger_error(current_token.pos);
        }
        return res;
    }

    if (current_token.type == TOKEN_RPAREN)
    {
        trigger_error(current_token.pos); // Extra right parenthesis
        return res;
    }

    // If an operator is encountered here, it means no left operand was supplied (Unary error)
    trigger_error(current_token.pos);
    return res;
}

/*
    Evaluates mathematical expressions using precedence climbing while handling operators,
    type promotions, and runtime math errors.

    It parses a left-hand value, hooks into a loop to continuously consume operators
    that meet the minimum precedence threshold (min_prec), computes the right-hand value,
    and immediately executes the math operation.
 */
Value parse_expression(int min_prec)
{
    // Parses the leftmost operand before entering the evaluation loop
    Value lhs = parse_primary();

    if (has_error) // Exit on global error
        return lhs;

    while (1)
    {
        int prec = get_tok_precedence(current_token.type);
        if (prec < min_prec) // Used to keep the order of mathematical operations
            break;

        TokenType op = current_token.type;
        long long op_pos = current_token.pos;
        current_token = get_next_token();

        // Check if two operators are side by side
        if (op == TOKEN_POW || op == TOKEN_MUL || op == TOKEN_DIV || op == TOKEN_PLUS || op == TOKEN_MINUS)
        {
            if (current_token.type == TOKEN_PLUS || current_token.type == TOKEN_MINUS ||
                current_token.type == TOKEN_MUL || current_token.type == TOKEN_DIV || current_token.type == TOKEN_POW)
            {
                trigger_error(current_token.pos);
                return lhs;
            }
        }

        long long rhs_pos = current_token.pos;
        // Right-associative handling for power exponent calculations
        int next_min_prec = (op == TOKEN_POW) ? prec : prec + 1;
        // Recursively parses the right-hand side of a mathematical operation using a dynamic precedence threshold
        Value rhs = parse_expression(next_min_prec);
        if (has_error)
            return lhs;

        // Perform Math Operations & Validate Constraints
        if (lhs.type == VAL_INT && rhs.type == VAL_INT)
        {
            if (op == TOKEN_PLUS)
            {
                lhs.i_val += rhs.i_val;
            }
            else if (op == TOKEN_MINUS)
            {
                lhs.i_val -= rhs.i_val;
            }
            else if (op == TOKEN_MUL)
            {
                lhs.i_val *= rhs.i_val;
            }
            else if (op == TOKEN_DIV)
            {
                if (rhs.i_val == 0)
                {
                    trigger_error(rhs_pos);
                    return lhs;
                }
                lhs.i_val /= rhs.i_val;
            }
            else if (op == TOKEN_POW)
            {
                if (rhs.i_val < 0)
                {
                    trigger_error(rhs_pos);
                    return lhs;
                }
                long long base = lhs.i_val;
                long long exp = rhs.i_val;
                long long p_res = 1;
                while (exp > 0)
                {
                    if (exp & 1) // If the lowest bit of the exponent is odd, it multiplies the running total (p_res) by the current base.
                        p_res *= base;
                    base *= base; // Multiplies the base by itself on every loop iteration, squaring it to prep for the next bit position.
                    exp >>= 1;    // Shifts the exponent's bits right by 1, effectively dividing it by 2 to move to the next power increment until the exponent hits zero.
                }
                lhs.i_val = p_res;
            }
        }
        else
        { // At least one float operand: upgrade everything to Double Float processing
            double l_val = (lhs.type == VAL_INT) ? (double)lhs.i_val : lhs.f_val;
            double r_val = (rhs.type == VAL_INT) ? (double)rhs.i_val : rhs.f_val;
            double f_res = 0.0;
            if (op == TOKEN_PLUS)
                f_res = l_val + r_val;
            else if (op == TOKEN_MINUS)
                f_res = l_val - r_val;
            else if (op == TOKEN_MUL)
                f_res = l_val * r_val;
            else if (op == TOKEN_DIV)
            {
                if (r_val == 0.0)
                {
                    trigger_error(rhs_pos);
                    return lhs;
                }
                f_res = l_val / r_val;
            }
            else if (op == TOKEN_POW)
            {
                // Float power calculations
                f_res = pow(l_val, r_val);
            }
            if (isinf(f_res) || isnan(f_res))
            {
                trigger_error(op_pos);
                return lhs;
            }
            lhs.type = VAL_FLOAT;
            lhs.f_val = f_res;
        }
    }
    return lhs;
}

// Master File Evaluation Routine =======

// Previous Claude prompt + Lab4 materials

void read_file(const char *input_path, const char *output_path)
{
    FILE *fp = fopen(input_path, "r");
    if (!fp)
    {
        fprintf(stderr, "ERROR: file cannot be opened.\n");
        return;
    }

    // Read full file context instantly to perfectly protect arbitrary lengths
    fseek(fp, 0, SEEK_END);
    // Determine file size
    file_len = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    // Reads contents to a buffer
    char *buf = malloc(file_len + 1);
    long long read_bytes = fread(buf, 1, file_len, fp);
    buf[read_bytes] = '\0'; // end with terminator
    file_len = read_bytes;

    fclose(fp);
    file_buf = buf;

    global_idx = 0;
    has_error = 0;

    current_token = get_next_token();

    while (current_token.type != TOKEN_EOF && !has_error) // if no errors
    {
        // Skip blank lines
        if (current_token.type == TOKEN_EOL)
        {
            current_token = get_next_token();
            continue;
        }
        Value res = parse_expression(0);
        if (has_error)
            break;
        if (current_token.type == TOKEN_EOL || current_token.type == TOKEN_EOF)
        {
            record_output(res);
            if (current_token.type == TOKEN_EOL)
            {
                current_token = get_next_token();
            }
        }
        else
        {
            trigger_error(current_token.pos);
            break;
        }
    }

    // Determine final output targets based on execution success state
    FILE *out_fp = fopen(output_path, "w");
    if (!out_fp)
    {
        free(buf);
        free(recorded_outputs);
        return;
    }
    if (has_error)
    {
        fprintf(out_fp, "ERROR:%lld\n", error_pos);
    }
    else
    {
        for (int i = 0; i < output_count; i++)
        {
            if (recorded_outputs[i].val.type == VAL_INT) // Writing evaluated numbers to a file
            {
                fprintf(out_fp, "%lld\n", recorded_outputs[i].val.i_val);
            }
            else
            {
                fprintf(out_fp, "%.15g\n", recorded_outputs[i].val.f_val);
            }
        }
    }

    // Preventing memory leaks
    fclose(out_fp);
    free(buf);
    free(recorded_outputs);
    recorded_outputs = NULL;
    output_count = 0;
    output_capacity = 0;
}

/*
    FILE OPERATION LOGIC HEADERS
*/
// Creates output folder if it doesn't exist
int create_directory(const char *path);

// Extracts base name from path (handles directory or file extensions)
void get_base_name(const char *path, char *buffer, int is_dir);

// Builds the final required naming path structures and tracks processing parameters
int process_file(const char *input_file, const char *out_dir);

// Source - https://stackoverflow.com/a/66104290
// Posted by Steve Summit
// Retrieved 2026-10-22, License - CC BY-SA 4.0

/*
    MAIN FUNCTION
*/

int main(int argc, char *argv[])
{
    char *dir_path = NULL;
    char *out_dir_arg = NULL;
    char *input_file = NULL;

    if (argc < 2) // it's just "calc"
    {
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
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--dir") == 0) // input directory handling
        {
            if (i + 1 < argc)
            {
                dir_path = argv[++i];
            }
            else
            {
                fprintf(stderr, "ERROR: Missing folder value for option %s.\n", argv[i]);
                return 1;
            }
        }
        else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output-dir") == 0) // output directory handling
        {
            if (i + 1 < argc)
            {
                out_dir_arg = argv[++i];
            }
            else
            {
                fprintf(stderr, "ERROR: Missing directory value for option %s.\n", argv[i]);
                return 1;
            }
        }
        else if (argv[i][0] == '-') // things happen
        {
            fprintf(stderr, "ERROR: Unknown option %s.\n", argv[i]);
            return 1;
        }
        else
        {
            if (input_file == NULL)
            {
                input_file = argv[i]; // default option
            }
            else
            {
                fprintf(stderr, "ERROR: Multiple input targets specified.\n");
                return 1;
            }
        }
    }

    // Mutually exclusive context validations
    if (dir_path && input_file)
    {
        fprintf(stderr, "ERROR: Cannot process both a directory (-d) and an individual file together.\n");
        return 1;
    }
    if (!dir_path && !input_file)
    {
        fprintf(stderr, "ERROR: Missing input file target or input directory option.\n");
        return 1;
    }

    // Resolve final destination directory name
    char final_out_dir[MAX_PATH * 2];

    // Link: https://learnxbyexample.com/c/file-paths/
    // Learned how to handle file paths: getting, combining and using.

    if (out_dir_arg)
    {
        // Check if the argument fits entirely without silent truncation
        if (strlen(out_dir_arg) >= sizeof(final_out_dir))
        {
            fprintf(stderr, "Error: Output directory path is too long\n");
            return 0; // Or appropriate error exit
        }
        strcpy(final_out_dir, out_dir_arg);
    }
    else
    {
        char input_base[MAX_PATH];

        // Ensure the source paths fit within MAX_PATH bounds before extracting base name
        const char *source_path = dir_path ? dir_path : input_file;
        if (strlen(source_path) >= sizeof(input_base))
        {
            fprintf(stderr, "Error: Input path exceeds maximum allowed length\n");
            return 0;
        }

        get_base_name(source_path, input_base, dir_path ? 1 : 0);

        // Explicitly validate that both the base name and suffix fit inside the target buffer
        int written = snprintf(final_out_dir, sizeof(final_out_dir), "%s_%s", input_base, STUDENT_SUFFIX);
        if (written < 0 || (size_t)written >= sizeof(final_out_dir))
        {
            fprintf(stderr, "Error: Final destination path buffer overflow prevented\n");
            return 0;
        }
    }

    // Create target output destination folder if not present
    if (create_directory(final_out_dir) != 0)
    {
        fprintf(stderr, "ERROR: Output directory '%s' cannot be created or accessed.\n", final_out_dir);
        return 1;
    }

    // Execution routing based on modes
    if (input_file)
    {
        struct stat st;
        if (stat(input_file, &st) != 0 || S_ISDIR(st.st_mode))
        {
            fprintf(stderr, "ERROR: Input file '%s' does not exist or is invalid.\n", input_file);
            return 1;
        }
        if (process_file(input_file, final_out_dir) != 0)
        {
            return 1;
        }
    }
    else if (dir_path) // processing all the .txt files if no specific file given
    {
        DIR *dir = opendir(dir_path);
        if (!dir)
        {
            fprintf(stderr, "ERROR: Input directory folder source '%s' does not exist.\n", dir_path);
            return 1;
        }

        // scans a directory to find and process all regular files that end with a .txt extension.
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL)
        {
            has_error = 0;

            // Skip hidden files or exact matches like "." and ".."
            if (entry->d_name[0] == '.')
            {
                continue;
            }

            char *dot = strrchr(entry->d_name, '.');
            if (dot && strcmp(dot, ".txt") == 0)
            {
                char full_input_path[MAX_PATH * 2];
                int written;

                // Check if dir_path already ends with a slash to avoid duplicate slashes
                size_t len = strlen(dir_path);
                if (len > 0 && (dir_path[len - 1] == '/' || dir_path[len - 1] == '\\'))
                {
                    written = snprintf(full_input_path, sizeof(full_input_path), "%s%s", dir_path, entry->d_name);
                }
                else
                {
                    written = snprintf(full_input_path, sizeof(full_input_path), "%s/%s", dir_path, entry->d_name);
                }

                // Catch path buffer truncation errors instantly
                if (written < 0 || (size_t)written >= sizeof(full_input_path))
                {
                    fprintf(stderr, "Error: Full input path buffer overflow prevented\n");
                    closedir(dir);
                    return 1;
                }

                struct stat entry_stat;
                if (stat(full_input_path, &entry_stat) == 0 && S_ISREG(entry_stat.st_mode))
                {
                    if (process_file(full_input_path, final_out_dir) != 0)
                    {
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

/*
    FILE OPERATION LOGIC
*/
int create_directory(const char *path)
{
    struct stat st = {0};
    if (stat(path, &st) == -1)
    {
        if (mkdir(path, 0777) != 0)
        { // creates directory with specified path and read, write and execute permissions
            return -1;
        }
    }
    return 0;
}

void get_base_name(const char *path, char *buffer, int is_dir)
{
    const char *base = strrchr(path, '/');
#ifdef _WIN32
    const char *base_win = strrchr(path, '\\');
    if (!base || (base_win && base_win > base))
        base = base_win;
#endif
    base = (base == NULL) ? path : (base + 1);

    if (is_dir)
    {
        strcpy(buffer, base);
    }
    else
    {
        const char *dot = strrchr(base, '.');
        if (dot && strcmp(dot, ".txt") == 0)
        {
            size_t len = dot - base;
            strncpy(buffer, base, len);
            buffer[len] = '\0';
        }
        else
        {
            strcpy(buffer, base);
        }
    }
}

// Coordinates the end-to-end translation pipeline for a single file
// by resolving its output destination, initiating parsing, and verifying execution errors
int process_file(const char *input_file, const char *out_dir)
{
    char base_name[MAX_PATH];
    get_base_name(input_file, base_name, 0);

    // Construct the exact output target filepath
    char out_file_path[MAX_PATH * 2];
    snprintf(out_file_path, sizeof(out_file_path), "%s/%s_%s.txt", out_dir, base_name, STUDENT_FULL_SUFFIX);

    // Evaluating the file
    read_file(input_file, out_file_path);

    return 0; // Here we go!
}