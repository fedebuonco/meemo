# TODO
## Add opt parsing


# IN PROGRESS
## lowe higher
## Full memory load for higher lower



# DONE
## from cmd loop to cmd as shortcut and to always display scrollable search state
## Double Buffering for avoiding flickering on terminal
# Work around limitation of the iovec.
        Max iovec is 1024 so cannot used to store values
        need to use custom data structure and use scatter gather always requesting the all regions
        and save the address on this struct.
        
## Implement search structure for repetitive searches pass it to cmdloop everytime
## Implement easy read where dia for local is correctly init and returned
## ## create simple command line parser 
        s something
        w pointer something
        q quit
## Read Memory of victim
## Search ui32


## Suggested by AI
High Priority (Critical Issues)

Fix memory leaks in error paths (particularly in read_from_remote_dia())
Complete free_iovec_array() function to free the DIA struct itself
Replace unsafe string functions (sprintf) with bounds-checked alternatives (snprintf)
Implement a complete cleanup function for SearchState struct
Add checks for integer overflows in buffer size calculations
Add proper error handling for process_vm_readv failure cases

Medium Priority (Important Improvements)

Establish consistent error handling pattern across all functions
Replace magic numbers with named constants
Add input validation for all user inputs and function parameters
Fix potential off-by-one errors in buffer handling
Add proper alignment checks when using memcpy between different types
Replace hard-coded buffer sizes with dynamic allocation where appropriate

Low Priority (Style and Structure)

Create a proper logging system to replace DEBUG_PRINT macros
Make type conversions explicit with proper casts
Use const or enum for constants instead of preprocessor macros where possible
Apply consistent naming conventions throughout the codebase
Separate debug print statements from core function logic
Document function interfaces with clear comments

Future Considerations

Add thread safety if the code might be used in multi-threaded environments
Implement defensive checks for null pointers throughout the code
Consider security implications of cross-process memory access
Add unit tests for critical functions