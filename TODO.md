# TODO
## Add option parsing

# IN PROGRESS
## Display values and improve print formatting
## Add status message functions
## Implement lower/higher search
## Implement initial unknown value search for lower/higher

# DONE
## Transitioned from command loop to command shortcuts and always display scrollable search state
## Only load writable regions: heap, stack, data, and bss. These regions must be re-read at every search as they might grow.
    * Correction: Re-reading is not needed; an initial read is sufficient.
## Implemented Double Buffering to avoid terminal flickering
## Worked around the iovec limitation.
    * The maximum iovec is 1024, so it cannot be used to store all values.
    * A custom data structure is needed, always requesting all regions for scatter/gather operations and saving the addresses in this structure.
## Implemented search structure for repetitive searches and passed it to the command loop each time
## Implemented easy read where DIA for local is correctly initialized and returned
## Created a simple command-line parser:
    * `s something`
    * `w pointer something`
    * `q quit`
## Read Memory of victim process
## Searched for ui32