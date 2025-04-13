# TODO
## Add opt parsing


# IN PROGRESS
# Work around limitation of the iovec.
        Max iovec is 1024 so cannot used to store values
        need to use custom data structure and use scatter gather always requesting the all regions
        and save the address on this struct.
        
        


# DONE
## Implement search structure for repetitive searches pass it to cmdloop everytime
## Implement easy read where dia for local is correctly init and returned
## ## create simple command line parser 
        s something
        w pointer something
        q quit
## Read Memory of victim
## Search ui32